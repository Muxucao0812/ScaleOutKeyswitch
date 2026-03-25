#include <sst/core/sst_config.h>

#include "compute_unit.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>

#include "accel_event.h"

namespace {

uint32_t parseCycles(const std::string& text)
{
    uint32_t value = 0;
    for (char ch : text) {
        if (ch >= '0' && ch <= '9') {
            value = (value * 10) + static_cast<uint32_t>(ch - '0');
        }
    }
    return value == 0 ? 1 : value;
}

std::string laneScopeName(uint32_t lane)
{
    std::ostringstream name;
    name << "lane" << std::setw(3) << std::setfill('0') << lane;
    return name.str();
}

uint64_t ceilDivide(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0) {
        return 0;
    }
    return (numerator + denominator - 1) / denominator;
}

bool overlaps(uint64_t lhs_start, uint64_t lhs_bytes, uint64_t rhs_start, uint64_t rhs_bytes)
{
    const uint64_t lhs_end = lhs_start + lhs_bytes;
    const uint64_t rhs_end = rhs_start + rhs_bytes;
    return lhs_start < rhs_end && rhs_start < lhs_end;
}

uint32_t benesStages(uint32_t n)
{
    if (n <= 1) {
        return 1;
    }

    uint32_t log2n = 0;
    uint32_t value = 1;
    while (value < n) {
        value <<= 1;
        ++log2n;
    }

    return (2 * log2n) - 1;
}

} // namespace

namespace Tutorial {

ComputeUnit::ComputeUnit(SST::ComponentId_t id, SST::Params& params) :
    SST::Component(id),
    hbm_link_(nullptr),
    spu_{1, 12, 1},
    nttu_{1, 4, 2, 2},
    nttu_twiddle_from_hbm_(false),
    context_(loadCryptoContext(params, 8192, params.find<uint32_t>("lane_width_bits", 32), 1)),
    lane_width_bits_(params.find<uint32_t>("lane_width_bits", context_.data_width_bits)),
    scratchpad_bytes_(params.find<uint64_t>("scratchpad_bytes", 1048576ULL)),
    scratchpad_bandwidth_bytes_per_cycle_(params.find<uint32_t>("scratchpad_bandwidth_bytes_per_cycle", 128)),
    scratchpad_banks_(params.find<uint32_t>("scratchpad_banks", 32)),
    bank_interleave_bytes_(params.find<uint32_t>("bank_interleave_bytes", 32)),
    bank_read_ports_(params.find<uint32_t>("bank_read_ports", 1)),
    bank_write_ports_(params.find<uint32_t>("bank_write_ports", 1)),
    bank_conflict_penalty_cycles_(params.find<uint32_t>("bank_conflict_penalty_cycles", 1)),
    dma_engines_(params.find<uint32_t>("dma_engines", 2)),
    alu_engines_(params.find<uint32_t>("alu_engines", 2)),
    current_cycle_(0)
{
    const uint32_t verbose = params.find<uint32_t>("verbose", 0);
    output_.init("[CU] ", verbose, 0, SST::Output::STDOUT);

    output_.verbose(
        CALL_INFO,
        1,
        0,
        "Crypto context: N=%u data_width_bits=%u limbs=%u element_bytes=%llu (lane_width_bits=%u)\n",
        context_.poly_degree_n,
        context_.data_width_bits,
        context_.limbs,
        static_cast<unsigned long long>(context_.elementBytes()),
        lane_width_bits_);

    hbm_link_ = configureLink(
        "hbm",
        new SST::Event::Handler2<ComputeUnit, &ComputeUnit::handleHbmEvent>(this));
    sst_assert(hbm_link_ != nullptr, CALL_INFO, -1, "ComputeUnit requires the 'hbm' port to be connected\n");

    const SST::Params spu_params = params.get_scoped_params("spu");
    spu_.count = spu_params.find<uint32_t>("count", 1);
    spu_.automorphism_latency_cycles = parseCycles(spu_params.find<std::string>("automorphism_latency", "12cycle"));
    spu_.switch_latency_cycles = parseCycles(spu_params.find<std::string>("switch_latency", "1cycle"));

    const SST::Params nttu_params = params.get_scoped_params("nttu");
    nttu_.count = nttu_params.find<uint32_t>("count", 1);
    nttu_.butterfly_latency_cycles = parseCycles(nttu_params.find<std::string>("butterfly_latency", "4cycle"));
    nttu_.twiddle_penalty_cycles = parseCycles(nttu_params.find<std::string>("twiddle_penalty", "2cycle"));
    nttu_.shuffle_penalty_cycles = parseCycles(nttu_params.find<std::string>("shuffle_penalty", "2cycle"));
    nttu_twiddle_from_hbm_ = (nttu_params.find<uint32_t>("twiddle_from_hbm", 0) != 0);

    const uint32_t num_lanes = params.find<uint32_t>("num_lanes", 256);
    lanes_.reserve(num_lanes);
    for (uint32_t lane = 0; lane < num_lanes; ++lane) {
        const SST::Params lane_params = params.get_scoped_params(laneScopeName(lane));
        lanes_.push_back(
            {
                lane_params.find<uint32_t>("mod_multiplier.count", 1),
                parseCycles(lane_params.find<std::string>("mod_multiplier.latency", "2cycle")),
                lane_params.find<uint32_t>("mod_adder.count", 2),
                parseCycles(lane_params.find<std::string>("mod_adder.latency", "1cycle")),
            });
    }

    bank_busy_until_.assign(std::max<uint32_t>(1, scratchpad_banks_), 0);

    const std::string clock = params.find<std::string>("clock", "1.5GHz");
    registerClock(clock, new SST::Clock::Handler2<ComputeUnit, &ComputeUnit::tick>(this));
}

void ComputeUnit::handleHbmEvent(SST::Event* event)
{
    AcceleratorEvent* request = dynamic_cast<AcceleratorEvent*>(event);
    sst_assert(request != nullptr, CALL_INFO, -1, "ComputeUnit received unexpected event type on 'hbm'\n");

    request->src_component = static_cast<uint32_t>(ComponentType::ComputeUnit);
    request->dst_component = static_cast<uint32_t>(ComponentType::HBM);
    output_.verbose(
        CALL_INFO,
        1,
        0,
        "Recv from HBM: inst=%llu txn=%llu opcode=%s\n",
        static_cast<unsigned long long>(request->parent_instruction_id),
        static_cast<unsigned long long>(request->txn_id),
        Instruction::opcodeName(static_cast<Instruction::Opcode>(request->opcode)));

    if (!enqueueWork(request)) {
        request->event_type = static_cast<uint32_t>(EventType::Completion);
        request->completion = true;
        output_.verbose(
            CALL_INFO,
            1,
            0,
            "Reject work: inst=%llu txn=%llu status=%u\n",
            static_cast<unsigned long long>(request->parent_instruction_id),
            static_cast<unsigned long long>(request->txn_id),
            request->status);
        hbm_link_->send(request);
    }
}

bool ComputeUnit::tick(SST::Cycle_t cycle)
{
    (void)cycle;
    ++current_cycle_;

    serviceRunningWork(dma_running_);
    serviceRunningWork(alu_running_);
    serviceRunningWork(spu_running_);
    serviceRunningWork(ntt_running_);

    serviceDomainQueue(dma_queue_, dma_running_, std::max<uint32_t>(1, dma_engines_));
    serviceDomainQueue(alu_queue_, alu_running_, std::max<uint32_t>(1, alu_engines_));
    serviceDomainQueue(spu_queue_, spu_running_, std::max<uint32_t>(1, spu_.count));
    serviceDomainQueue(ntt_queue_, ntt_running_, std::max<uint32_t>(1, nttu_.count));

    if (!barrier_queue_.empty()) {
        WorkItem barrier_item = barrier_queue_.front();
        const bool pending_work_left = !allDomainQueuesEmpty() ||
                                       !dma_running_.empty() ||
                                       !alu_running_.empty() ||
                                       !spu_running_.empty() ||
                                       !ntt_running_.empty();

        bool prior_uses_done = true;
        for (const RegionUse& use : inflight_region_uses_) {
            if (!use.done && use.seq_no < barrier_item.seq_no) {
                prior_uses_done = false;
                break;
            }
        }

        if (!pending_work_left && prior_uses_done) {
            barrier_queue_.pop_front();
            barrier_item.event->status = static_cast<uint32_t>(EventStatus::Ok);
            barrier_item.event->event_type = static_cast<uint32_t>(EventType::Completion);
            barrier_item.event->completion = true;
            barrier_item.event->ready_cycle_hint = current_cycle_;
            output_.verbose(
                CALL_INFO,
                1,
                0,
                "Barrier release: inst=%llu txn=%llu\n",
                static_cast<unsigned long long>(barrier_item.event->parent_instruction_id),
                static_cast<unsigned long long>(barrier_item.event->txn_id));
            hbm_link_->send(barrier_item.event);
        }
    }

    return false;
}

bool ComputeUnit::enqueueWork(AcceleratorEvent* event)
{
    WorkItem item = buildWorkItem(event);

    for (const RegionSpec& region : item.read_regions) {
        if (!fitsScratchpad(region.start, region.bytes)) {
            event->status = static_cast<uint32_t>(EventStatus::InvalidAddress);
            return false;
        }
    }

    for (const RegionSpec& region : item.write_regions) {
        if (!fitsScratchpad(region.start, region.bytes)) {
            event->status = static_cast<uint32_t>(EventStatus::InvalidAddress);
            return false;
        }
    }

    switch (item.domain) {
    case WorkDomain::DMA:
        dma_queue_.push_back(item);
        registerRegionUses(item);
        output_.verbose(
            CALL_INFO,
            2,
            0,
            "Enqueue DMA: inst=%llu txn=%llu qdepth=%zu\n",
            static_cast<unsigned long long>(event->parent_instruction_id),
            static_cast<unsigned long long>(event->txn_id),
            dma_queue_.size());
        return true;
    case WorkDomain::ALU:
        alu_queue_.push_back(item);
        registerRegionUses(item);
        output_.verbose(
            CALL_INFO,
            2,
            0,
            "Enqueue ALU: inst=%llu txn=%llu qdepth=%zu\n",
            static_cast<unsigned long long>(event->parent_instruction_id),
            static_cast<unsigned long long>(event->txn_id),
            alu_queue_.size());
        return true;
    case WorkDomain::SPU:
        spu_queue_.push_back(item);
        registerRegionUses(item);
        output_.verbose(
            CALL_INFO,
            2,
            0,
            "Enqueue SPU: inst=%llu txn=%llu qdepth=%zu\n",
            static_cast<unsigned long long>(event->parent_instruction_id),
            static_cast<unsigned long long>(event->txn_id),
            spu_queue_.size());
        return true;
    case WorkDomain::NTT:
        ntt_queue_.push_back(item);
        registerRegionUses(item);
        output_.verbose(
            CALL_INFO,
            2,
            0,
            "Enqueue NTT: inst=%llu txn=%llu qdepth=%zu\n",
            static_cast<unsigned long long>(event->parent_instruction_id),
            static_cast<unsigned long long>(event->txn_id),
            ntt_queue_.size());
        return true;
    case WorkDomain::BARRIER:
        barrier_queue_.push_back(item);
        output_.verbose(
            CALL_INFO,
            2,
            0,
            "Enqueue BARRIER: inst=%llu txn=%llu\n",
            static_cast<unsigned long long>(event->parent_instruction_id),
            static_cast<unsigned long long>(event->txn_id));
        return true;
    }

    event->status = static_cast<uint32_t>(EventStatus::Error);
    return false;
}

ComputeUnit::WorkItem ComputeUnit::buildWorkItem(AcceleratorEvent* event) const
{
    WorkItem item;
    item.event = event;
    item.domain = WorkDomain::ALU;
    item.state = WorkState::WaitDependency;
    item.seq_no = event->seq_no;
    item.remaining_cycles = 0;
    item.earliest_start_cycle = 0;
    item.completion_cycle = 0;

    const Instruction::Opcode opcode = static_cast<Instruction::Opcode>(event->opcode);
    const uint64_t bytes = bytesForEvent(*event);
    const uint64_t src_spm = (event->spm_address != 0) ? event->spm_address : event->src0_address;
    const uint64_t dst_spm = (event->spm_address != 0) ? event->spm_address : event->dst_address;

    switch (opcode) {
    case Instruction::Opcode::HBM_LOAD:
        item.domain = WorkDomain::DMA;
        item.write_regions.push_back({dst_spm, bytes});
        break;
    case Instruction::Opcode::HBM_STORE:
        item.domain = WorkDomain::DMA;
        item.read_regions.push_back({src_spm, bytes});
        break;
    case Instruction::Opcode::MOD_ADD:
    case Instruction::Opcode::MOD_SUB:
    case Instruction::Opcode::MOD_MUL:
        item.domain = WorkDomain::ALU;
        item.read_regions.push_back({event->src0_address, bytes});
        item.read_regions.push_back({event->src1_address, bytes});
        item.write_regions.push_back({event->dst_address, bytes});
        break;
    case Instruction::Opcode::MAC:
        item.domain = WorkDomain::ALU;
        item.read_regions.push_back({event->src0_address, bytes});
        item.read_regions.push_back({event->src1_address, bytes});
        item.read_regions.push_back({event->acc_address, bytes});
        item.write_regions.push_back({event->dst_address, bytes});
        break;
    case Instruction::Opcode::AUTOMORPHISM:
        item.domain = WorkDomain::SPU;
        item.read_regions.push_back({event->src0_address, bytes});
        item.write_regions.push_back({event->dst_address, bytes});
        break;
    case Instruction::Opcode::NTT_STAGE:
    case Instruction::Opcode::INTT_STAGE: {
        item.domain = WorkDomain::NTT;
        const uint32_t degree = std::max<uint32_t>(
            2,
            event->poly_degree == 0
                ? (event->element_count == 0 ? context_.poly_degree_n : event->element_count)
                : event->poly_degree);
        const uint64_t twiddle_bytes = static_cast<uint64_t>(degree / 2) * bytesPerElement();
        item.read_regions.push_back({event->src0_address, bytes});
        if (!nttu_twiddle_from_hbm_) {
            item.read_regions.push_back({event->twiddle_address, std::max<uint64_t>(bytesPerElement(), twiddle_bytes)});
        }
        item.write_regions.push_back({event->dst_address, bytes});
        break;
    }
    case Instruction::Opcode::BARRIER:
        item.domain = WorkDomain::BARRIER;
        break;
    case Instruction::Opcode::PCIE_H2D:
    case Instruction::Opcode::PCIE_D2H:
        item.domain = WorkDomain::BARRIER;
        break;
    }

    return item;
}

void ComputeUnit::serviceRunningWork(std::vector<WorkItem>& running)
{
    std::vector<WorkItem> still_running;
    still_running.reserve(running.size());

    for (WorkItem& item : running) {
        if (item.remaining_cycles > 0) {
            --item.remaining_cycles;
        }

        if (item.remaining_cycles > 0) {
            still_running.push_back(item);
            continue;
        }

        item.state = WorkState::Done;
        item.completion_cycle = current_cycle_;

        for (const RegionSpec& write_region : item.write_regions) {
            markValidRegion(write_region.start, write_region.bytes, current_cycle_, item.seq_no);
        }

        markRegionUsesDone(item.seq_no);

        item.event->status = static_cast<uint32_t>(EventStatus::Ok);
        item.event->event_type = static_cast<uint32_t>(EventType::Completion);
        item.event->completion = true;
        item.event->ready_cycle_hint = current_cycle_;
        item.event->src_component = static_cast<uint32_t>(ComponentType::ComputeUnit);
        item.event->dst_component = static_cast<uint32_t>(ComponentType::HBM);
        output_.verbose(
            CALL_INFO,
            1,
            0,
            "Complete: inst=%llu txn=%llu opcode=%s\n",
            static_cast<unsigned long long>(item.event->parent_instruction_id),
            static_cast<unsigned long long>(item.event->txn_id),
            Instruction::opcodeName(static_cast<Instruction::Opcode>(item.event->opcode)));
        hbm_link_->send(item.event);
    }

    running.swap(still_running);
}

void ComputeUnit::serviceDomainQueue(std::deque<WorkItem>& queue, std::vector<WorkItem>& running, uint32_t capacity)
{
    if (running.size() >= capacity || queue.empty()) {
        return;
    }

    // Avoid head-of-line deadlock: inspect each queued work-item once per cycle
    // and start any ready item until the domain capacity is filled.
    const std::size_t initial_entries = queue.size();
    std::size_t examined = 0;
    while (running.size() < capacity && examined < initial_entries && !queue.empty()) {
        WorkItem item = queue.front();
        queue.pop_front();

        if (tryStartWorkItem(item)) {
            running.push_back(item);
        } else {
            queue.push_back(item);
        }

        ++examined;
    }
}

bool ComputeUnit::tryStartWorkItem(WorkItem& item)
{
    if (!dependenciesSatisfied(item)) {
        item.state = WorkState::WaitDependency;
        return false;
    }

    item.banks_touched = collectTouchedBanks(item);
    if (!banksAvailableNow(item.banks_touched)) {
        item.state = WorkState::WaitResource;
        return false;
    }

    const uint64_t bank_hold_cycles = estimateBankHoldCycles(item);
    reserveBanks(item.banks_touched, bank_hold_cycles);

    item.remaining_cycles = std::max<uint64_t>(1, estimateWorkLatency(item));
    item.earliest_start_cycle = current_cycle_;
    item.state = WorkState::Running;
    output_.verbose(
        CALL_INFO,
        2,
        0,
        "Start: inst=%llu txn=%llu opcode=%s latency=%llu\n",
        static_cast<unsigned long long>(item.event->parent_instruction_id),
        static_cast<unsigned long long>(item.event->txn_id),
        Instruction::opcodeName(static_cast<Instruction::Opcode>(item.event->opcode)),
        static_cast<unsigned long long>(item.remaining_cycles));
    return true;
}

bool ComputeUnit::dependenciesSatisfied(const WorkItem& item) const
{
    if (hasOutstandingConflict(item)) {
        return false;
    }

    for (const RegionSpec& read_region : item.read_regions) {
        if (!regionExistsAndReady(read_region.start, read_region.bytes, current_cycle_)) {
            return false;
        }
    }

    return true;
}

bool ComputeUnit::regionExistsAndReady(uint64_t start, uint64_t bytes, uint64_t cycle) const
{
    const uint64_t end = start + bytes;
    for (const RegionState& region : valid_regions_) {
        const uint64_t region_end = region.start + region.bytes;
        if (!region.valid || region.ready_cycle > cycle) {
            continue;
        }
        if (start >= region.start && end <= region_end) {
            return true;
        }
    }

    return false;
}

bool ComputeUnit::hasOutstandingConflict(const WorkItem& item) const
{
    for (const RegionUse& use : inflight_region_uses_) {
        if (use.done || use.seq_no >= item.seq_no) {
            continue;
        }

        for (const RegionSpec& read_region : item.read_regions) {
            if (use.is_write && overlaps(use.start, use.bytes, read_region.start, read_region.bytes)) {
                return true;
            }
        }

        for (const RegionSpec& write_region : item.write_regions) {
            if (overlaps(use.start, use.bytes, write_region.start, write_region.bytes)) {
                return true;
            }
        }
    }

    return false;
}

void ComputeUnit::registerRegionUses(const WorkItem& item)
{
    for (const RegionSpec& read_region : item.read_regions) {
        inflight_region_uses_.push_back({item.seq_no, read_region.start, read_region.bytes, false, false});
    }

    for (const RegionSpec& write_region : item.write_regions) {
        inflight_region_uses_.push_back({item.seq_no, write_region.start, write_region.bytes, true, false});
    }
}

void ComputeUnit::markRegionUsesDone(uint64_t seq_no)
{
    for (RegionUse& use : inflight_region_uses_) {
        if (use.seq_no == seq_no) {
            use.done = true;
        }
    }

    inflight_region_uses_.erase(
        std::remove_if(
            inflight_region_uses_.begin(),
            inflight_region_uses_.end(),
            [](const RegionUse& use) {
                return use.done;
            }),
        inflight_region_uses_.end());
}

bool ComputeUnit::fitsScratchpad(uint64_t start, uint64_t bytes) const
{
    if (start > scratchpad_bytes_) {
        return false;
    }
    return bytes <= (scratchpad_bytes_ - start);
}

void ComputeUnit::markValidRegion(uint64_t start, uint64_t bytes, uint64_t ready_cycle, uint64_t writer_seq)
{
    valid_regions_.push_back({start, bytes, ready_cycle, writer_seq, true});
    std::sort(
        valid_regions_.begin(),
        valid_regions_.end(),
        [](const RegionState& lhs, const RegionState& rhs) {
            return lhs.start < rhs.start;
        });

    std::vector<RegionState> merged;
    for (const RegionState& region : valid_regions_) {
        if (merged.empty()) {
            merged.push_back(region);
            continue;
        }

        RegionState& last = merged.back();
        const uint64_t last_end = last.start + last.bytes;
        const uint64_t region_end = region.start + region.bytes;

        if (region.start > last_end) {
            merged.push_back(region);
            continue;
        }

        last.bytes = std::max<uint64_t>(last_end, region_end) - last.start;
        last.ready_cycle = std::max<uint64_t>(last.ready_cycle, region.ready_cycle);
        last.last_writer_seq = std::max<uint64_t>(last.last_writer_seq, region.last_writer_seq);
        last.valid = last.valid && region.valid;
    }

    valid_regions_.swap(merged);
}

std::vector<uint32_t> ComputeUnit::collectTouchedBanks(const WorkItem& item) const
{
    std::set<uint32_t> banks;
    const uint32_t stride = std::max<uint32_t>(1, bank_interleave_bytes_);

    const auto collect = [&](const RegionSpec& region) {
        if (region.bytes == 0) {
            return;
        }
        for (uint64_t offset = 0; offset < region.bytes; offset += stride) {
            banks.insert(mapAddressToBank(region.start + offset));
        }
    };

    for (const RegionSpec& read_region : item.read_regions) {
        collect(read_region);
    }
    for (const RegionSpec& write_region : item.write_regions) {
        collect(write_region);
    }

    return std::vector<uint32_t>(banks.begin(), banks.end());
}

uint32_t ComputeUnit::mapAddressToBank(uint64_t address) const
{
    const uint32_t banks = std::max<uint32_t>(1, scratchpad_banks_);
    const uint64_t index = address / std::max<uint32_t>(1, bank_interleave_bytes_);
    return static_cast<uint32_t>(index % banks);
}

uint64_t ComputeUnit::estimateBankHoldCycles(const WorkItem& item) const
{
    uint64_t total_bytes = 0;
    for (const RegionSpec& read_region : item.read_regions) {
        total_bytes += read_region.bytes;
    }
    for (const RegionSpec& write_region : item.write_regions) {
        total_bytes += write_region.bytes;
    }

    const uint64_t base_cycles = std::max<uint64_t>(
        1,
        ceilDivide(total_bytes, std::max<uint32_t>(1, scratchpad_bandwidth_bytes_per_cycle_)));

    const uint32_t read_streams = static_cast<uint32_t>(item.read_regions.size());
    const uint32_t write_streams = static_cast<uint32_t>(item.write_regions.size());

    const uint32_t read_factor = static_cast<uint32_t>(
        std::max<uint64_t>(1, ceilDivide(read_streams, std::max<uint32_t>(1, bank_read_ports_))));
    const uint32_t write_factor = static_cast<uint32_t>(
        std::max<uint64_t>(1, ceilDivide(write_streams, std::max<uint32_t>(1, bank_write_ports_))));

    uint64_t hold_cycles = base_cycles * std::max<uint32_t>(read_factor, write_factor);

    if (!item.banks_touched.empty()) {
        const uint64_t lane_pressure = std::max<uint64_t>(1, item.event->lane_count);
        const uint64_t bank_count = item.banks_touched.size();
        if (bank_count < lane_pressure) {
            const uint64_t conflict_factor = ceilDivide(lane_pressure, bank_count);
            hold_cycles += conflict_factor * std::max<uint32_t>(1, bank_conflict_penalty_cycles_);
        }
    }

    return std::max<uint64_t>(1, hold_cycles);
}

bool ComputeUnit::banksAvailableNow(const std::vector<uint32_t>& banks) const
{
    for (uint32_t bank : banks) {
        if (bank_busy_until_[bank] > current_cycle_) {
            return false;
        }
    }
    return true;
}

void ComputeUnit::reserveBanks(const std::vector<uint32_t>& banks, uint64_t hold_cycles)
{
    const uint64_t busy_until = current_cycle_ + std::max<uint64_t>(1, hold_cycles);
    for (uint32_t bank : banks) {
        bank_busy_until_[bank] = busy_until;
    }
}

uint64_t ComputeUnit::estimateWorkLatency(const WorkItem& item) const
{
    const Instruction::Opcode opcode = static_cast<Instruction::Opcode>(item.event->opcode);

    const uint32_t lane_count = std::max<uint32_t>(
        1,
        std::min<uint32_t>(item.event->lane_count == 0 ? static_cast<uint32_t>(lanes_.size()) : item.event->lane_count,
                           static_cast<uint32_t>(lanes_.size())));
    const uint64_t elements = std::max<uint32_t>(1, item.event->element_count);
    const uint64_t bytes = bytesForEvent(*item.event);

    if (opcode == Instruction::Opcode::HBM_LOAD || opcode == Instruction::Opcode::HBM_STORE) {
        return std::max<uint64_t>(
            1,
            ceilDivide(bytes, std::max<uint32_t>(1, scratchpad_bandwidth_bytes_per_cycle_)) + 2);
    }

    if (opcode == Instruction::Opcode::AUTOMORPHISM) {
        const uint64_t tiles = ceilDivide(elements, lane_count);
        const uint64_t passes = ceilDivide(tiles, std::max<uint32_t>(1, spu_.count));
        const uint64_t benes = benesStages(lane_count) * std::max<uint32_t>(1, spu_.switch_latency_cycles);
        return std::max<uint64_t>(
            1,
            passes * std::max<uint32_t>(1, spu_.automorphism_latency_cycles) + benes);
    }

    if (opcode == Instruction::Opcode::NTT_STAGE || opcode == Instruction::Opcode::INTT_STAGE) {
        const uint32_t degree = std::max<uint32_t>(
            2,
            item.event->poly_degree == 0
                ? (item.event->element_count == 0 ? context_.poly_degree_n : item.event->element_count)
                : item.event->poly_degree);
        const uint64_t butterflies = std::max<uint64_t>(1, degree / 2);
        const uint64_t parallel = std::max<uint64_t>(1, static_cast<uint64_t>(lane_count) * std::max<uint32_t>(1, nttu_.count));
        const uint64_t waves = ceilDivide(butterflies, parallel);
        const uint64_t core = waves * std::max<uint32_t>(1, nttu_.butterfly_latency_cycles);
        return std::max<uint64_t>(
            1,
            core +
                std::max<uint32_t>(1, nttu_.twiddle_penalty_cycles) +
                std::max<uint32_t>(1, nttu_.shuffle_penalty_cycles) +
                static_cast<uint64_t>(std::max<uint32_t>(1, bank_conflict_penalty_cycles_)));
    }

    uint64_t total_units = 0;
    uint32_t wave_latency = 1;
    for (uint32_t lane = 0; lane < lane_count; ++lane) {
        const LaneDescriptor& descriptor = lanes_[lane];
        if (opcode == Instruction::Opcode::MOD_ADD || opcode == Instruction::Opcode::MOD_SUB) {
            total_units += std::max<uint32_t>(1, descriptor.mod_adder_count);
            wave_latency = std::max<uint32_t>(wave_latency, descriptor.mod_adder_latency_cycles);
        } else if (opcode == Instruction::Opcode::MOD_MUL) {
            total_units += std::max<uint32_t>(1, descriptor.mod_multiplier_count);
            wave_latency = std::max<uint32_t>(wave_latency, descriptor.mod_multiplier_latency_cycles);
        } else if (opcode == Instruction::Opcode::MAC) {
            total_units += std::max<uint32_t>(1, descriptor.mod_multiplier_count);
            wave_latency = std::max<uint32_t>(
                wave_latency,
                descriptor.mod_multiplier_latency_cycles + descriptor.mod_adder_latency_cycles);
        }
    }

    const uint64_t waves = ceilDivide(elements, std::max<uint64_t>(1, total_units));
    return std::max<uint64_t>(1, waves * std::max<uint32_t>(1, wave_latency));
}

uint64_t ComputeUnit::bytesPerElement() const
{
    return std::max<uint64_t>(1, context_.elementBytes());
}

uint64_t ComputeUnit::bytesForEvent(const AcceleratorEvent& event) const
{
    if (event.bytes > 0) {
        return event.bytes;
    }
    return static_cast<uint64_t>(std::max<uint32_t>(1, event.element_count)) * bytesPerElement();
}

bool ComputeUnit::allDomainQueuesEmpty() const
{
    return dma_queue_.empty() && alu_queue_.empty() && spu_queue_.empty() && ntt_queue_.empty();
}

} // namespace Tutorial

#include <sst/core/sst_config.h>

#include "hbm.h"

#include <algorithm>

#include "accel_event.h"

namespace {

uint64_t ceilDivide(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0) {
        return 0;
    }
    return (numerator + denominator - 1) / denominator;
}

uint64_t normalizeBytes(const Tutorial::AcceleratorEvent& event)
{
    if (event.bytes > 0) {
        return event.bytes;
    }
    const uint32_t elements = std::max<uint32_t>(1, event.element_count);
    return static_cast<uint64_t>(elements) * sizeof(uint32_t);
}

} // namespace

namespace Tutorial {

HBM::HBM(SST::ComponentId_t id, SST::Params& params) :
    SST::Component(id),
    fabric_link_(nullptr),
    compute_link_(nullptr),
    capacity_bytes_(params.find<uint64_t>("capacity_bytes", 17179869184ULL)),
    num_channels_(params.find<uint32_t>("num_channels", 8)),
    pseudo_channels_per_channel_(params.find<uint32_t>("pseudo_channels_per_channel", 2)),
    burst_bytes_(params.find<uint32_t>("burst_bytes", 64)),
    base_access_cycles_(params.find<uint32_t>("base_access_cycles", 24)),
    burst_cycles_(params.find<uint32_t>("burst_cycles", 2)),
    compute_dispatch_cycles_(params.find<uint32_t>("compute_dispatch_cycles", 2)),
    network_return_cycles_(params.find<uint32_t>("network_return_cycles", 4)),
    nttu_twiddle_from_hbm_(false),
    context_(loadCryptoContext(params, 8192, 32, 1)),
    current_cycle_(0)
{
    const uint32_t verbose = params.find<uint32_t>("verbose", 0);
    output_.init("[HBM] ", verbose, 0, SST::Output::STDOUT);

    const SST::Params nttu_params = params.get_scoped_params("nttu");
    nttu_twiddle_from_hbm_ = (nttu_params.find<uint32_t>("twiddle_from_hbm", 0) != 0);

    fabric_link_ = configureLink(
        "fabric",
        new SST::Event::Handler2<HBM, &HBM::handleFabricEvent>(this));
    sst_assert(fabric_link_ != nullptr, CALL_INFO, -1, "HBM requires the 'fabric' port to be connected\n");

    compute_link_ = configureLink(
        "compute",
        new SST::Event::Handler2<HBM, &HBM::handleComputeEvent>(this));
    sst_assert(compute_link_ != nullptr, CALL_INFO, -1, "HBM requires the 'compute' port to be connected\n");

    const uint32_t total_channels = std::max<uint32_t>(1, num_channels_ * pseudo_channels_per_channel_);
    channel_queues_.resize(total_channels);
    channel_active_valid_.resize(total_channels, false);
    channel_active_.resize(total_channels);

    const std::string clock = params.find<std::string>("clock", "1.2GHz");
    registerClock(clock, new SST::Clock::Handler2<HBM, &HBM::tick>(this));
}

void HBM::handleFabricEvent(SST::Event* event)
{
    AcceleratorEvent* request = dynamic_cast<AcceleratorEvent*>(event);
    sst_assert(request != nullptr, CALL_INFO, -1, "HBM received unexpected event type on 'fabric'\n");

    const Instruction::Opcode opcode = static_cast<Instruction::Opcode>(request->opcode);
    const uint64_t bytes = normalizeBytes(*request);
    output_.verbose(
        CALL_INFO,
        1,
        0,
        "Recv fabric: inst=%llu txn=%llu opcode=%s bytes=%llu\n",
        static_cast<unsigned long long>(request->parent_instruction_id),
        static_cast<unsigned long long>(request->txn_id),
        Instruction::opcodeName(opcode),
        static_cast<unsigned long long>(bytes));

    switch (opcode) {
    case Instruction::Opcode::PCIE_H2D:
        if (!withinCapacity(request->hbm_address, bytes)) {
            request->status = static_cast<uint32_t>(EventStatus::InvalidAddress);
            request->completion = true;
            enqueueTimedDispatch(request, network_return_cycles_, CompletionTarget::Fabric);
            return;
        }
        enqueueMemoryRequest(
            request,
            request->hbm_address,
            bytes,
            false,
            true,
            false,
            true);
        return;

    case Instruction::Opcode::PCIE_D2H:
        if (!withinCapacity(request->hbm_address, bytes)) {
            request->status = static_cast<uint32_t>(EventStatus::InvalidAddress);
            request->completion = true;
            enqueueTimedDispatch(request, network_return_cycles_, CompletionTarget::Fabric);
            return;
        }
        enqueueMemoryRequest(
            request,
            request->hbm_address,
            bytes,
            true,
            false,
            false,
            true);
        return;

    case Instruction::Opcode::HBM_LOAD:
        if (!withinCapacity(request->hbm_address, bytes)) {
            request->status = static_cast<uint32_t>(EventStatus::InvalidAddress);
            request->completion = true;
            enqueueTimedDispatch(request, network_return_cycles_, CompletionTarget::Fabric);
            return;
        }
        enqueueMemoryRequest(
            request,
            request->hbm_address,
            bytes,
            true,
            false,
            true,
            false);
        return;

    case Instruction::Opcode::HBM_STORE:
    case Instruction::Opcode::MOD_ADD:
    case Instruction::Opcode::MOD_SUB:
    case Instruction::Opcode::MOD_MUL:
    case Instruction::Opcode::MAC:
    case Instruction::Opcode::AUTOMORPHISM:
    case Instruction::Opcode::BARRIER:
        request->event_type = static_cast<uint32_t>(EventType::Command);
        request->src_component = static_cast<uint32_t>(ComponentType::HBM);
        request->dst_component = static_cast<uint32_t>(ComponentType::ComputeUnit);
        request->completion = false;
        enqueueTimedDispatch(request, compute_dispatch_cycles_, CompletionTarget::Compute);
        return;

    case Instruction::Opcode::NTT_STAGE:
    case Instruction::Opcode::INTT_STAGE:
        if (nttu_twiddle_from_hbm_) {
            const uint64_t twiddle_bytes = twiddleBytesForEvent(*request);
            if (!withinCapacity(request->twiddle_address, twiddle_bytes)) {
                request->status = static_cast<uint32_t>(EventStatus::InvalidAddress);
                request->completion = true;
                enqueueTimedDispatch(request, network_return_cycles_, CompletionTarget::Fabric);
                return;
            }

            enqueueMemoryRequest(
                request,
                request->twiddle_address,
                twiddle_bytes,
                true,
                false,
                true,
                false,
                false);
            return;
        }

        request->event_type = static_cast<uint32_t>(EventType::Command);
        request->src_component = static_cast<uint32_t>(ComponentType::HBM);
        request->dst_component = static_cast<uint32_t>(ComponentType::ComputeUnit);
        request->completion = false;
        enqueueTimedDispatch(request, compute_dispatch_cycles_, CompletionTarget::Compute);
        return;
    }
}

void HBM::handleComputeEvent(SST::Event* event)
{
    AcceleratorEvent* response = dynamic_cast<AcceleratorEvent*>(event);
    sst_assert(response != nullptr, CALL_INFO, -1, "HBM received unexpected event type on 'compute'\n");

    const Instruction::Opcode opcode = static_cast<Instruction::Opcode>(response->opcode);
    const uint64_t bytes = normalizeBytes(*response);
    output_.verbose(
        CALL_INFO,
        1,
        0,
        "Recv compute: inst=%llu txn=%llu opcode=%s bytes=%llu\n",
        static_cast<unsigned long long>(response->parent_instruction_id),
        static_cast<unsigned long long>(response->txn_id),
        Instruction::opcodeName(opcode),
        static_cast<unsigned long long>(bytes));

    if (opcode == Instruction::Opcode::HBM_STORE) {
        if (!withinCapacity(response->hbm_address, bytes)) {
            response->status = static_cast<uint32_t>(EventStatus::InvalidAddress);
            response->completion = true;
            enqueueTimedDispatch(response, network_return_cycles_, CompletionTarget::Fabric);
            return;
        }

        enqueueMemoryRequest(
            response,
            response->hbm_address,
            bytes,
            false,
            true,
            false,
            true);
        return;
    }

    response->event_type = static_cast<uint32_t>(EventType::Completion);
    response->src_component = static_cast<uint32_t>(ComponentType::HBM);
    response->dst_component = static_cast<uint32_t>(ComponentType::PCIe);
    response->completion = true;
    enqueueTimedDispatch(response, network_return_cycles_, CompletionTarget::Fabric);
}

bool HBM::tick(SST::Cycle_t cycle)
{
    (void)cycle;
    ++current_cycle_;

    serviceMemoryChannels(current_cycle_);
    serviceDispatchQueue();

    return false;
}

void HBM::enqueueMemoryRequest(
    AcceleratorEvent* event,
    uint64_t address,
    uint64_t bytes,
    bool requires_ready_data,
    bool mark_valid_on_complete,
    bool forward_to_compute,
    bool respond_to_fabric,
    bool rewrite_event_bytes)
{
    if (rewrite_event_bytes) {
        event->bytes = bytes;
    }
    event->event_type = static_cast<uint32_t>(EventType::MemRequest);
    event->src_component = static_cast<uint32_t>(ComponentType::HBM);
    event->dst_component = static_cast<uint32_t>(ComponentType::HBM);

    const uint32_t channel = mapToChannel(address);
    channel_queues_[channel].push_back(
        {
            event,
            address,
            bytes,
            estimateMemoryCycles(bytes),
            requires_ready_data,
            mark_valid_on_complete,
            forward_to_compute,
            respond_to_fabric,
        });
    output_.verbose(
        CALL_INFO,
        2,
        0,
        "Enqueue channel req: ch=%u inst=%llu txn=%llu opcode=%s bytes=%llu qdepth=%zu\n",
        channel,
        static_cast<unsigned long long>(event->parent_instruction_id),
        static_cast<unsigned long long>(event->txn_id),
        Instruction::opcodeName(static_cast<Instruction::Opcode>(event->opcode)),
        static_cast<unsigned long long>(bytes),
        channel_queues_[channel].size());
}

void HBM::enqueueTimedDispatch(AcceleratorEvent* event, uint64_t cycles, CompletionTarget target)
{
    dispatch_queue_.push_back({event, std::max<uint64_t>(1, cycles), target});
}

void HBM::serviceMemoryChannels(uint64_t now_cycle)
{
    const std::size_t channels = channel_queues_.size();
    for (std::size_t channel = 0; channel < channels; ++channel) {
        if (!channel_active_valid_[channel] && !channel_queues_[channel].empty()) {
            channel_active_[channel] = channel_queues_[channel].front();
            channel_queues_[channel].pop_front();
            channel_active_valid_[channel] = true;
        }

        if (!channel_active_valid_[channel]) {
            continue;
        }

        ChannelRequest& active = channel_active_[channel];
        if (active.requires_ready_data && !hasReadyRegion(active.address, active.bytes, now_cycle)) {
            output_.verbose(
                CALL_INFO,
                3,
                0,
                "Channel stall(wait data): ch=%zu inst=%llu txn=%llu addr=0x%llx bytes=%llu\n",
                channel,
                static_cast<unsigned long long>(active.event->parent_instruction_id),
                static_cast<unsigned long long>(active.event->txn_id),
                static_cast<unsigned long long>(active.address),
                static_cast<unsigned long long>(active.bytes));
            channel_queues_[channel].push_back(active);
            channel_active_valid_[channel] = false;
            continue;
        }

        if (active.remaining_cycles > 0) {
            --active.remaining_cycles;
        }

        if (active.remaining_cycles != 0) {
            continue;
        }

        if (active.mark_valid_on_complete) {
            markValidRegion(active.address, active.bytes, now_cycle);
            output_.verbose(
                CALL_INFO,
                2,
                0,
                "Mark HBM valid: addr=0x%llx bytes=%llu ready=%llu\n",
                static_cast<unsigned long long>(active.address),
                static_cast<unsigned long long>(active.bytes),
                static_cast<unsigned long long>(now_cycle));
        }

        if (active.forward_to_compute) {
            active.event->event_type = static_cast<uint32_t>(EventType::Command);
            active.event->src_component = static_cast<uint32_t>(ComponentType::HBM);
            active.event->dst_component = static_cast<uint32_t>(ComponentType::ComputeUnit);
            active.event->completion = false;
            output_.verbose(
                CALL_INFO,
                2,
                0,
                "Forward to Compute: inst=%llu txn=%llu opcode=%s\n",
                static_cast<unsigned long long>(active.event->parent_instruction_id),
                static_cast<unsigned long long>(active.event->txn_id),
                Instruction::opcodeName(static_cast<Instruction::Opcode>(active.event->opcode)));
            enqueueTimedDispatch(active.event, compute_dispatch_cycles_, CompletionTarget::Compute);
        }

        if (active.respond_to_fabric) {
            active.event->event_type = static_cast<uint32_t>(EventType::Completion);
            active.event->src_component = static_cast<uint32_t>(ComponentType::HBM);
            active.event->dst_component = static_cast<uint32_t>(ComponentType::PCIe);
            active.event->completion = true;
            output_.verbose(
                CALL_INFO,
                2,
                0,
                "Respond to fabric: inst=%llu txn=%llu opcode=%s status=%u\n",
                static_cast<unsigned long long>(active.event->parent_instruction_id),
                static_cast<unsigned long long>(active.event->txn_id),
                Instruction::opcodeName(static_cast<Instruction::Opcode>(active.event->opcode)),
                active.event->status);
            enqueueTimedDispatch(active.event, network_return_cycles_, CompletionTarget::Fabric);
        }

        channel_active_valid_[channel] = false;
    }
}

void HBM::serviceDispatchQueue()
{
    const std::size_t entries = dispatch_queue_.size();
    for (std::size_t i = 0; i < entries; ++i) {
        TimedDispatch dispatch = dispatch_queue_.front();
        dispatch_queue_.pop_front();

        if (dispatch.remaining_cycles > 0) {
            --dispatch.remaining_cycles;
        }

        if (dispatch.remaining_cycles == 0) {
            if (dispatch.target == CompletionTarget::Compute) {
                output_.verbose(
                    CALL_INFO,
                    3,
                    0,
                    "Timed dispatch -> Compute: inst=%llu txn=%llu\n",
                    static_cast<unsigned long long>(dispatch.event->parent_instruction_id),
                    static_cast<unsigned long long>(dispatch.event->txn_id));
                compute_link_->send(dispatch.event);
            } else {
                output_.verbose(
                    CALL_INFO,
                    3,
                    0,
                    "Timed dispatch -> Fabric: inst=%llu txn=%llu\n",
                    static_cast<unsigned long long>(dispatch.event->parent_instruction_id),
                    static_cast<unsigned long long>(dispatch.event->txn_id));
                fabric_link_->send(dispatch.event);
            }
        } else {
            dispatch_queue_.push_back(dispatch);
        }
    }
}

uint64_t HBM::estimateMemoryCycles(uint64_t bytes) const
{
    const uint64_t bursts = std::max<uint64_t>(1, ceilDivide(bytes, std::max<uint32_t>(1, burst_bytes_)));
    return static_cast<uint64_t>(std::max<uint32_t>(1, base_access_cycles_)) +
           bursts * static_cast<uint64_t>(std::max<uint32_t>(1, burst_cycles_));
}

uint64_t HBM::twiddleBytesForEvent(const AcceleratorEvent& event) const
{
    const uint32_t degree = std::max<uint32_t>(
        2,
        event.poly_degree == 0
            ? (event.element_count == 0 ? context_.poly_degree_n : event.element_count)
            : event.poly_degree);
    const uint64_t twiddle_elements = std::max<uint64_t>(1, static_cast<uint64_t>(degree) / 2);
    return twiddle_elements * std::max<uint64_t>(1, context_.elementBytes());
}

uint32_t HBM::mapToChannel(uint64_t address) const
{
    const uint32_t total_channels = static_cast<uint32_t>(channel_queues_.size());
    const uint64_t bucket = address / std::max<uint32_t>(1, burst_bytes_);
    return static_cast<uint32_t>(bucket % std::max<uint32_t>(1, total_channels));
}

bool HBM::withinCapacity(uint64_t address, uint64_t bytes) const
{
    if (address > capacity_bytes_) {
        return false;
    }
    return bytes <= (capacity_bytes_ - address);
}

bool HBM::hasReadyRegion(uint64_t start, uint64_t bytes, uint64_t cycle) const
{
    const uint64_t end = start + bytes;
    for (const RegionState& region : valid_regions_) {
        if (!region.valid || region.ready_cycle > cycle) {
            continue;
        }
        const uint64_t region_end = region.start + region.bytes;
        if (start >= region.start && end <= region_end) {
            return true;
        }
    }
    return false;
}

void HBM::markValidRegion(uint64_t start, uint64_t bytes, uint64_t ready_cycle)
{
    valid_regions_.push_back({start, bytes, ready_cycle, true});
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
        last.valid = last.valid && region.valid;
    }

    valid_regions_.swap(merged);
}

} // namespace Tutorial

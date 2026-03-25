#ifndef _SST_TUTORIAL_COMPUTE_UNIT_H
#define _SST_TUTORIAL_COMPUTE_UNIT_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include <sst/core/component.h>
#include <sst/core/output.h>

#include "context.h"

namespace Tutorial {

class AcceleratorEvent;

class ComputeUnit : public SST::Component {
public:
    SST_ELI_REGISTER_COMPONENT(
        ComputeUnit,
        "tutorial",
        "ComputeUnit",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Compute unit with DMA/ALU/SPU/NTTU domains and banked scratchpad timing",
        COMPONENT_CATEGORY_UNCATEGORIZED)

    SST_ELI_DOCUMENT_PARAMS(
        {"clock", "Clock frequency used for compute progress", "1.5GHz"},
        {"num_lanes", "Number of SIMD lanes in the compute unit", "256"},
        {"lane_width_bits", "Width of each lane", "32"},
        {"scratchpad_bytes", "Capacity of the on-chip scratchpad memory", "1048576"},
        {"scratchpad_bandwidth_bytes_per_cycle", "Aggregate scratchpad bandwidth in bytes per cycle", "128"},
        {"scratchpad_banks", "Number of scratchpad banks", "32"},
        {"bank_interleave_bytes", "Bank interleave granularity", "32"},
        {"bank_read_ports", "Read ports per bank", "1"},
        {"bank_write_ports", "Write ports per bank", "1"},
        {"bank_conflict_penalty_cycles", "Penalty when bank pressure exceeds ports", "1"},
        {"dma_engines", "Number of HBM<->SPM DMA engines", "2"},
        {"alu_engines", "Number of ALU issue slots", "2"},
        {"spu.count", "Number of SPU automorphism engines", "1"},
        {"spu.automorphism_latency", "Latency of one SPU automorphism pass", "12cycle"},
        {"spu.switch_latency", "Latency per Benes stage", "1cycle"},
        {"nttu.count", "Number of NTT units", "1"},
        {"nttu.butterfly_latency", "Base butterfly core latency", "4cycle"},
        {"nttu.twiddle_penalty", "Twiddle fetch penalty per stage", "2cycle"},
        {"nttu.shuffle_penalty", "Shuffle penalty per stage", "2cycle"},
        {"nttu.twiddle_from_hbm", "If 1, stream NTT twiddle coefficients from HBM instead of SPM", "0"},
        {"laneXXX.mod_multiplier.count", "Number of modular multipliers in one lane scope", "1"},
        {"laneXXX.mod_multiplier.latency", "Modular multiplier latency", "2cycle"},
        {"laneXXX.mod_adder.count", "Number of modular adders in one lane scope", "2"},
        {"laneXXX.mod_adder.latency", "Modular adder latency", "1cycle"},
        {"context.n", "Crypto polynomial degree N", "8192"},
        {"context.data_width_bits", "Ciphertext limb width in bits", "32"},
        {"context.limbs", "Number of limbs per element", "1"},
        {"verbose", "Verbose log level (0 disables logs)", "0"})

    SST_ELI_DOCUMENT_PORTS(
        {"hbm", "Bidirectional port connected to HBM", {"Tutorial::AcceleratorEvent"}})

    ComputeUnit(SST::ComponentId_t id, SST::Params& params);
    ~ComputeUnit() override = default;

private:
    struct RegionSpec {
        uint64_t start;
        uint64_t bytes;
    };

    struct RegionState {
        uint64_t start;
        uint64_t bytes;
        uint64_t ready_cycle;
        uint64_t last_writer_seq;
        bool valid;
    };

    struct RegionUse {
        uint64_t seq_no;
        uint64_t start;
        uint64_t bytes;
        bool is_write;
        bool done;
    };

    struct LaneDescriptor {
        uint32_t mod_multiplier_count;
        uint32_t mod_multiplier_latency_cycles;
        uint32_t mod_adder_count;
        uint32_t mod_adder_latency_cycles;
    };

    struct SPUDescriptor {
        uint32_t count;
        uint32_t automorphism_latency_cycles;
        uint32_t switch_latency_cycles;
    };

    struct NTTUDescriptor {
        uint32_t count;
        uint32_t butterfly_latency_cycles;
        uint32_t twiddle_penalty_cycles;
        uint32_t shuffle_penalty_cycles;
    };

    enum class WorkDomain : uint32_t {
        DMA = 0,
        ALU = 1,
        SPU = 2,
        NTT = 3,
        BARRIER = 4,
    };

    enum class WorkState : uint32_t {
        WaitDependency = 0,
        WaitResource = 1,
        Running = 2,
        Done = 3,
    };

    struct WorkItem {
        AcceleratorEvent* event;
        WorkDomain domain;
        WorkState state;
        uint64_t seq_no;
        uint64_t remaining_cycles;
        uint64_t earliest_start_cycle;
        uint64_t completion_cycle;
        std::vector<RegionSpec> read_regions;
        std::vector<RegionSpec> write_regions;
        std::vector<uint32_t> banks_touched;
    };

    void handleHbmEvent(SST::Event* event);
    bool tick(SST::Cycle_t cycle);

    bool enqueueWork(AcceleratorEvent* event);
    WorkItem buildWorkItem(AcceleratorEvent* event) const;

    void serviceRunningWork(std::vector<WorkItem>& running);
    void serviceDomainQueue(std::deque<WorkItem>& queue, std::vector<WorkItem>& running, uint32_t capacity);
    bool tryStartWorkItem(WorkItem& item);

    bool dependenciesSatisfied(const WorkItem& item) const;
    bool regionExistsAndReady(uint64_t start, uint64_t bytes, uint64_t cycle) const;
    bool hasOutstandingConflict(const WorkItem& item) const;
    void registerRegionUses(const WorkItem& item);
    void markRegionUsesDone(uint64_t seq_no);

    bool fitsScratchpad(uint64_t start, uint64_t bytes) const;
    void markValidRegion(uint64_t start, uint64_t bytes, uint64_t ready_cycle, uint64_t writer_seq);

    std::vector<uint32_t> collectTouchedBanks(const WorkItem& item) const;
    uint32_t mapAddressToBank(uint64_t address) const;
    uint64_t estimateBankHoldCycles(const WorkItem& item) const;
    bool banksAvailableNow(const std::vector<uint32_t>& banks) const;
    void reserveBanks(const std::vector<uint32_t>& banks, uint64_t hold_cycles);

    uint64_t estimateWorkLatency(const WorkItem& item) const;
    uint64_t bytesPerElement() const;
    uint64_t bytesForEvent(const AcceleratorEvent& event) const;

    bool allDomainQueuesEmpty() const;

    SST::Link* hbm_link_;

    std::vector<LaneDescriptor> lanes_;
    SPUDescriptor spu_;
    NTTUDescriptor nttu_;
    bool nttu_twiddle_from_hbm_;

    std::deque<WorkItem> dma_queue_;
    std::deque<WorkItem> alu_queue_;
    std::deque<WorkItem> spu_queue_;
    std::deque<WorkItem> ntt_queue_;
    std::deque<WorkItem> barrier_queue_;

    std::vector<WorkItem> dma_running_;
    std::vector<WorkItem> alu_running_;
    std::vector<WorkItem> spu_running_;
    std::vector<WorkItem> ntt_running_;

    std::vector<RegionState> valid_regions_;
    std::vector<RegionUse> inflight_region_uses_;

    CryptoContext context_;
    uint32_t lane_width_bits_;
    uint64_t scratchpad_bytes_;
    uint32_t scratchpad_bandwidth_bytes_per_cycle_;

    uint32_t scratchpad_banks_;
    uint32_t bank_interleave_bytes_;
    uint32_t bank_read_ports_;
    uint32_t bank_write_ports_;
    uint32_t bank_conflict_penalty_cycles_;

    uint32_t dma_engines_;
    uint32_t alu_engines_;

    std::vector<uint64_t> bank_busy_until_;

    uint64_t current_cycle_;
    SST::Output output_;
};

} // namespace Tutorial

#endif

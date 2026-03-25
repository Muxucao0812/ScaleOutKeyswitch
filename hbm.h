#ifndef _SST_TUTORIAL_HBM_H
#define _SST_TUTORIAL_HBM_H

#include <cstdint>
#include <deque>
#include <vector>

#include <sst/core/component.h>
#include <sst/core/output.h>

#include "context.h"

namespace Tutorial {

class AcceleratorEvent;

class HBM : public SST::Component {
public:
    SST_ELI_REGISTER_COMPONENT(
        HBM,
        "tutorial",
        "HBM",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "HBM model with channel-aware scheduling and valid-region tracking",
        COMPONENT_CATEGORY_UNCATEGORIZED)

    SST_ELI_DOCUMENT_PARAMS(
        {"clock", "Clock frequency used for HBM internal progress", "1.2GHz"},
        {"capacity_bytes", "Modeled capacity of the HBM stack", "17179869184"},
        {"num_channels", "Number of physical HBM channels", "8"},
        {"pseudo_channels_per_channel", "Pseudo channels per physical channel", "2"},
        {"burst_bytes", "HBM burst size in bytes", "64"},
        {"base_access_cycles", "Base cycles for one memory request", "24"},
        {"burst_cycles", "Additional cycles per burst", "2"},
        {"compute_dispatch_cycles", "Cycles to dispatch command from HBM to ComputeUnit", "2"},
        {"network_return_cycles", "Cycles to return completion from HBM to PCIe", "4"},
        {"nttu.twiddle_from_hbm", "If 1, stage twiddle fetch in HBM before dispatching NTT/INTT", "0"},
        {"context.n", "Crypto polynomial degree N", "8192"},
        {"context.data_width_bits", "Ciphertext limb width in bits", "32"},
        {"context.limbs", "Number of limbs per element", "1"},
        {"verbose", "Verbose log level (0 disables logs)", "0"})

    SST_ELI_DOCUMENT_PORTS(
        {"fabric", "Bidirectional PCIe-facing port", {"Tutorial::AcceleratorEvent"}},
        {"compute", "Bidirectional ComputeUnit-facing port", {"Tutorial::AcceleratorEvent"}})

    HBM(SST::ComponentId_t id, SST::Params& params);
    ~HBM() override = default;

private:
    struct RegionState {
        uint64_t start;
        uint64_t bytes;
        uint64_t ready_cycle;
        bool valid;
    };

    enum class CompletionTarget {
        Fabric,
        Compute,
    };

    struct ChannelRequest {
        AcceleratorEvent* event;
        uint64_t address;
        uint64_t bytes;
        uint64_t remaining_cycles;
        bool requires_ready_data;
        bool mark_valid_on_complete;
        bool forward_to_compute;
        bool respond_to_fabric;
    };

    struct TimedDispatch {
        AcceleratorEvent* event;
        uint64_t remaining_cycles;
        CompletionTarget target;
    };

    void handleFabricEvent(SST::Event* event);
    void handleComputeEvent(SST::Event* event);
    bool tick(SST::Cycle_t cycle);

    void enqueueMemoryRequest(
        AcceleratorEvent* event,
        uint64_t address,
        uint64_t bytes,
        bool requires_ready_data,
        bool mark_valid_on_complete,
        bool forward_to_compute,
        bool respond_to_fabric,
        bool rewrite_event_bytes = true);

    void enqueueTimedDispatch(AcceleratorEvent* event, uint64_t cycles, CompletionTarget target);
    void serviceMemoryChannels(uint64_t now_cycle);
    void serviceDispatchQueue();

    uint64_t estimateMemoryCycles(uint64_t bytes) const;
    uint64_t twiddleBytesForEvent(const AcceleratorEvent& event) const;
    uint32_t mapToChannel(uint64_t address) const;
    bool withinCapacity(uint64_t address, uint64_t bytes) const;

    bool hasReadyRegion(uint64_t start, uint64_t bytes, uint64_t cycle) const;
    void markValidRegion(uint64_t start, uint64_t bytes, uint64_t ready_cycle);

    SST::Link* fabric_link_;
    SST::Link* compute_link_;

    uint64_t capacity_bytes_;
    uint32_t num_channels_;
    uint32_t pseudo_channels_per_channel_;
    uint32_t pseudo_channel_bytes_;
    uint32_t burst_bytes_;
    uint32_t base_access_cycles_;
    uint32_t burst_cycles_;
    uint32_t compute_dispatch_cycles_;
    uint32_t network_return_cycles_;
    bool nttu_twiddle_from_hbm_;
    CryptoContext context_;


    std::vector<std::deque<ChannelRequest>> channel_queues_;
    std::vector<bool> channel_active_valid_;
    std::vector<ChannelRequest> channel_active_;

    std::deque<TimedDispatch> dispatch_queue_;
    std::vector<RegionState> valid_regions_;
    uint64_t current_cycle_;
    SST::Output output_;
};

} // namespace Tutorial

#endif

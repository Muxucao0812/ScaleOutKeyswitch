#ifndef _SST_TUTORIAL_PCIE_H
#define _SST_TUTORIAL_PCIE_H

#include <cstdint>
#include <deque>

#include <sst/core/component.h>
#include <sst/core/output.h>

namespace Tutorial {

class AcceleratorEvent;

class PCIe : public SST::Component {
public:
    SST_ELI_REGISTER_COMPONENT(
        PCIe,
        "tutorial",
        "PCIe",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "PCIe model with control doorbells and host-device DMA timing",
        COMPONENT_CATEGORY_UNCATEGORIZED)

    SST_ELI_DOCUMENT_PARAMS(
        {"clock", "Clock frequency for PCIe timing progression", "1.0GHz"},
        {"doorbell_setup_cycles", "Cycles to dispatch a non-data command to HBM", "4"},
        {"dma_setup_cycles", "Cycles to configure one H2D/D2H DMA request", "12"},
        {"protocol_overhead_cycles", "Additional protocol overhead per DMA request", "4"},
        {"effective_bytes_per_cycle", "Effective payload bandwidth in bytes per cycle", "64"},
        {"completion_return_cycles", "Cycles to return completion from PCIe to CPU", "2"},
        {"verbose", "Verbose log level (0 disables logs)", "0"})

    SST_ELI_DOCUMENT_PORTS(
        {"cpu_ctrl", "Bidirectional control path to CPU", {"Tutorial::AcceleratorEvent"}},
        {"fabric", "Bidirectional path to HBM fabric", {"Tutorial::AcceleratorEvent"}})

    PCIe(SST::ComponentId_t id, SST::Params& params);
    ~PCIe() override = default;

private:
    struct QueueEntry {
        AcceleratorEvent* event;
        uint64_t remaining_cycles;
    };

    void handleCpuEvent(SST::Event* event);
    void handleFabricEvent(SST::Event* event);
    bool tick(SST::Cycle_t cycle);

    void tryLaunchCommand();
    void tryLaunchDma();
    void tryLaunchCompletion();

    uint64_t estimateDmaCycles(uint64_t bytes) const;

    SST::Link* cpu_ctrl_link_;
    SST::Link* fabric_link_;

    std::deque<QueueEntry> command_waiting_queue_;
    std::deque<QueueEntry> dma_waiting_queue_;
    std::deque<QueueEntry> completion_waiting_queue_;

    bool command_active_valid_;
    QueueEntry command_active_;

    bool dma_active_valid_;
    QueueEntry dma_active_;

    bool completion_active_valid_;
    QueueEntry completion_active_;

    uint32_t doorbell_setup_cycles_;
    uint32_t dma_setup_cycles_;
    uint32_t protocol_overhead_cycles_;
    uint32_t effective_bytes_per_cycle_;
    uint32_t completion_return_cycles_;
    SST::Output output_;
};

} // namespace Tutorial

#endif

// Copyright (c) Siddharth Jayashankar. All rights reserved.
#ifndef CINNAMONCPU_H
#define CINNAMONCPU_H

#include <queue>

#include "sst/core/component.h"
#include "sst/core/event.h"
#include "sst/core/link.h"
#include "sst/core/output.h"
#include "sst/core/params.h"
#include "sst/core/sst_types.h"
// #include "sst/core/interfaces/simpleMem.h"
#include "sst/core/interfaces/stdMem.h"

#include "chip.h"
#include "latency.h"
#include "network.h"
#include "readers/reader.h"
#include "utils/utils.h"

// using namespace SST;
// using namespace SST::Interfaces;

namespace SST {
namespace Cinnamon {

extern uint64_t VEC_DEPTH;
// constexpr uint64_t VEC_DEPTH = 128;

class CinnamonAccelerator : public Component {
public:
    CinnamonAccelerator(ComponentId_t id, Params &params);
    ~CinnamonAccelerator() = default;

    // Virtual Functions From BaseComponent
    void init(unsigned int phase);
    void setup();
    void finish();

    SST_ELI_REGISTER_COMPONENT(
        CinnamonAccelerator,
        "cinnamon",
        "Accelerator",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Cinnamon Accelerator",
        COMPONENT_CATEGORY_PROCESSOR)

    SST_ELI_DOCUMENT_PARAMS(
        {"verbose", "Verbosity for debugging. Increased numbers for increased verbosity.", "0"},
        {"reader", "The trace reader module to load", "cinnamon.CinnamonTextTraceReader"},
        {"clock", "Sets the clock of the core", "2GHz"},

        // Functional Unit parameters
        {"computeAddQueueSize", "Sets size of funcitonal unit", "10"},
        {"numComputeAddUnits", "Sets number of compute add units", "5"},
        {"numComputeAddCycles", "Sets number of cycles it takes to process an add instruction", "5"},

        {"computeMultQueueSize", "Sets size of funcitonal unit", "10"},
        {"numComputeMultUnits", "Sets number of compute mult units", "5"},
        {"numComputeMultCycles", "Sets number of cycles it takes to process an mult instruction", "5"},

        // VRF parameters
        {"numPhysicalVecRegs", "Sets number of physical vector registers", "10"},
        {"vecRegSize", "Sets size of a vector register", "10"},

    )

    SST_ELI_DOCUMENT_PORTS(
        {"memory_link", "Link to the memory hierarchy (e.g., HBM)", {"memHierarchy.memEvent", ""}})

    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS(
        {"chip", "Slots for chip", "SST::Cinnamon::CinnamonChip"},
        {"network", "Trace Reader to use to", "SST::Cinnamon::CinnamonReader"})

    const Latency &latency() {
        return latency_;
    }

private:
    CinnamonAccelerator();                            // Serialization only
    CinnamonAccelerator(const CinnamonAccelerator &); // Do not impl.
    void operator=(const CinnamonAccelerator &);      // Do not impl.

    size_t numChips;
    std::vector<std::unique_ptr<CinnamonChip>> chips;
    std::shared_ptr<SST::Output> output;

    std::unique_ptr<CinnamonNetwork> network;
    Latency latency_;

    Cycle_t networkBusyCycles = 0;

    bool tick(Cycle_t cycle);

    void networkBusyCycle(Cycle_t currentCycle, Cycle_t val) {
        networkBusyCycles += val;
        if (currentCycle % 100000) {

            networkBusyCycles = 0;
        }
    }
};

} // Namespace Cinnamon
} // Namespace SST

#endif /* CINNAMONCPU_H */

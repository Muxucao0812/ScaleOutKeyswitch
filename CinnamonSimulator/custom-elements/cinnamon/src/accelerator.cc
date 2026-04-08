// Copyright (c) Siddharth Jayashankar. All rights reserved.
#include <cmath>
#include <queue>

#include "sst/core/component.h"
#include "sst/core/event.h"
#include "sst/core/link.h"
#include "sst/core/output.h"
#include "sst/core/params.h"
#include "sst/core/sst_types.h"
// #include "sst/core/interfaces/simpleMem.h"
#include "sst/core/interfaces/stdMem.h"

#include "accelerator.h"
#include "readers/reader.h"
#include "utils/utils.h"

// using namespace SST;
// using namespace SST::Interfaces;

namespace SST {
namespace Cinnamon {

uint64_t VEC_DEPTH = 64;

CinnamonAccelerator::CinnamonAccelerator(ComponentId_t id, Params &params) : Component(id) {

    const uint32_t output_level = (uint32_t)params.find<uint32_t>("verbose", 0);
    output = std::make_shared<SST::Output>(SST::Output("Cinnamon[@p:@l]: ", output_level, 0, SST::Output::STDOUT));

    std::string prosClock = params.find<std::string>("clock", "1GHz");
    // Register the clock
    TimeConverter *time = registerClock(prosClock, new Clock::Handler<CinnamonAccelerator>(this, &CinnamonAccelerator::tick));

    numChips = params.find<size_t>("num_chips", "1");

    VEC_DEPTH = params.find<uint64_t>("vec_depth", "64");

    output->verbose(CALL_INFO, 2, 0, "Configured Cinnamon VecDepth %lu\n", VEC_DEPTH);
    output->verbose(CALL_INFO, 2, 0, "Configured Cinnamon clock for %s\n", prosClock.c_str());

    // // tell the simulator not to end without us
    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();

    latency_.Mul = 5;
    latency_.Add = 1;
    latency_.Evg = 200;

    latency_.Mod = 6 + VEC_DEPTH * (16 - 1);
    latency_.Rsv = 9 + VEC_DEPTH * (16 - 1);

    latency_.NTT_butterfly = 6;
    latency_.Rot_one_stage = std::log2l(256);

    latency_.NTT_one_stage = std::log2l(256) * latency_.NTT_butterfly; // 48 cycles
    latency_.Transpose = VEC_DEPTH + std::log2l(VEC_DEPTH);
    latency_.NTT = latency_.NTT_one_stage + latency_.Mul + latency_.Transpose + latency_.NTT_one_stage;
    latency_.Rot = latency_.Rot_one_stage + latency_.Transpose + latency_.Rot_one_stage + latency_.Transpose;

    latency_.Bcu_read = latency_.Mul * std::ceil(std::log2l(13)) + VEC_DEPTH * (2 - 1);
    latency_.Bcu_write = 1;

    network.reset(loadUserSubComponent<CinnamonNetwork>("network", ComponentInfo::SHARE_NONE, this, numChips));
    if (!network) {
        output->fatal(CALL_INFO, -1, "Unable to load Cinnamon Network\n");
    }

    for (size_t chipID = 0; chipID < numChips; chipID++) {
        std::unique_ptr<CinnamonChip> chip(loadUserSubComponent<CinnamonChip>("chip_" + std::to_string(chipID), ComponentInfo::SHARE_NONE, this, network.get(), chipID));
        if (!chip) {
            output->fatal(CALL_INFO, -1, "Unable to load chip_%ld\n", chipID);
        }
        chips.push_back(std::move(chip));
    }

    output->verbose(CALL_INFO, 2, 0, "Cinnamon configuration completed successfully.\n");
}

void CinnamonAccelerator::init(unsigned int phase) {
    for (auto &chip : chips) {
        chip->init(phase);
    }
}

void CinnamonAccelerator::setup() {
    for (auto &chip : chips) {
        chip->setup();
    }
}

void CinnamonAccelerator::finish() {
    for (auto &chip : chips) {
        chip->finish();
    }
    output->output("------------------------------------------------------------------------\n");
    output->output("%s", network->printStats().c_str());
    output->output("------------------------------------------------------------------------\n");
    output->output("Finished \n");
}

bool CinnamonAccelerator::tick(SST::Cycle_t cycle) {
    bool retval = true;
    for (auto &chip : chips) {
        retval &= chip->tick(cycle);
    }
    network->tick(cycle);
    if (retval) {
        primaryComponentOKToEndSim();
        return true;
    }
    return false;
}

} // Namespace Cinnamon
} // Namespace SST

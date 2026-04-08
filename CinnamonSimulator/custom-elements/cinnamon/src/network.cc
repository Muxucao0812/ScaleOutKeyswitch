// Copyright (c) Siddharth Jayashankar. All rights reserved.
#include "network.h"
#include "accelerator.h"

namespace SST {
namespace Cinnamon {

CinnamonNetwork::CinnamonNetwork(ComponentId_t id, Params &params, CinnamonAccelerator *accelerator, size_t numChips) : accelerator(accelerator), numChips(numChips), SubComponent(id) {

    const uint32_t output_level = (uint32_t)params.find<uint32_t>("verbose", 0);
    output = std::make_shared<SST::Output>(SST::Output("CinnamonNetwork[@p:@l]: ", output_level, 0, SST::Output::STDOUT));

    hops = params.find<uint32_t>("hops", "2");
    auto linkBW = params.find<UnitAlgebra>("linkBW");
    UnitAlgebra limbSize("224KB");
    UnitAlgebra outputClock = linkBW / limbSize;

    std::string port_name_base("chip_port_");
    for (size_t chipID = 0; chipID < numChips; chipID++) {
        std::string port_name = port_name_base + std::to_string(chipID);
        output->verbose(CALL_INFO, 1, 0, "Configured Link: %s\n", port_name.c_str());
        auto link = configureLink(port_name, new Event::Handler<CinnamonNetwork, int>(this, &CinnamonNetwork::handleInput, chipID));
        if (!link) {
            output->fatal(CALL_INFO, -1, "Unable to load chip Link for port : %s\n", port_name.c_str());
        }
        chipLinks.push_back(link);
        auto outputTimingLink = configureSelfLink("output_timing_" + std::to_string(chipID), outputClock,
                                                  new Event::Handler<CinnamonNetwork, int>(this, &CinnamonNetwork::handleOutput, chipID));
        outputTiming.push_back(outputTimingLink);
    }

    outputBWBuffer.resize(numChips);
}
CinnamonNetwork::~CinnamonNetwork() {}

void CinnamonNetwork::init(unsigned int phase) {}
void CinnamonNetwork::setup() {}
void CinnamonNetwork::finish() {}

bool CinnamonNetwork::tryRegisterSync(size_t ChipID, uint64_t syncID, uint64_t syncSize, OpType op, bool sendReply /* Does the network need to send you a value */, bool recvValue /* Are you sending a value to the network*/) {
    std::unique_lock lock(mtx);
    if (syncOps.find(syncID) == syncOps.end()) {
        if (syncOps.size() > 1) {
            output->verbose(CALL_INFO, 4, 0, "Registered Sync for syncID = %ld. Sync op size: \n", syncID);
        }
        auto syncOp = SyncOperation(syncID, syncSize, op);
        syncOp.incrementReadyCount(ChipID);
        output->verbose(CALL_INFO, 4, 0, "Registered Sync for syncID = %ld\n", syncID);
        if (recvValue) {
            syncOp.incrementInputsPending();
        }
        if (sendReply) {
            syncOp.incrementOutputsPending();
            if (op == OpType::Agg) {
                syncOp.setAggregationDestination(ChipID);
                // assert(aggregateDestination == -1);
                // aggregateDestination = ChipID;
            } else if (op == OpType::Brc) {
                syncOp.addBroadcastDestination(ChipID);
            }
        }
        syncOps[syncID] = std::move(syncOp);
        return true;
    } else {
        auto &syncOp = syncOps.at(syncID);
        if (op != syncOp.operation()) {
            throw std::invalid_argument("Registered operation does not match expected operation");
        }
        if (syncSize != syncOp.syncSize()) {
            throw std::invalid_argument("Registered syncSize does not match expected syncSize");
        }
        syncOp.incrementReadyCount(ChipID);
        if (recvValue) {
            syncOp.incrementInputsPending();
        }
        if (sendReply) {
            syncOp.incrementOutputsPending();
            if (op == OpType::Agg) {
                syncOp.setAggregationDestination(ChipID);
            } else if (op == OpType::Brc) {
                syncOp.addBroadcastDestination(ChipID);
            }
        }
        output->verbose(CALL_INFO, 4, 0, "Increment readyCount to %ld for syncID = %ld\n", syncOp.readyCount(), syncID);
        return true;
    }
    return false;
}

bool CinnamonNetwork::networkReady(uint64_t syncID) const {
    std::shared_lock lock(mtx);
    if (syncOps.find(syncID) == syncOps.end()) {
        return false;
    }
    const auto &syncOp = syncOps.at(syncID);
    assert(syncOp.inputsPending() >= 0);
    assert(syncOp.outputsPending() >= 0);
    bool ready = syncOp.ready();
    auto operation = syncOp.operation();
    if (ready) {
        if (operation == OpType::Brc) {
            assert(syncOp.inputsPending() == 1);
        } else if (operation == OpType::Agg) {
            assert(syncOp.outputsPending() == 1);
            assert(syncOp.aggregationDestination() != -1);
        }
        // syncOp.computeRoute();
    }
    return ready;
}

void CinnamonNetwork::handleInput(SST::Event *ev, int portID) {
    std::unique_ptr<CinnamonNetworkEvent> networkEvent(static_cast<CinnamonNetworkEvent *>(ev));
    auto syncID = networkEvent->syncID();
    if (syncOps.find(syncID) == syncOps.end()) {
        output->fatal(CALL_INFO, -1, "%s: %lu Received Spurious Incoming With mismatching syncID: %lu\n", getName().c_str(), accelerator->getCurrentSimTime(), networkEvent->syncID());
        return;
    }

    auto &syncOp = syncOps.at(syncID);
    syncOp.decrementInputsPending();
    assert(syncOp.inputsPending() >= 0);
    output->verbose(CALL_INFO, 2, 4, "%s: %lu Received Incoming with syncID : %lu\n", getName().c_str(), accelerator->getCurrentSimCycle(), networkEvent->syncID());
    if (syncOp.inputsPending() == 0) {
        auto operation = syncOp.operation();
        if (operation == OpType::Brc) {
            for (auto &i : syncOp.broadcastDestinations()) {
                if (i == portID) {
                    continue;
                }
                // TODO: Make buffers here that handle the latency of ops
                auto outputBWBufferEntry = CinnamonNetworkOutputBWEntry(syncID, 224 * 1024);
                outputBWBuffer[i].push_back(outputBWBufferEntry);
            }
        } else if (operation == OpType::Agg) {
            auto aggregationDestination = syncOp.aggregationDestination();
            assert(aggregationDestination != -1);

            auto outputBWBufferEntry = CinnamonNetworkOutputBWEntry(syncID, 224 * 1024);
            outputBWBuffer[aggregationDestination].push_back(outputBWBufferEntry);
        } else {
            throw std::runtime_error("Unimplement Network Operation");
        }
    }
}

void CinnamonNetwork::handleOutput(SST::Event *ev, int portID) {
    std::unique_ptr<CinnamonNetworkEvent> networkEvent(static_cast<CinnamonNetworkEvent *>(ev));
    auto syncID = networkEvent->syncID();
    if (syncOps.find(syncID) == syncOps.end()) {
        output->fatal(CALL_INFO, -1, "%s: %lu Received Spurious Incoming With mismatching syncID: %lu\n", getName().c_str(), accelerator->getCurrentSimTime(), networkEvent->syncID());
        return;
    }

    auto &syncOp = syncOps.at(syncID);

    output->verbose(CALL_INFO, 4, 0, "%s: %lu Outputing syncID : %lu to chip: %d\n", getName().c_str(), accelerator->getCurrentSimCycle(), networkEvent->syncID(), portID);
    auto responseEvent = std::make_unique<CinnamonNetworkEvent>(networkEvent->syncID());

    auto hops_ = syncOp.computeHops();
    /* -1 because we already counted the latency once while receiving*/
    chipLinks[portID]->send(hops_ - 1 /*Latency */, responseEvent.release());
    syncOp.decrementOutputsPending();
    if (syncOp.inputsPending() == 0 && syncOp.outputsPending() == 0) {
        completeOperation(syncID);
    }
    assert(!outputBWBuffer[portID].empty());
    auto &bufferEntry = outputBWBuffer[portID].front();
    assert(bufferEntry.inFlight == true);
    outputBWBuffer[portID].pop_front();
}

void CinnamonNetwork::completeOperation(uint64_t syncID) {
    std::unique_lock lock(mtx);
    auto &syncOp = syncOps.at(syncID);
    assert(syncOp.inputsPending() == 0);
    assert(syncOp.outputsPending() == 0);
    output->verbose(CALL_INFO, 3, 0, "Completed Operation for syncID = %ld\n", syncID);
    syncOps.erase(syncID);
}

bool CinnamonNetwork::tick(SST::Cycle_t cycle) {
    for (auto &[k, v] : syncOps) {
        if (v.ready()) {
            stats_.busyCycles++;
            stats_.busyCyclesWindow++;
            break;
        }
    }

    if (cycle % 100000 == 0) {
        output->verbose(CALL_INFO, 2, 0, "%s:Heartbeat @ %" PRIu64 " 00K cycles. Network Util Cycles: %" PRIu64 "\n", getName().c_str(), cycle / (100000), stats_.busyCyclesWindow);
        output->flush();
        stats_.busyCyclesWindow = 0;
    }
    stats_.totalCycles++;
    bufferTick(cycle);
    return true;
}

bool CinnamonNetwork::bufferTick(SST::Cycle_t cycle) {
    for (size_t i = 0; i < numChips; i++) {
        if (outputBWBuffer[i].empty()) {
            continue;
        }
        auto &bufferEntry = outputBWBuffer[i].front();
        if (bufferEntry.inFlight) {
            continue;
        }
        auto packet = std::make_unique<CinnamonNetworkEvent>(bufferEntry.syncID);
        outputTiming[i]->send(1, packet.release());
        bufferEntry.inFlight = true;
    }
    return true;
}

std::string CinnamonNetwork::printStats() const {

    std::stringstream s;
    s << "Network Unit: \n";
    s << "\tTotal Cycles: " << stats_.totalCycles << "\n";
    s << "\tBusy Cycles: " << stats_.busyCycles << "\n";
    double utilisation = ((100.0) * stats_.busyCycles) / stats_.totalCycles;
    s << "\tUtilisation %: " << utilisation << "\n";
    return s.str();
}

} // namespace Cinnamon
} // namespace SST
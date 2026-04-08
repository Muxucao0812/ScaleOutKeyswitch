// Copyright (c) Siddharth Jayashankar. All rights reserved.
#ifndef __CINNAMON_NETWORK_H
#define __CINNAMON_NETWORK_H

#include <cmath>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sst/core/params.h>
#include <sst/core/subcomponent.h>

namespace SST {
namespace Cinnamon {

class CinnamonAccelerator;

class CinnamonNetworkEvent : public Event {
public:
    CinnamonNetworkEvent(uint64_t syncID) : syncID_(syncID), Event(){};

    uint64_t syncID() const {
        return syncID_;
    }

    // Add request size

private:
    uint64_t syncID_;
    CinnamonNetworkEvent();
    ImplementSerializable(SST::Cinnamon::CinnamonNetworkEvent)
};

class CinnamonNetwork : public SubComponent {
public:
    enum OpType {
        Brc, // Broadcast
        Agg  // Aggregate
    };

    CinnamonNetwork(ComponentId_t id, Params &params, CinnamonAccelerator *accelerator, size_t numChips);
    ~CinnamonNetwork();

    void init(unsigned int phase);
    void setup();
    void finish();
    bool tick(Cycle_t);
    bool bufferTick(Cycle_t);

    SST_ELI_REGISTER_SUBCOMPONENT_API(SST::Cinnamon::CinnamonNetwork, CinnamonAccelerator *, size_t)

    SST_ELI_REGISTER_SUBCOMPONENT(
        CinnamonNetwork,
        "cinnamon",
        "Network",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Cinnamon Network",
        SST::Cinnamon::CinnamonNetwork);

    SST_ELI_DOCUMENT_PORTS(
        {"chip_port_%(numChips)d", "Ports which connect to chips.", {}})
    // Attempt to register a synchronisation across chips
    // Makes sure that all chip network instructions synchronise at a barrier

    // Returns true if synchronisation registered on the network
    // Note: This call must succeed only once per chip per id
    bool tryRegisterSync(size_t ChipID, uint64_t id, uint64_t syncSize, OpType op, bool sendReply /* Does the network need to send you a value */, bool recvValue /* Are you sending a value to the network*/);

    // Return true if all chips have reached the synchronisation barrier for a syncID
    bool networkReady(uint64_t id) const;

    std::string printStats() const;

private:
    CinnamonNetwork();                        // Serialization only
    CinnamonNetwork(const CinnamonNetwork &); // Do not impl.
    void operator=(const CinnamonNetwork &);  // Do not impl.

    CinnamonAccelerator *accelerator;
    std::shared_ptr<SST::Output> output;
    size_t numChips;

    struct SyncOperation {
        uint64_t syncID_;
        size_t readyCount_;
        size_t syncSize_;
        OpType operation_;
        int inputsPending_;
        int outputsPending_;
        int aggregationDestination_;
        std::vector<int> broadcastDestinations_;
        int minDestination = 100000;
        int maxDestination = -1;

        SyncOperation() : syncID_(-1), syncSize_(-1), readyCount_(-1), inputsPending_(-1), outputsPending_(-1), aggregationDestination_(-1) {}
        SyncOperation(uint64_t syncID, size_t syncSize, OpType operation) : syncID_(syncID), syncSize_(syncSize), readyCount_(0), operation_(operation), inputsPending_(0), outputsPending_(0), aggregationDestination_(-1), minDestination(10000), maxDestination(-1) {}

        void addBroadcastDestination(int dest) {
            broadcastDestinations_.push_back(dest);
        }
        void setAggregationDestination(int dest) {
            assert(aggregationDestination_ == -1);
            aggregationDestination_ = dest;
        }
        void incrementReadyCount(size_t chipID) {
            readyCount_++;
            if (chipID > maxDestination) {
                maxDestination = chipID;
            }
            if (chipID < minDestination) {
                minDestination = chipID;
            }
        }
        void incrementInputsPending() {
            inputsPending_++;
        }
        void incrementOutputsPending() {
            outputsPending_++;
        }
        void decrementInputsPending() {
            inputsPending_--;
        }
        void decrementOutputsPending() {
            outputsPending_--;
        }

        auto operation() const {
            return operation_;
        }

        auto syncSize() const {
            return syncSize_;
        }

        auto readyCount() const {
            return readyCount_;
        }

        auto inputsPending() const {
            return inputsPending_;
        }

        auto outputsPending() const {
            return outputsPending_;
        }

        auto aggregationDestination() const {
            return aggregationDestination_;
        }

        bool ready() const {
            return readyCount_ == syncSize_;
        }

        const auto &broadcastDestinations() const {
            return broadcastDestinations_;
        }

        std::size_t computeHops() const {
            return static_cast<std::size_t>(std::log2(maxDestination - minDestination));
        }
    };

    struct CinnamonNetworkOutputBWEntry {
        uint64_t syncID;
        std::size_t size;
        std::size_t bytesSent;
        std::size_t bytesReceived;
        bool inFlight;

        CinnamonNetworkOutputBWEntry(uint64_t syncID, std::size_t size) : syncID(syncID), size(size), bytesSent(0), bytesReceived(0), inFlight(false){};
    };

    std::map<uint64_t, SyncOperation> syncOps;
    std::vector<std::deque<CinnamonNetworkOutputBWEntry>> outputBWBuffer;

    int hops;
    mutable std::shared_mutex mtx;

    std::vector<Link *> chipLinks;
    std::vector<Link *> outputTiming;

    struct Stats {
        SST::Cycle_t totalCycles = 0;
        SST::Cycle_t busyCycles = 0;
        SST::Cycle_t busyCyclesWindow = 0;
    } stats_;

    // Mark the operation as complete and make the network ready to accept the next operation
    void completeOperation(uint64_t syncID);
    void handleInput(SST::Event *ev, int id);
    void handleOutput(SST::Event *ev, int id);
};
} // namespace Cinnamon
} // namespace SST

#endif // __CINNAMON_NETWORK_H
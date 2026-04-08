// Copyright (c) Siddharth Jayashankar. All rights reserved.
#ifndef _H_SST_CINNAMON_MEMORY_UNIT
#define _H_SST_CINNAMON_MEMORY_UNIT

#include <list>
#include <queue>

#include "accelerator.h"
#include "instruction.h"
#include "physicalRegister.h"
#include "sst/core/interfaces/stdMem.h"
#include "utils/utils.h"
#include <sst/core/component.h>

namespace SST {
namespace Cinnamon {

class CinnamonMemoryUnit {

    CinnamonChip *pe;
    CinnamonAccelerator *accelerator;
    std::shared_ptr<SST::Output> output;
    std::list<std::shared_ptr<CinnamonMemoryInstruction>> loadQueue;
    std::list<std::shared_ptr<CinnamonMemoryInstruction>> storeQueue;
    Interfaces::StandardMem *memory; // Interface to Memory
    // Interfaces::StandardMem::Request::id_t outstandingRequestID;
    size_t requestWidth = 64;
    size_t NumConcurrentRequests = 2;

    struct MemRequest {
        size_t bytesProcessed = 0;
        size_t requestSize = 0;
        SST::Cycle_t issuedAtCycle = 0;
        std::uint16_t cyclesToCompletion = 0;
        bool responseReceived = false;
        std::shared_ptr<CinnamonMemoryInstruction> busyWith = nullptr;
    } memRequest[2];

    std::unordered_map<Interfaces::StandardMem::Request::id_t, MemRequest *> outstandingRequestID;

    // SST::Cycle_t busyCycles;
    // SST::Cycle_t totalCycles;

    struct Stats {
        uint64_t loadsIssued = 0;
        uint64_t storesIssued = 0;
        SST::Cycle_t totalCycles = 0;
        SST::Cycle_t busyCycles = 0;
        SST::Cycle_t totalLatency = 0;
        SST::Cycle_t maxLatency = 0;
        SST::Cycle_t busyCyclesWindow = 0;
    } stats_;

public:
    // CinnamonMemoryUnit(Interfaces::StandardMem * memory);
    CinnamonMemoryUnit(CinnamonChip *pe, CinnamonAccelerator *accelerator, const uint32_t outputLevel, Interfaces::StandardMem *memory, size_t requestWidth);
    std::shared_ptr<PhysicalRegister> findLoadAlias(Interfaces::StandardMem::Addr addr);
    std::shared_ptr<PhysicalRegister> findStoreAlias(Interfaces::StandardMem::Addr addr, bool quashAliasingStore);
    void addToLoadQueue(std::shared_ptr<CinnamonMemoryInstruction>);
    void addToStoreQueue(std::shared_ptr<CinnamonMemoryInstruction>);
    bool operateQueue(SST::Cycle_t currentCycle, std::list<std::shared_ptr<CinnamonMemoryInstruction>> &queue, const std::string &queueName);
    void executeCycleBegin(SST::Cycle_t currentCycle);
    void executeCycleEnd(SST::Cycle_t currentCycle);
    void init(unsigned int phase);
    void setup();
    void handleResponse(SST::Interfaces::StandardMem::Request *ev);
    void handleVectorLoad(SST::Cycle_t currentCycle, size_t memRequestIndex, Interfaces::StandardMem::Addr addr, std::size_t size);
    void handleVectorStore(SST::Cycle_t currentCycle, size_t memRequestIndex, Interfaces::StandardMem::Addr addr, std::size_t size);
    void handleScalarLoad(SST::Cycle_t currentCycle, Interfaces::StandardMem::Addr addr, std::size_t size);
    bool okayToFinish();
    std::string printStats();
};
} // Namespace Cinnamon
} // Namespace SST

#endif
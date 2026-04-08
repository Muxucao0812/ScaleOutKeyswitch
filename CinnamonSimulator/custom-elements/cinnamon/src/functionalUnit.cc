// Copyright (c) Siddharth Jayashankar. All rights reserved.

#include "functionalUnit.h"
#include "accelerator.h"
#include "chip.h"
#include "sst/core/interfaces/stdMem.h"
#include <sst/core/component.h>

#include <optional>

namespace SST {
namespace Cinnamon {

CinnamonAddQueue::CinnamonAddQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &addUnits) : pe(pe), name(name), latency(latency), addUnits(addUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonAddQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::Add:
    case OpCode::Sub:
    case OpCode::Neg:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonAddQueue::tick(SST::Cycle_t currentCycle) {
    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {

            // TODO: Reserve Register File too...

            SST::Cycle_t start = currentCycle;
            SST::Cycle_t end = currentCycle + VEC_DEPTH - 1;
            CinnamonInstructionInterval interval(start, end, instruction);

            bool instructionDispatched = false;
            for (int i = 0; i < addUnits.size(); i++) {
                if (addUnits.at(i)->isIntervalReservable(interval) == true) {
                    addUnits.at(i)->addReservation(interval);
                    instructionDispatched = true;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), interval.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!instructionDispatched) {
                return;
            } else {
                it = instructionQueue.erase(it);
            }
        } else {
            it++;
        }
    }
}

bool CinnamonAddQueue::okayToFinish() {
    return instructionQueue.empty();
}

//###########################################

CinnamonMulQueue::CinnamonMulQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &mulUnits) : pe(pe), name(name), latency(latency), mulUnits(mulUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonMulQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::Mul:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonMulQueue::tick(SST::Cycle_t currentCycle) {
    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {
            SST::Cycle_t start = currentCycle;
            SST::Cycle_t end = currentCycle + VEC_DEPTH - 1 + latency.Mul;
            CinnamonInstructionInterval interval(start, end, instruction);

            bool instructionDispatched = false;
            for (int i = 0; i < mulUnits.size(); i++) {
                if (mulUnits.at(i)->isIntervalReservable(interval) == true) {
                    mulUnits.at(i)->addReservation(interval);
                    instructionDispatched = true;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), interval.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!instructionDispatched) {
                return;
            } else {
                it = instructionQueue.erase(it);
            }
        } else {
            it++;
        }
    }
}

bool CinnamonMulQueue::okayToFinish() {
    return instructionQueue.empty();
}

//###########################################

CinnamonEvgQueue::CinnamonEvgQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &evgUnits) : pe(pe), name(name), latency(latency), evgUnits(evgUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonEvgQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::EvkGen:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonEvgQueue::tick(SST::Cycle_t currentCycle) {
    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {
            SST::Cycle_t start = currentCycle;
            SST::Cycle_t end = currentCycle + VEC_DEPTH - 1 + latency.Evg;
            CinnamonInstructionInterval interval(start, end, instruction);

            bool instructionDispatched = false;
            for (int i = 0; i < evgUnits.size(); i++) {
                if (evgUnits.at(i)->isIntervalReservable(interval) == true) {
                    evgUnits.at(i)->addReservation(interval);
                    instructionDispatched = true;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Evg FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), interval.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!instructionDispatched) {
                return;
            } else {
                it = instructionQueue.erase(it);
            }
        } else {
            it++;
        }
    }
}

bool CinnamonEvgQueue::okayToFinish() {
    return instructionQueue.empty();
}

//###########################################

CinnamonRotQueue::CinnamonRotQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &rotUnits, const FuVector &transposeUnits) : pe(pe), name(name), latency(latency), rotUnits(rotUnits), transposeUnits(transposeUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonRotQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::Rot:
    case OpCode::Con:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonRotQueue::tick(SST::Cycle_t currentCycle) {

    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {
            SST::Cycle_t startRot = currentCycle;
            SST::Cycle_t endRot = startRot + VEC_DEPTH - 1;
            CinnamonInstructionInterval intervalRot(startRot, endRot, instruction);

            SST::Cycle_t startTra1 = currentCycle + latency.Rot_one_stage;
            SST::Cycle_t endTra1 = startTra1 + VEC_DEPTH - 1;
            std::shared_ptr<CinnamonInstruction> nopInstruction1 = std::make_shared<CinnamonNoOpInstruction>();
            CinnamonInstructionInterval intervalTra1(startTra1, endTra1, nopInstruction1);

            SST::Cycle_t startTra2 = currentCycle + latency.Rot_one_stage + latency.Transpose + latency.Rot_one_stage;
            assert(startTra2 > endTra1);
            SST::Cycle_t endTra2 = startTra2 + VEC_DEPTH - 1;
            std::shared_ptr<CinnamonInstruction> nopInstruction2 = std::make_shared<CinnamonNoOpInstruction>();
            CinnamonInstructionInterval intervalTra2(startTra2, endTra2, nopInstruction2);

            bool instructionDispatched = false;
            std::optional<int> rotUnitID, transposeUnit1ID, transposeUnit2ID;
            for (int i = 0; i < rotUnits.size(); i++) {
                if (rotUnits.at(i)->isIntervalReservable(intervalRot) == true) {
                    rotUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Rot FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalRot.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!rotUnitID.has_value()) {
                return;
            }
            for (int i = 0; i < transposeUnits.size(); i++) {
                if (transposeUnits.at(i)->isIntervalReservable(intervalTra1) == true) {
                    transposeUnit1ID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Transpose FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalTra1.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!transposeUnit1ID.has_value()) {
                return;
            }
            for (int i = 0; i < transposeUnits.size(); i++) {
                if (transposeUnits.at(i)->isIntervalReservable(intervalTra2) == true) {
                    transposeUnit2ID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Transpose FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalTra2.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!transposeUnit2ID.has_value()) {
                return;
            }
            auto selectedRotUnit = rotUnits.at(rotUnitID.value());
            auto selectedTranspose1Unit = transposeUnits.at(transposeUnit1ID.value());
            auto selectedTranspose2Unit = transposeUnits.at(transposeUnit2ID.value());
            selectedRotUnit->addReservation(intervalRot);
            selectedTranspose1Unit->addReservation(intervalTra1);
            selectedTranspose2Unit->addReservation(intervalTra2);
            output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Dispatched Instruction: %s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
            it = instructionQueue.erase(it);
        } else {
            it++;
        }
    }
}

bool CinnamonRotQueue::okayToFinish() {
    return instructionQueue.empty();
}

//###########################################

CinnamonNttQueue::CinnamonNttQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &bcReadUnits, const FuVector &nttUnits, const FuVector &transposeUnits, const FuVector &bcWriteUnits) : pe(pe), name(name), latency(latency), bcReadUnits(bcReadUnits), nttUnits(nttUnits), transposeUnits(transposeUnits), bcWriteUnits(bcWriteUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonNttQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::Ntt:
    case OpCode::Int:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonNttQueue::tick(SST::Cycle_t currentCycle) {

    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {

            std::optional<int> bcReadUnitID, nttUnitID, transposeUnitID;

            SST::Cycle_t startBcRead = currentCycle;
            SST::Cycle_t endBcRead = startBcRead + VEC_DEPTH - 1 + latency.Bcu_read;
            SST::Cycle_t startNtt = currentCycle;
            CinnamonInstructionInterval intervalBcRead(startBcRead, endBcRead);
            auto nttInstruction = std::dynamic_pointer_cast<CinnamonNttInstruction>(instruction);
            if (nttInstruction && nttInstruction->hasBcSrc()) {
                auto bcuSrcPhyID = nttInstruction->getBcSrcPhyID();
                assert(bcuSrcPhyID != -1);

                for (int i = 0; i < bcReadUnits.size(); i++) {
                    if (bcReadUnits.at(i)->isIntervalReservable(intervalBcRead) == true) {
                        bcReadUnitID = i;
                        output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on BcRead FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalBcRead.getString().c_str(), instruction->getString().c_str(), i);
                        break;
                    }
                }
                if (!bcReadUnitID.has_value()) {
                    return;
                }
                // if (bcReadUnits.at(bcuSrcPhyID)->isIntervalReservable(intervalBcRead) == true){
                //     output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on BC Read FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalBcRead.getString().c_str(), instruction->getString().c_str(),bcuSrcPhyID);
                // } else {
                //     return;
                // }
                startNtt = startBcRead + latency.Bcu_read;
            } else {
                startNtt = currentCycle;
            }
            SST::Cycle_t endNtt = startNtt + VEC_DEPTH - 1 + latency.NTT_butterfly; // Need to reserve extra time since we can't pipeline across limbs;
            CinnamonInstructionInterval intervalNtt(startNtt, endNtt);

            SST::Cycle_t startTra = startNtt + latency.NTT_one_stage + latency.Mul; // TODO: Set this as the NTT latency
            SST::Cycle_t endTra = startTra + VEC_DEPTH - 1;
            std::shared_ptr<CinnamonInstruction> nopInstruction = std::make_shared<CinnamonNoOpInstruction>();
            CinnamonInstructionInterval intervalTra(startTra, endTra, nopInstruction);

            SST::Cycle_t startBcWrite = startNtt + latency.NTT;
            SST::Cycle_t endBcWrite = startBcWrite + VEC_DEPTH - 1 + latency.Bcu_write;
            CinnamonInstructionInterval intervalBcWrite(startBcWrite, endBcWrite);
            auto inttInstruction = std::dynamic_pointer_cast<CinnamonInttInstruction>(instruction);
            if (inttInstruction && inttInstruction->hasBcDest()) {
                assert(0);
                // auto bcuDestPhyID = inttInstruction->getBcDestPhyID();
                // assert(bcuDestPhyID != -1);
                // if (bcWriteUnits.at(bcuDestPhyID)->isIntervalReservable(intervalBcWrite) == true){
                //     output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on BC Write FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalBcRead.getString().c_str(), instruction->getString().c_str(),bcuDestPhyID);
                // } else {
                //     return;
                // }
            }

            bool instructionDispatched = false;
            for (int i = 0; i < nttUnits.size(); i++) {
                if (nttUnits.at(i)->isIntervalReservable(intervalNtt) == true) {
                    nttUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on NTT FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalNtt.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            for (int i = 0; i < transposeUnits.size(); i++) {
                if (transposeUnits.at(i)->isIntervalReservable(intervalTra) == true) {
                    transposeUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Transpose FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalTra.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }

            if (nttUnitID.has_value() && transposeUnitID.has_value()) {

                if (nttInstruction && nttInstruction->hasBcSrc()) {
                    assert(bcReadUnitID.has_value());
                    auto split = nttInstruction->splitInstruction();
                    assert(split.size() == 2);
                    intervalBcRead = CinnamonInstructionInterval(startBcRead, endBcRead, split[0]);
                    intervalNtt = CinnamonInstructionInterval(startNtt, endNtt, split[1]);
                    bcReadUnits.at(bcReadUnitID.value())->addReservation(intervalBcRead);
                } else {
                    intervalNtt = CinnamonInstructionInterval(startNtt, endNtt, instruction);
                }

                auto selectedNttUnit = nttUnits.at(nttUnitID.value());
                auto selectedTransposeUnit = transposeUnits.at(transposeUnitID.value());
                selectedNttUnit->addReservation(intervalNtt);
                selectedTransposeUnit->addReservation(intervalTra);
                output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Dispatched Instruction: %s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
                it = instructionQueue.erase(it);
            } else {
                return;
            }
        } else {
            it++;
        }
    }
}

bool CinnamonNttQueue::okayToFinish() {
    return instructionQueue.empty();
}

//##########################

CinnamonSuDQueue::CinnamonSuDQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &bcReadUnits, const FuVector &addUnits, const FuVector &mulUnits, const FuVector &nttUnits, const FuVector &transposeUnits) : pe(pe), name(name), latency(latency), bcReadUnits(bcReadUnits), addUnits(addUnits), mulUnits(mulUnits), nttUnits(nttUnits), transposeUnits(transposeUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonSuDQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::SuD:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonSuDQueue::tick(SST::Cycle_t currentCycle) {

    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {

            std::optional<int> bcReadUnitID, nttUnitID, transposeUnitID, subUnitID, divUnitID;

            SST::Cycle_t startBcRead = currentCycle;
            SST::Cycle_t endBcRead = startBcRead + VEC_DEPTH - 1 + latency.Bcu_read;
            SST::Cycle_t startNtt = currentCycle;
            CinnamonInstructionInterval intervalBcRead(startBcRead, endBcRead);
            auto sudInstruction = std::dynamic_pointer_cast<CinnamonSuDInstruction>(instruction);
            assert(sudInstruction != nullptr);
            if (sudInstruction->hasBcSrc()) {
                auto bcuSrcPhyID = sudInstruction->getBcSrcPhyID();
                assert(bcuSrcPhyID != -1);
                for (int i = 0; i < bcReadUnits.size(); i++) {
                    if (bcReadUnits.at(i)->isIntervalReservable(intervalBcRead) == true) {
                        bcReadUnitID = i;
                        output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on BcRead FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalBcRead.getString().c_str(), instruction->getString().c_str(), i);
                        ;
                    }
                }
                if (!bcReadUnitID.has_value()) {
                    return;
                }
                // if (bcReadUnits.at(bcuSrcPhyID)->isIntervalReservable(intervalBcRead) == true){
                //     output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on BC Read FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalBcRead.getString().c_str(), instruction->getString().c_str(),bcuSrcPhyID);
                // } else {
                //     return;
                // }
                startNtt = startBcRead + latency.Bcu_read;
            } else {
                startNtt = currentCycle;
            }
            SST::Cycle_t endNtt = startNtt + VEC_DEPTH - 1 + latency.NTT_butterfly; // Need to reserve extra time since we can't pipeline across limbs;
            CinnamonInstructionInterval intervalNtt(startNtt, endNtt);

            SST::Cycle_t startTranspose = startNtt + latency.NTT_one_stage + latency.Mul; // TODO: Set this as the NTT latency
            SST::Cycle_t endTranspose = startTranspose + VEC_DEPTH - 1;
            std::shared_ptr<CinnamonInstruction> nopInstruction = std::make_shared<CinnamonNoOpInstruction>();
            CinnamonInstructionInterval intervalTranspose(startTranspose, endTranspose, nopInstruction);

            SST::Cycle_t startSub = startNtt + latency.NTT;
            SST::Cycle_t endSub = startSub + VEC_DEPTH - 1;
            CinnamonInstructionInterval intervalSub(startSub, endSub);

            SST::Cycle_t startDiv = startSub + latency.Add;
            SST::Cycle_t endDiv = startDiv + VEC_DEPTH - 1 + latency.Mul; // Need to reserve extra since we can't pipeline accross limbs
            CinnamonInstructionInterval intervalDiv(startDiv, endDiv);

            bool instructionDispatched = false;

            for (int i = 0; i < nttUnits.size(); i++) {
                if (nttUnits.at(i)->isIntervalReservable(intervalNtt) == true) {
                    nttUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on NTT FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalNtt.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!nttUnitID.has_value()) {
                return;
            }
            for (int i = 0; i < transposeUnits.size(); i++) {
                if (transposeUnits.at(i)->isIntervalReservable(intervalTranspose) == true) {
                    transposeUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Transpose FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalTranspose.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!transposeUnitID.has_value()) {
                return;
            }

            for (int i = 0; i < addUnits.size(); i++) {
                if (addUnits.at(i)->isIntervalReservable(intervalSub) == true) {
                    subUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Add FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalSub.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!subUnitID.has_value()) {
                return;
            }

            for (int i = 0; i < mulUnits.size(); i++) {
                if (mulUnits.at(i)->isIntervalReservable(intervalDiv) == true) {
                    divUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Mul FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalDiv.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!divUnitID.has_value()) {
                return;
            }

            // auto sudInstruction = std::dynamic_pointer_cast<CinnamonSuDInstruction>(instruction);

            auto selectedNttUnit = nttUnits.at(nttUnitID.value());
            auto selectedTransposeUnit = transposeUnits.at(transposeUnitID.value());
            auto selectedSubUnit = addUnits.at(subUnitID.value());
            auto selectedDivUnit = mulUnits.at(divUnitID.value());

            std::vector<std::shared_ptr<CinnamonInstruction>> splitInstructions = sudInstruction->splitInstruction();
            if (sudInstruction->hasBcSrc()) {
                assert(bcReadUnitID.has_value());
                assert(splitInstructions.size() == 4);
                intervalBcRead = CinnamonInstructionInterval(startBcRead, endBcRead, splitInstructions[0]);
                intervalNtt = CinnamonInstructionInterval(startNtt, endNtt, splitInstructions[1]);
                intervalSub = CinnamonInstructionInterval(startSub, endSub, splitInstructions[2]);
                intervalDiv = CinnamonInstructionInterval(startDiv, endDiv, splitInstructions[3]);
                bcReadUnits.at(bcReadUnitID.value())->addReservation(intervalBcRead);
            } else {
                assert(splitInstructions.size() == 3);
                intervalNtt = CinnamonInstructionInterval(startNtt, endNtt, splitInstructions[0]);
                intervalSub = CinnamonInstructionInterval(startSub, endSub, splitInstructions[1]);
                intervalDiv = CinnamonInstructionInterval(startDiv, endDiv, splitInstructions[2]);
            }

            selectedNttUnit->addReservation(intervalNtt);
            selectedTransposeUnit->addReservation(intervalTranspose);
            selectedSubUnit->addReservation(intervalSub);
            selectedDivUnit->addReservation(intervalDiv);
            output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Dispatched Instruction: %s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
            it = instructionQueue.erase(it);

        } else {
            it++;
        }
    }
}

bool CinnamonSuDQueue::okayToFinish() {
    return instructionQueue.empty();
}

//##########################

CinnamonBciQueue::CinnamonBciQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const std::vector<std::shared_ptr<CinnamonBaseConversionUnit>> &baseConversionUnits) : pe(pe), name(name), baseConversionUnits(baseConversionUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonBciQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    auto bciInstruction = std::dynamic_pointer_cast<CinnamonBciInstruction>(instruction);
    assert(bciInstruction != nullptr);
    switch (bciInstruction->getOpCode()) {
    case OpCode::Bci:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(bciInstruction);
}

void CinnamonBciQueue::tick(SST::Cycle_t currentCycle) {

    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        bool instructionDispatched = false;
        if (instruction->allOperandsReady()) {
            for (int i = 0; i < baseConversionUnits.size(); i++) {
                if (baseConversionUnits.at(i)->isBusy() == false) {
                    baseConversionUnits.at(i)->initInstruction(currentCycle, instruction);
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu BCI Queue:%s Dispatched Instruction %s to Unit: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str(), i);
                    instructionDispatched = true;
                    break;
                }
            }
            if (instructionDispatched) {
                it = instructionQueue.erase(it);
            } else {
                it++;
            }
        } else {
            it++;
        }
    }
}

bool CinnamonBciQueue::okayToFinish() {
    return instructionQueue.empty();
}

//##########################

CinnamonBcwQueue::CinnamonBcwQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &bcWriteUnits) : pe(pe), name(name), latency(latency), bcWriteUnits(bcWriteUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonBcwQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::BcW:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonBcwQueue::tick(SST::Cycle_t currentCycle) {

    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {

            SST::Cycle_t startBcWrite = currentCycle;
            SST::Cycle_t endBcWrite = startBcWrite + VEC_DEPTH - 1;
            CinnamonInstructionInterval intervalBcWrite(startBcWrite, endBcWrite, instruction);

            bool instructionDispatched = false;
            std::optional<int> bcWriteUnitID;
            for (int i = 0; i < bcWriteUnits.size(); i++) {
                if (bcWriteUnits.at(i)->isIntervalReservable(intervalBcWrite) == true) {
                    bcWriteUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on BcWrite FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalBcWrite.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }

            if (!bcWriteUnitID.has_value()) {
                return;
            }

            auto selectedBcWriteUnit = bcWriteUnits.at(bcWriteUnitID.value());

            selectedBcWriteUnit->addReservation(intervalBcWrite);
            output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Dispatched Instruction: %s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
            it = instructionQueue.erase(it);
        } else {
            it++;
        }
    }
}

bool CinnamonBcwQueue::okayToFinish() {
    return instructionQueue.empty();
}

//##########################

CinnamonPl1Queue::CinnamonPl1Queue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &nttUnits, const FuVector &transposeUnits, const FuVector &bcWriteUnits) : pe(pe), name(name), latency(latency), nttUnits(nttUnits), transposeUnits(transposeUnits), bcWriteUnits(bcWriteUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonPl1Queue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::Pl1:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonPl1Queue::tick(SST::Cycle_t currentCycle) {

    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {

            SST::Cycle_t startIntt = currentCycle;
            SST::Cycle_t endIntt = startIntt + VEC_DEPTH - 1 + latency.NTT_butterfly;
            CinnamonInstructionInterval intervalIntt(startIntt, endIntt);

            SST::Cycle_t startTranspose = currentCycle + latency.NTT_one_stage + latency.Mul;
            SST::Cycle_t endTranspose = startTranspose + VEC_DEPTH - 1;
            std::shared_ptr<CinnamonInstruction> nopInstruction = std::make_shared<CinnamonNoOpInstruction>();
            CinnamonInstructionInterval intervalTranspose(startTranspose, endTranspose, nopInstruction);

            SST::Cycle_t startBcWrite = currentCycle + latency.NTT;
            SST::Cycle_t endBcWrite = startBcWrite + VEC_DEPTH - 1;
            CinnamonInstructionInterval intervalBcWrite(startBcWrite, endBcWrite);

            bool instructionDispatched = false;
            std::optional<int> bcWriteUnitID, nttUnitID, transposeUnitID;
            for (int i = 0; i < nttUnits.size(); i++) {
                if (nttUnits.at(i)->isIntervalReservable(intervalIntt) == true) {
                    nttUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on NTT FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalIntt.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!nttUnitID.has_value()) {
                return;
            }
            for (int i = 0; i < transposeUnits.size(); i++) {
                if (transposeUnits.at(i)->isIntervalReservable(intervalTranspose) == true) {
                    transposeUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on Transpose FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalTranspose.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!transposeUnitID.has_value()) {
                return;
            }

            for (int i = 0; i < bcWriteUnits.size(); i++) {
                if (bcWriteUnits.at(i)->isIntervalReservable(intervalBcWrite) == true) {
                    bcWriteUnitID = i;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on BcWrite FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), intervalBcWrite.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }

            if (!bcWriteUnitID.has_value()) {
                return;
            }

            auto pl1Instruction = std::dynamic_pointer_cast<CinnamonPl1Instruction>(instruction);
            assert(pl1Instruction != nullptr);

            std::vector<std::shared_ptr<CinnamonInstruction>> splitInstructions = pl1Instruction->splitInstruction();
            assert(splitInstructions.size() == 2);

            auto selectedNttUnit = nttUnits.at(nttUnitID.value());
            auto selectedTransposeUnit = transposeUnits.at(transposeUnitID.value());
            auto selectedBcWriteUnit = bcWriteUnits.at(bcWriteUnitID.value());
            intervalIntt = CinnamonInstructionInterval(startIntt, endIntt, splitInstructions[0]);
            intervalBcWrite = CinnamonInstructionInterval(startBcWrite, endBcWrite, splitInstructions[1]);
            selectedNttUnit->addReservation(intervalIntt);
            selectedTransposeUnit->addReservation(intervalTranspose);
            selectedBcWriteUnit->addReservation(intervalBcWrite);
            output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Dispatched Instruction: %s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
            it = instructionQueue.erase(it);
        } else {
            it++;
        }
    }
}

bool CinnamonPl1Queue::okayToFinish() {
    return instructionQueue.empty();
}

//##########################

CinnamonRsvQueue::CinnamonRsvQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &rsvUnits) : pe(pe), name(name), latency(latency), rsvUnits(rsvUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonRsvQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::Rsi:
    case OpCode::Rsv:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonRsvQueue::tick(SST::Cycle_t currentCycle) {
    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {

            // TODO: Reserve Register File too...

            SST::Cycle_t start = currentCycle;
            SST::Cycle_t end = currentCycle + VEC_DEPTH - 1 + latency.Rsv;
            CinnamonInstructionInterval interval(start, end, instruction);

            bool instructionDispatched = false;
            for (int i = 0; i < rsvUnits.size(); i++) {
                if (rsvUnits.at(i)->isIntervalReservable(interval) == true) {
                    rsvUnits.at(i)->addReservation(interval);
                    instructionDispatched = true;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), interval.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!instructionDispatched) {
                return;
            } else {
                it = instructionQueue.erase(it);
            }
        } else {
            it++;
        }
    }
}

bool CinnamonRsvQueue::okayToFinish() {
    return instructionQueue.empty();
}

//###########################################

CinnamonModQueue::CinnamonModQueue(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const Latency &latency, const FuVector &modUnits) : pe(pe), name(name), latency(latency), modUnits(modUnits), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonModQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::Mod:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonModQueue::tick(SST::Cycle_t currentCycle) {
    if (instructionQueue.empty()) {
        if (QUEUE_EMPTY) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
        }
        return;
    }

    auto it = instructionQueue.begin();
    for (; it != instructionQueue.end();) {
        auto &instruction = *it;
        if (instruction->allOperandsReady()) {

            // TODO: Reserve Register File too...

            SST::Cycle_t start = currentCycle;
            SST::Cycle_t end = currentCycle + VEC_DEPTH - 1 + latency.Mod;
            CinnamonInstructionInterval interval(start, end, instruction);

            bool instructionDispatched = false;
            for (int i = 0; i < modUnits.size(); i++) {
                if (modUnits.at(i)->isIntervalReservable(interval) == true) {
                    modUnits.at(i)->addReservation(interval);
                    instructionDispatched = true;
                    output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Found Reservation Interval %s for Instruction: %s on FU: %d\n", pe->getName().c_str(), currentCycle, name.c_str(), interval.getString().c_str(), instruction->getString().c_str(), i);
                    break;
                }
            }
            if (!instructionDispatched) {
                return;
            } else {
                it = instructionQueue.erase(it);
            }
        } else {
            it++;
        }
    }
}

bool CinnamonModQueue::okayToFinish() {
    return instructionQueue.empty();
}

//###########################################

CinnamonDisQueue::CinnamonDisQueue(CinnamonChip *pe, CinnamonAccelerator *accelerator, const std::string &name, const uint32_t outputLevel, CinnamonNetwork *network, Link *networkLink) : pe(pe), accelerator(accelerator), network(network), networkLink(networkLink), name(name), syncRegistered(false), CinnamonInstructionQueue() {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
    networkLink->setFunctor(new Event::Handler<CinnamonDisQueue>(this, &CinnamonDisQueue::handle_incoming));
}

void CinnamonDisQueue::addToInstructionQueue(std::shared_ptr<CinnamonInstruction> instruction) {

    using OpCode = CinnamonInstruction::OpCode;
    switch (instruction->getOpCode()) {
    case OpCode::Rcv:
    case OpCode::Dis:
    case OpCode::Joi:
        break;
    default:
        assert(0);
    }
    instructionQueue.emplace_back(instruction);
}

void CinnamonDisQueue::handle_dis(std::shared_ptr<CinnamonDisInstruction> &instruction) {
    auto networkEvent = std::make_unique<CinnamonNetworkEvent>(instruction->syncID());
    networkLink->send(networkEvent.release());
    instruction->setExecutionComplete();
    busyWith = nullptr;
    syncRegistered = false;
}

void CinnamonDisQueue::handle_joi(std::shared_ptr<CinnamonDisInstruction> &instruction) {
    auto networkEvent = std::make_unique<CinnamonNetworkEvent>(instruction->syncID());
    if (instruction->hasSource() == true) {
        networkLink->send(networkEvent.release());
        output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Sending to network instruction: %s\n", pe->getName().c_str(), accelerator->getCurrentSimTime(), name.c_str(), busyWith->getString().c_str());
    }
    if (instruction->hasDest() == false) {
        // This instruction only sends data on the network, so we can mark the instruction as complete now
        instruction->setExecutionComplete();
        busyWith = nullptr;
        syncRegistered = false;
    }
}

void CinnamonDisQueue::handle_incoming(SST::Event *ev) {
    std::unique_ptr<CinnamonNetworkEvent> networkEvent(static_cast<CinnamonNetworkEvent *>(ev));
    if (!busyWith) {
        output->fatal(CALL_INFO, -1, "%s: %lu Received Spurious Response\n", pe->getName().c_str(), accelerator->getCurrentSimTime());
    }
    if (networkEvent->syncID() != busyWith->syncID()) {
        output->fatal(CALL_INFO, -1, "%s: %lu Received Response With mismatching syncID. Expected: %lu, Got: %lu\n", pe->getName().c_str(), accelerator->getCurrentSimTime(), networkEvent->syncID(), busyWith->syncID());
    }
    output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s received Response for instruction: %s\n", pe->getName().c_str(), accelerator->getCurrentSimTime(), name.c_str(), busyWith->getString().c_str());
    busyWith->setExecutionComplete();
    busyWith = nullptr;
    syncRegistered = false;
}

void CinnamonDisQueue::tick(SST::Cycle_t currentCycle) {
    stats_.totalCycles++;
    using OpCode = CinnamonInstruction::OpCode;
    if (busyWith == nullptr) {
        if (instructionQueue.empty()) {
            if (QUEUE_EMPTY) {
                output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Empty\n", pe->getName().c_str(), currentCycle, name.c_str());
            }
            return;
        }

        auto it = instructionQueue.begin();

        auto instruction = std::dynamic_pointer_cast<CinnamonDisInstruction>(*it);
        assert(instruction);

        if (!instruction->allOperandsReady()) {
            return;
        }

        auto syncID = instruction->syncID();
        auto syncSize = instruction->syncSize();

        if (!syncRegistered) {
            CinnamonNetwork::OpType opType;
            switch (instruction->getOpCode()) {
            case OpCode::Rcv:
            case OpCode::Dis:
                opType = CinnamonNetwork::OpType::Brc;
                break;
            case OpCode::Joi:
                opType = CinnamonNetwork::OpType::Agg;
                break;
            default:
                throw std::runtime_error("Invalid Instruciton for Network : " + instruction->getString());
                break;
            }
            syncRegistered = network->tryRegisterSync(pe->chipID(), syncID, syncSize, opType, instruction->hasDest(), instruction->hasSource());
            if (syncRegistered) {
                output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Registerd Sync for Instruction: %s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
            }
        }

        bool networkReady = network->networkReady(syncID);
        if (!networkReady) {
            stats_.waitingForNetworkCycles++;
            return;
        }

        busyWith = instruction;
        it = instructionQueue.erase(it);

        output->verbose(CALL_INFO, 4, 0, "%s: %lu Queue:%s Network ready for Instruction: %s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
        if (instruction->getOpCode() == OpCode::Dis) {
            handle_dis(instruction);
        } else if (instruction->getOpCode() == OpCode::Rcv) {
            ;
        } else if (instruction->getOpCode() == OpCode::Joi) {
            handle_joi(instruction);
        } else {
            throw std::runtime_error("Invalid OpCode For network instruction: " + instruction->getString());
        }
    } else if (busyWith != nullptr) {
        stats_.busyCycles++;
    }
}

bool CinnamonDisQueue::okayToFinish() {
    return instructionQueue.empty();
}

//###########################################

CinnamonFunctionalUnit::CinnamonFunctionalUnit(CinnamonChip *pe, const std::string &name, const uint32_t outputLevel, const uint16_t latency, const uint16_t vecDepth) : pe(pe), name(name), latency(latency), vecDepth(vecDepth) {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonFunctionalUnit::executeCycleEnd(SST::Cycle_t currentCycle) {

    stats_.totalCycles++;

    if (!inProcess.empty()) {
        stats_.busyCycles++;
        stats_.busyCyclesWindow++;
    }
    auto it = busyWith.begin();
    for (; it != busyWith.end();) {
        auto &instruction = it->first;
        auto &cyclesToReady = it->second;
        cyclesToReady--;
        if (cyclesToReady == 0) {
            instruction->setExecutionComplete();
            output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Instruction: %s is Ready\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
            it = busyWith.erase(it);
        } else {
            it++;
        }
    }

    it = inProcess.begin();
    for (; it != inProcess.end();) {
        auto &instruction = it->first;
        auto &cyclesToComplete = it->second;
        cyclesToComplete--;
        if (cyclesToComplete == 0) {
            output->verbose(CALL_INFO, 4, 0, "%s: %lu FU:%s Instruction: %s is Complete\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
            it = inProcess.erase(it);
        } else {
            it++;
        }
    }

    if (reservations.empty()) {
        return;
    }
    auto front = reservations.front();
    auto &instruction = front.value();
    // output->verbose(CALL_INFO, 2, 0, "%s: %lu FU:%s Completed Input Cycle for Instruction: %s with Interval: %s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str(),front.getString().c_str());
    if (front.end() == currentCycle) {
        reservations.popFront();
        pe->addBusyCyclesWindow(VEC_DEPTH);
    }
}

bool CinnamonFunctionalUnit::isIntervalReservable(const CinnamonInstructionInterval &interval) {
    return !reservations.hasOverlap(interval);
}

// This function assumes that a reservation can be made. It must be called only after isIntervalAcceptable has been called
void CinnamonFunctionalUnit::addReservation(const CinnamonInstructionInterval &interval) {

    auto instruction = interval.value();
    if (reservations.hasOverlap(interval) == false) {
        reservations.insert(interval);
        return;
    }

    output->fatal(CALL_INFO, -1, "Error: Reservation for interval: %s could not be made. Some thing is terribly wrong\n", interval.getString().c_str());
}

void CinnamonFunctionalUnit::executeCycleBegin(SST::Cycle_t currentCycle) {

    if (currentCycle % 100000 == 0) {
        output->verbose(CALL_INFO, 2, 0, "%s:Heartbeat @ %" PRId64 "00K cycles. FU[%s] Util Cycles: %" PRIu64 "\n", pe->getName().c_str(), currentCycle / (100000), pe->getName().c_str(), stats_.busyCyclesWindow);
        output->verbose(CALL_INFO, 2, 0, "%s:Heartbeat @ %" PRId64 "00K cycles. FU[%s] Issue Cycles: %" PRIu64 "\n", pe->getName().c_str(), currentCycle / (100000), pe->getName().c_str(), stats_.issueCyclesWindow);
        output->flush();
        stats_.busyCyclesWindow = 0;
        stats_.issueCyclesWindow = 0;
    }

    if (consumingCycles != 0) {
        consumingCycles--;
    }

    if (reservations.empty()) {
        return;
    }

    const CinnamonInstructionInterval &interval = reservations.front();
    std::shared_ptr<CinnamonInstruction> instruction = interval.value();

    if (interval.start() < currentCycle) {
        return;
    }

    if (currentCycle < interval.start()) {
        return;
    }

    if (interval.end() < currentCycle) {
        output->verbose(CALL_INFO, 4, 0, "Error: Instruction %s was not issued. Interval: %s. Some thing is terribly wrong\n", instruction->getString().c_str(), interval.getString().c_str());
        assert(0);
        output->fatal(CALL_INFO, -1, "Error: Instruction %s was not issued in interval: %s. Some thing is terribly wrong\n", instruction->getString().c_str(), interval.getString().c_str());
    }

    output->verbose(CALL_INFO, 4, 0, "%s: %lu Executing Instruction: %s with Interval: %s\n",
                    pe->getName().c_str(), currentCycle, instruction->getString().c_str(), interval.getString().c_str());

    if (instruction->allOperandsReady() != true) {
        output->fatal(CALL_INFO, -1, "ERROR: Instruction %s not ready at cycle: %" PRIu64 ".\n", instruction->getString().c_str(), currentCycle);
    }

    if (consumingCycles != 0) {
        output->fatal(CALL_INFO, -1, "ERROR: Instruction %s cannoth be issued at cycle: %" PRIu64 ".\n", instruction->getString().c_str(), currentCycle);
    }

    busyWith.emplace_back(std::make_pair(instruction, latency));
    stats_.issueCycles += (vecDepth);
    stats_.issueCyclesWindow += (vecDepth);
    consumingCycles = (vecDepth);

    inProcess.emplace_back(std::make_pair(instruction, latency + VEC_DEPTH - 1));
}

bool CinnamonFunctionalUnit::okayToFinish() {
    if (reservations.empty() != true || busyWith.empty() != true) {
        return false;
    }
    return true;
}

std::string CinnamonFunctionalUnit::printStats() {

    std::stringstream s;
    s << "Functional Unit: " << name << "\n";
    s << "\tTotal Cycles: " << stats_.totalCycles << "\n";
    s << "\tBusy Cycles: " << stats_.busyCycles << "\n";
    s << "\tIssue Cycles: " << stats_.issueCycles << "\n";
    double utilisation = ((100.0) * stats_.busyCycles) / stats_.totalCycles;
    s << "\tUtilisation %: " << utilisation << "\n";
    double issueRate = ((100.0) * stats_.issueCycles) / stats_.totalCycles;
    s << "\tIssue Rate %: " << issueRate << "\n";
    return s.str();
}

// ####################

CinnamonBaseConversionUnit::CinnamonBaseConversionUnit(CinnamonChip *pe, const BaseConversionRegister::PhysicalID_t phyID, const std::string &name, const uint32_t outputLevel, const uint16_t latency) : pe(pe), phyID(phyID), name(name), latency(latency) {
    output = std::make_shared<SST::Output>(SST::Output(name + "[@p:@l]: ", outputLevel, 0, SST::Output::STDOUT));
}

void CinnamonBaseConversionUnit::executeCycleEnd(SST::Cycle_t currentCycle) {

    if (!busyWith.has_value()) {
        return;
    }

    auto instruction = busyWith.value();
    if (instruction->isCompleted()) {
        output->verbose(CALL_INFO, 4, 0, "%s: %lu BCU:%s Instruction: %s is Ready\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
        instruction->setExecutionComplete();
        busyWith.reset();
        return;
    }
}

void CinnamonBaseConversionUnit::executeCycleBegin(SST::Cycle_t currentCycle) {
    return;
}

bool CinnamonBaseConversionUnit::isBusy() const {
    return busyWith.has_value();
}

void CinnamonBaseConversionUnit::initInstruction(SST::Cycle_t currentCycle, std::shared_ptr<CinnamonBciInstruction> instruction) {
    assert(!busyWith.has_value());
    instruction->setPhyiscalBaseConversionRegister(phyID);
    busyWith = instruction;
    output->verbose(CALL_INFO, 4, 0, "%s: %lu BCU:%s : Initialized with instruction%s\n", pe->getName().c_str(), currentCycle, name.c_str(), instruction->getString().c_str());
}

bool CinnamonBaseConversionUnit::okayToFinish() {
    if (busyWith.has_value() == true) {
        return false;
    }
    return true;
}

} // Namespace Cinnamon
} // Namespace SST
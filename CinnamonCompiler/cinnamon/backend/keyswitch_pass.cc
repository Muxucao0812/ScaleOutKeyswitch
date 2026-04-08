// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include "cinnamon/backend/keyswitch_pass.h"
#include "cinnamon/frontend/program.h"
#include "cinnamon/frontend/term_map.h"

#include <set>
#include <iostream>
#include <tuple>
#include <unordered_map>


namespace CinnamonP {

    FuseAggregationPass::FuseAggregationPass(Program & program) : program(program) {}

    Term::Ptr FuseAggregationPass::makeTermFrom(const Term::Ptr & term, Op op, const std::vector<Term::Ptr> &operands = {}){
        return program.makeTerm(op,term->getType(),term->getScale(),term->getLevel(),term->getEphemeralKey(),term->getPartitionSize(), term->getPartitionId(),operands);

    }

    void FuseAggregationPass::addOperandToRotAccTerm(Term::Ptr & rotAccTerm, Term::Ptr & rotTerm) {
        Term::Ptr op = rotTerm;
        int32_t rotIdx = 0;
        rotAccTerm->addOperand(op);
    }

    bool FuseAggregationPass::isSingleUseRotateOp(const Term::Ptr & term) {
        if(term->numUses() != 1) {
            return false;
        }
        return term->getOp() == Op::RotateLeftConst || term->getOp() == Op::RotateRightConst;
    }
    bool FuseAggregationPass::isAggregationOp(const Term::Ptr & term) {
        return term->getOp() == Op::RotAcc;
    }

void FuseAggregationPass::operator()(Term::Ptr &term) {


    if(term->getOp() == Op::Add) {
        auto operands = term->getOperands();
        assert(operands.size() == 2);
        if(isSingleUseRotateOp(operands[0]) || isSingleUseRotateOp(operands[1])) {
            // auto uses = term->getUses();
            term->setOp(Op::RotAcc);
            // DO DFS and replace all terms
            std::vector<Term::Ptr> checklist;
            for (auto & op: term->getOperands()) {
                checklist.push_back(op);
            }
            std::set<uint64_t> visited;
            while(!checklist.empty()) {
                auto visit = checklist.back();
                checklist.pop_back();
                if(visited.find(visit->index) != visited.end()){
                    continue;
                }
                // assert(visit->getOp() == Op::Add || isSingleUseRotateOp(visit));
                visited.insert(visit->index);
                if(isSingleUseRotateOp(visit)) {
                    assert(visit->numUses() == 1);
                    auto use = visit->getUses()[0];
                    if(use->getOp() == Op::RotAcc){
                        addOperandToRotAccTerm(term,visit);
                        use->eraseOperand(visit);
                        assert(visit->numUses() == 1);
                        continue;
                    }
                    assert(use->getOp() == Op::Add);
                    addOperandToRotAccTerm(term,visit);
                    use->eraseOperand(visit);
                    assert(visit->numUses() == 1);
                    assert(use->numOperands() == 1);
                    auto useOp = use->operandAt(0);
                    use->eraseOperand(useOp);
                    use->replaceAllUsesWith(useOp);
                    assert(use->numUses() == 0);
                    use->setOp(Op::Nop);
                    // continue;
                }
                else if(visit->getOp() == Op::Add){
                    assert(visit->numOperands() == 2);
                    for (auto & op: visit->getOperands()) {
                        checklist.push_back(op);
                    }
                }
            }
            auto rotOperands = term->getOperands();
            std::vector<int32_t> rotIdxs(rotOperands.size());
            for(int i = 0; i < rotOperands.size(); i++) {
                int32_t rotIdx = 0;
                auto rotTerm = rotOperands[i];
                if(isSingleUseRotateOp(rotTerm)){
                    if(rotTerm->getOp() == Op::RotateLeftConst){
                        rotIdx = rotTerm->get<RotationAttribute>();
                    } else if(rotTerm->getOp() == Op::RotateRightConst) {
                        rotIdx = rotTerm->get<RotationAttribute>();
                        rotIdx = -rotIdx;
                    }
                    assert(rotTerm->numOperands() == 1);
                    auto op = rotTerm->operandAt(0); 
                    rotTerm->eraseOperand(op);
                    term->eraseOperand(rotTerm);
                    assert(rotTerm->numUses() == 0);
                    assert(rotTerm->numOperands() == 0);
                    rotTerm->setOp(Op::Nop);
                    rotOperands[i] = op;
                    rotIdxs[i] = rotIdx;
                }
            }
            term->set<MultiRotationAttribute>(rotIdxs);
            term->setOperands(rotOperands);
        }
    }
  }

    HoistInputBroadcastPass::HoistInputBroadcastPass(Program & program) : program(program) {}

    Term::Ptr HoistInputBroadcastPass::makeTermFrom(const Term::Ptr & term, Op op, const std::vector<Term::Ptr> &operands = {}){
        return program.makeTerm(op,term->getType(),term->getScale(),term->getLevel(),term->getEphemeralKey(),term->getPartitionSize(), term->getPartitionId(),operands);

    }

    bool HoistInputBroadcastPass::isRotateTerm(const Term::Ptr & term){
        return term->getOp() == Op::RotateLeftConst || term->getOp() == Op::RotateRightConst;
    }


void HoistInputBroadcastPass::operator()(const Term::Ptr &term) {

    auto uses = term->getUses();
    std::vector<Term::Ptr> rotationUses;
    for(auto & use: uses) {
        if(isRotateTerm(use)) {
            rotationUses.push_back(use);
        }
    }
    if(rotationUses.size() <= 1) {
        return;
    }

    auto rotManyTerm = makeTermFrom(term,Op::HoistInpBroadcast,{term});
    std::vector<int32_t> rotationIndices;
    rotManyTerm->setExtractPossible(true);
    rotManyTerm->setExtractSize(rotationUses.size());
    for(int i = 0; i < rotationUses.size(); i++) {
        // TODO: make extract Vec Here
        auto use = rotationUses.at(i);
        // use->replaceAllUsesWith(extract);
        if(use->getOp() == Op::RotateLeftConst){
            rotationIndices.push_back(use->get<RotationAttribute>());
        } else if(use->getOp() == Op::RotateRightConst){
            int32_t idx = use->get<RotationAttribute>();
            rotationIndices.push_back(-idx);
        } else {
            assert(0);
        }
        // auto extract = makeTermFrom(rotManyTerm,Op::TermInCiphertextVec,{rotManyTerm});
        use->addOperand(rotManyTerm);
        use->eraseOperand(term);
        use->setOp(Op::TermInCiphertextVec);
        use->set<TermIdxInVec>(i);
        // if(use->numUses() == 0) {
        //     use->setOp(Op::Nop);
        // }
    }
    rotManyTerm->set<MultiRotationAttribute>(rotationIndices);
  }

    CommonReceiveEliminatorPass::CommonReceiveEliminatorPass(Program & program) : program(program) {}

    bool CommonReceiveEliminatorPass::isReceiveTerm(const Term::Ptr & term){
        return term->getOp() == Op::Receive;
    }


void CommonReceiveEliminatorPass::operator()(const Term::Ptr &term) {

    auto uses = term->getUses();
    std::vector<Term::Ptr> receiveUses;
    for(auto & use: uses) {
        if(isReceiveTerm(use)) {
            receiveUses.push_back(use);
        }
    }
    if(receiveUses.size() <= 1) {
        return;
    }

    struct PairHash {
    std::size_t operator()(const std::pair<uint32_t, uint32_t> &pair) const {
        return std::hash<uint32_t>()(std::get<0>(pair)) ^
            std::hash<uint32_t>()(std::get<1>(pair));
    }
    };

    struct PairCompare {
    bool operator()(const std::pair<uint32_t,uint32_t> &lhs, const std::pair<uint32_t,uint32_t> &rhs) const {
        return std::get<0>(lhs) == std::get<0>(rhs) && std::get<1>(lhs) == std::get<1>(rhs);
    }
    };
    std::unordered_map<std::pair<uint32_t,uint32_t>,Term::Ptr,PairHash,PairCompare> receiveMap;

    for(auto & use: receiveUses) {
        auto key = std::pair<uint32_t,uint32_t>(use->getPartitionSize(),use->getPartitionId());
        auto it = receiveMap.find(key);
        if(it == receiveMap.end()) {
            receiveMap[key] = use;
            continue;
        }
        auto prev_use = it->second;
        use->replaceAllUsesWith(prev_use);
        use->setOp(Op::Nop);
    }

  }



} // namespace CinnamonP
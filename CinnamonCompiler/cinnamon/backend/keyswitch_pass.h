// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once
#include "cinnamon/frontend/term_map.h"

using namespace Cinnamon;

namespace CinnamonP {
using namespace Frontend;
class FuseAggregationPass {

public:
  FuseAggregationPass(Program &program);
  void operator()(Term::Ptr &term);

private:
  Program &program;
  Term::Ptr makeTermFrom(const Term::Ptr &term, Op op,
                         const std::vector<Term::Ptr> &operands);
  bool isSingleUseRotateOp(const Term::Ptr &term);
  bool isAggregationOp(const Term::Ptr &term);
  void addOperandToRotAccTerm(Term::Ptr &rotAccTerm, Term::Ptr &rotTerm);
};
} // namespace CinnamonP

namespace CinnamonP {
class HoistInputBroadcastPass {

public:
  HoistInputBroadcastPass(Program &program);
  void operator()(const Term::Ptr &term);

private:
  Program &program;
  Term::Ptr makeTermFrom(const Term::Ptr &term, Op op,
                         const std::vector<Term::Ptr> &operands);
  bool isRotateTerm(const Term::Ptr &term);
  bool isAggregationOp(const Term::Ptr &term);
  void addOperandToRotAccTerm(Term::Ptr &rotAccTerm, Term::Ptr &rotTerm);
};
} // namespace CinnamonP

namespace CinnamonP {
class CommonReceiveEliminatorPass {

public:
  CommonReceiveEliminatorPass(Program &program);
  void operator()(const Term::Ptr &term);

private:
  Program &program;
  Term::Ptr makeTermFrom(const Term::Ptr &term, Op op,
                         const std::vector<Term::Ptr> &operands);
  bool isReceiveTerm(const Term::Ptr &term);
  bool isAggregationOp(const Term::Ptr &term);
  void addOperandToRotAccTerm(Term::Ptr &rotAccTerm, Term::Ptr &rotTerm);
};
} // namespace CinnamonP

// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#include "cinnamon/frontend/term.h"
#include "cinnamon/frontend/program.h"
#include <algorithm>
#include <utility>

using namespace std;

namespace Cinnamon {
namespace Frontend {

Term::Term(Op op, Program &program, Type type, Scale_t scale, Level_t level,
           bool ephemeralKey, std::uint8_t partitionSize, uint8_t partitionId)
    : op(op), program(program), index(program.allocateIndex()), type(type),
      level(level), scale(scale), needsRelinearization(false),
      extractPossible(false), extractSize(0), ephemeralKey(ephemeralKey),
      partitionSize(partitionSize), partitionId(partitionId),
      hasReceiveUse(false) {
  program.sources.insert(this);
  program.sinks.insert(this);
}

Term::~Term() {
  for (Ptr &operand : operands) {
    operand->eraseUse(this);
  }
  if (operands.empty()) {
    program.sources.erase(this);
  }
  assert(uses.empty());
  program.sinks.erase(this);
}

void Term::addOperand(const Term::Ptr &term) {
  if (operands.empty()) {
    program.sources.erase(this);
  }
  operands.emplace_back(term);
  term->addUse(this);
}

bool Term::eraseOperand(const Ptr &term) {
  auto iter = find(operands.begin(), operands.end(), term);
  if (iter != operands.end()) {
    term->eraseUse(this);
    operands.erase(iter);
    if (operands.empty()) {
      program.sources.insert(this);
    }
    return true;
  }
  return false;
}

bool Term::replaceOperand(Ptr oldTerm, Ptr newTerm) {
  bool replaced = false;
  for (Ptr &operand : operands) {
    if (operand == oldTerm) {
      operand = newTerm;
      oldTerm->eraseUse(this);
      newTerm->addUse(this);
      replaced = true;
    }
  }
  return replaced;
}

void Term::replaceUsesWithIf(Ptr term, function<bool(const Ptr &)> predicate) {
  auto thisPtr = shared_from_this(); // TODO: avoid this and similar
                                     // unnecessary reference counting
  for (auto &use : getUses()) {
    if (predicate(use)) {
      use->replaceOperand(thisPtr, term);
    }
  }
}

void Term::replaceAllUsesWith(Ptr term) {
  replaceUsesWithIf(term, [](const Ptr &) { return true; });
}

void Term::replaceOtherUsesWith(Ptr term) {
  replaceUsesWithIf(term, [&](const Ptr &use) { return use != term; });
}

void Term::setNeedsRelinearization(bool val) { needsRelinearization = val; }

void Term::setExtractPossible(bool val) { extractPossible = val; }

void Term::setExtractSize(size_t val) {
  if (!extractPossible) {
    throw std::runtime_error(
        "Extract Possible must be true to set extract size");
  }
  extractSize = val;
}

void Term::setHasReceiveUse(bool val) { hasReceiveUse = val; }
Term::Type Term::getType() const { return type; }

std::string Term::getTypeString() const {
  switch (type) {
  case Type::Cipher:
    return std::string("Cipher");
    break;
  case Type::Plain:
    return std::string("Plain");
    break;
  }
  throw std::runtime_error("Invalid Type");
  return "";
}

Term::Scale_t Term::getScale() const { return scale; }
Term::Level_t Term::getLevel() const { return level; };

bool Term::getNeedsRelinearization() const { return needsRelinearization; };

bool Term::getExtractPossible() const { return extractPossible; };

size_t Term::getExtractSize() const { return extractSize; };

bool Term::getEphemeralKey() const { return ephemeralKey; };

uint8_t Term::getPartitionSize() const { return partitionSize; };

uint8_t Term::getPartitionId() const { return partitionId; };

bool Term::getHasReceiveUse() const { return hasReceiveUse; };

void Term::setOperands(vector<Term::Ptr> o) {
  if (operands.empty()) {
    program.sources.erase(this);
  }

  for (auto &operand : operands) {
    operand->eraseUse(this);
  }
  operands = move(o);
  for (auto &operand : operands) {
    operand->addUse(this);
  }

  if (operands.empty()) {
    program.sources.insert(this);
  }
}

Op Term::getOp() const { return op; }

void Term::setOp(Op o) { op = o; }

size_t Term::numOperands() const { return operands.size(); }

Term::Ptr Term::operandAt(size_t i) { return operands.at(i); }

const vector<Term::Ptr> &Term::getOperands() const { return operands; }

size_t Term::numUses() { return uses.size(); }

vector<Term::Ptr> Term::getUses() {
  vector<Term::Ptr> u;
  for (Term *use : uses) {
    u.emplace_back(use->shared_from_this());
  }
  return u;
}

bool Term::isInternal() const {
  return ((operands.size() != 0) && (uses.size() != 0));
}

void Term::addUse(Term *term) {
  if (uses.empty()) {
    program.sinks.erase(this);
  }
  uses.emplace_back(term);
}

bool Term::eraseUse(Term *term) {
  auto iter = find(uses.begin(), uses.end(), term);
  assert(iter != uses.end());
  uses.erase(iter);
  if (uses.empty()) {
    program.sinks.insert(this);
    return true;
  }
  return false;
}

ostream &operator<<(ostream &s, const Term &term) {
  s << term.index << ':' << getOpName(term.op) << '(';
  bool first = true;
  for (const auto &operand : term.getOperands()) {
    if (first) {
      first = false;
    } else {
      s << ',';
    }
    s << operand->index;
  }
  s << ')';
  return s;
}

} // namespace Frontend
} // namespace Cinnamon

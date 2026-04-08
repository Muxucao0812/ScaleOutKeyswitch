// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "cinnamon/frontend/attributes.h"
#include "cinnamon/frontend/ops.h"
#include "cinnamon/frontend/types.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <unordered_set>
#include <vector>

namespace Cinnamon {
namespace Frontend {

class Program;

class Term : public AttributeList, public std::enable_shared_from_this<Term> {
public:
  using Ptr = std::shared_ptr<Term>;
  using Scale_t = std::uint64_t;
  using Level_t = std::uint16_t;

  enum Type { Cipher, Plain };

  // Term(Op opcode, Program &program);
  Term(Op opcode, Program &program, Type type, Scale_t scale, Level_t level,
       bool ephemeralKey, std::uint8_t partitionSize, std::uint8_t partitionId);
  ~Term();

  void addOperand(const Ptr &term);
  bool eraseOperand(const Ptr &term);
  bool replaceOperand(Ptr oldTerm, Ptr newTerm);
  void setOperands(std::vector<Ptr> o);
  std::size_t numOperands() const;
  Ptr operandAt(size_t i);
  const std::vector<Ptr> &getOperands() const;

  void replaceUsesWithIf(Ptr term, std::function<bool(const Ptr &)>);
  void replaceAllUsesWith(Ptr term);
  void replaceOtherUsesWith(Ptr term);
  void setNeedsRelinearization(bool val);
  void setExtractPossible(bool val);
  void setExtractSize(size_t val);
  void setEphemeralKey(bool val);
  void setHasReceiveUse(bool val);

  Type getType() const;
  std::string getTypeString() const;
  Scale_t getScale() const;
  Level_t getLevel() const;
  bool getNeedsRelinearization() const;
  bool getExtractPossible() const;
  size_t getExtractSize() const;
  bool getEphemeralKey() const;
  uint8_t getPartitionSize() const;
  uint8_t getPartitionId() const;
  bool getHasReceiveUse() const;

  std::size_t numUses();
  std::vector<Ptr> getUses();

  bool isInternal() const;

  Program &program;

  Op getOp() const;

  void setOp(Op o);

  // Unique index for this Term in the owning Program. Managed by Program
  // and used to index into TermMap instances.
  std::uint64_t index;

  friend std::ostream &operator<<(std::ostream &s, const Term &term);

private:
  std::vector<Ptr> operands; // use->def chain (unmanaged pointers)
  std::vector<Term *> uses;  // def->use chain (managed pointers)

  Op op;
  Type type;
  Scale_t scale;
  Level_t level;
  bool needsRelinearization;
  bool extractPossible;
  bool ephemeralKey;
  size_t extractSize;

  std::uint8_t partitionSize;
  std::uint8_t partitionId;
  bool hasReceiveUse;

  void addUse(Term *term);
  bool eraseUse(Term *term);
};

} // namespace Frontend
} // namespace Cinnamon

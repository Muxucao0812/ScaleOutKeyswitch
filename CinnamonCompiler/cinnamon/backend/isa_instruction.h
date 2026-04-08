// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <map>
#include <memory>
#include <optional>

#include "cinnamon/backend/limb.h"

namespace Cinnamon {
namespace Backend {

class Register {
public:
  enum RegType { None, Vector, Scalar, Bcu };

private:
  uint32_t id_;
  uint32_t bcu_id_;
  RegType reg_type_;
  bool is_dead_;
  friend std::ostream &operator<<(std::ostream &s, const Register &reg);

public:
  Register()
      : id_(-1), bcu_id_(-1), reg_type_(RegType::None), is_dead_(false){};
  Register(uint32_t id, RegType type)
      : id_(id), bcu_id_(-1), reg_type_(type), is_dead_(false) {
    assert(reg_type_ == RegType::Vector || reg_type_ == RegType::Scalar);
  };
  Register(uint32_t bcu_id, uint32_t id)
      : id_(id), bcu_id_(bcu_id), reg_type_(RegType::Bcu), is_dead_(false){};

  uint32_t id() const { return id_; }

  RegType reg_type() const { return reg_type_; }

  bool is_bcor() const { return reg_type_ == RegType::Bcu; }

  void mark_dead() { is_dead_ = true; }

  bool is_dead() const { return is_dead_; }
};

struct RegisterHash {
  std::size_t operator()(const Register &reg) const {
    uint32_t hash_id = (reg.reg_type() << 28) | reg.id();
    return std::hash<uint32_t>()(hash_id);
  }
};

struct RegisterCompare {
  bool operator()(const Register &lhs, const Register &rhs) const {
    return lhs.id() == rhs.id() && lhs.reg_type() == rhs.reg_type();
  }
};

std::ostream &operator<<(std::ostream &s, const Register &reg) {
  if (reg.is_bcor()) {
    s << "b" << reg.bcu_id_ << "{" << reg.id_ << "}";
  } else if (reg.reg_type() == Register::RegType::Scalar) {
    s << "s" << reg.id_;
  } else {
    s << "r" << reg.id_;
  }
  if (reg.is_dead_) {
    s << "[X]";
  }
  return s;
}

class BaseConversionUnit {
  uint32_t id_;
  std::shared_ptr<std::unordered_map<LimbIndexType, Register>>
      converted_bases_map_;
  std::shared_ptr<uint32_t> writes_;
  std::shared_ptr<std::set<LimbIndexType>> source_base_indices_;
  std::shared_ptr<std::set<LimbIndexType>> dest_base_indices_;
  // uint32_t limbs_contained_;
  friend std::ostream &operator<<(std::ostream &s,
                                  const BaseConversionUnit &bcu);

public:
  BaseConversionUnit()
      : id_(-1), writes_(nullptr), converted_bases_map_(nullptr) {}
  BaseConversionUnit(uint32_t id)
      : id_(id), writes_(nullptr), converted_bases_map_(nullptr) {}

  void initialise(const std::set<LimbIndexType> &dest_base_indices,
                  const std::set<LimbIndexType> &dest_shares) {
    writes_ = std::make_shared<uint32_t>(0);
    converted_bases_map_ =
        std::make_shared<std::unordered_map<LimbIndexType, Register>>();
    source_base_indices_ = std::make_shared<std::set<LimbIndexType>>();
    dest_base_indices_ = std::make_shared<std::set<LimbIndexType>>();
    int i = 0;
    for (auto &base : dest_base_indices) {
      (*converted_bases_map_)[base] = Register(id_, i);
      i++;
    }
    // *source_base_indices_ = source_base_indices;
  }

  Register read_register(const Limb &limb) const {
    auto limb_idx = limb.limb_idx();
    auto reg = converted_bases_map_->at(limb_idx);
    dest_base_indices_->insert(limb_idx);
    return reg;
  }

  void free_register(const Limb &limb) {
    auto limb_idx = limb.limb_idx();
    converted_bases_map_->erase(limb_idx);
  }

  bool is_free() { return converted_bases_map_->empty(); }

  void write(const LimbIndexType limb_index) {
    *writes_ = *writes_ + 1;
    source_base_indices_->insert(limb_index);
  }

  void clear() {
    writes_ = nullptr;
    converted_bases_map_ = nullptr;
    source_base_indices_ = nullptr;
    dest_base_indices_ = nullptr;
  }

  friend class BaseConversionUnitInitialiseISAInstruction;
};

std::ostream &operator<<(std::ostream &s, const BaseConversionUnit &bcu) {
  s << "B" << bcu.id_;
  return s;
}

class PolynomialRegisterGroup {
  std::vector<Register> registers_;
  uint64_t next_use_;
  // std::shared_ptr<std::set<LimbIndexType>> source_base_indices_;
  friend std::ostream &operator<<(std::ostream &s,
                                  const BaseConversionUnit &bcu);

public:
  PolynomialRegisterGroup() : next_use_(-1) {}
  PolynomialRegisterGroup(const std::vector<Register> &&registers)
      : registers_(registers), next_use_(-1) {}

  const std::vector<Register> &registers() const { return registers_; }

  std::vector<Register> &registers() { return registers_; }

  void set_next_use(uint64_t next_use) { next_use_ = next_use; }

  friend class ResolveInitialiseISAInstruction;
};

std::ostream &operator<<(std::ostream &s, const PolynomialRegisterGroup &prg) {
  s << "{";
  auto registers = prg.registers();
  auto it = registers.begin();
  s << *it;
  it++;
  for (; it != registers.end(); it++) {
    s << ", " << *it;
  }
  s << "}";
  return s;
}

// Cinnamon ISA Instructions
class ISAInstruction {
public:
  enum OpCode {
    Add,
    AdS, // Scalar add
    Sub,
    SuS, // Scalar subtract
    Neg,
    MuP, // Plaintext multiply
    MuS, // Scalar multiply
    Joi, // Join
    Mul,
    Rot,
    Con,
    Ntt,
    Int,
    Bco,
    Pip1,
    SuD, // subtract and divide by the polynomial base
    Dis, // Distrbute globally
    Rcv, // Receive from Global Distributor
    Inp, // Input
    Mov, // Move
    Rec,
    Snd,
    Pip7,
    Pip6,
    Load,
    Store,
    EvkGen,
    BcoInit,
    RsvInit,
    Rsv,
    Mod

  };

  virtual std::vector<Register *> dests() = 0;
  virtual std::vector<Register *> srcs() = 0;
  virtual std::string ppOp() const = 0;

  ISAInstruction::OpCode opcode() const { return op_; };
  virtual bool is_pl_instruction() const { return false; }
  virtual bool is_join_instruction() const { return false; }

protected:
  ISAInstruction(OpCode op, LimbIndexType base) : op_(op), base_(base){};
  ISAInstruction(){};
  OpCode op_;
  LimbIndexType base_;
};

class LoadISAInstruction : public ISAInstruction {
  Register dest_;
  Limb src_;
  bool is_scalar_;
  bool free_src_from_memory_;

public:
  LoadISAInstruction(OpCode op, const Register &dest, const Limb &src,
                     const bool is_scalar)
      : dest_(dest), src_(src), is_scalar_(is_scalar),
        free_src_from_memory_(false), ISAInstruction(op, 0) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({&dest_});
  }

  std::vector<Register *> srcs() override { return std::vector<Register *>(); }

  const Limb *load_src() { return &src_; }

  void set_free_load_val_from_memory() { free_src_from_memory_ = true; }

  std::string ppOp() const override {
    std::stringstream s;
    switch (op_) {
    case OpCode::Load: {
      if (is_scalar_) {
        s << "loas ";
      } else {
        s << "load ";
      }
      break;
    }
    case OpCode::EvkGen: {
      s << "evg ";
    };
    }
    s << dest_ << ": " << src_;
    if (free_src_from_memory_) {
      s << "{F}";
    }

    return s.str();
  }
};

class StoreISAInstruction : public ISAInstruction {
  Register src_;
  Limb dest_;
  bool
      is_spill_; // If the instruction is a spill or programmer instructed store

public:
  StoreISAInstruction(const Limb &dest, const Register &src,
                      const bool is_spill = false)
      : dest_(dest), src_(src), is_spill_(is_spill),
        ISAInstruction(OpCode::Store, 0) {}

  std::vector<Register *> dests() override { return std::vector<Register *>(); }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({&src_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    if (is_spill_) {
      s << "spill ";
    } else {
      s << "store ";
    }
    s << src_ << ": " << dest_;
    return s.str();
  }
};

class ReceiveInputISAInstruction : public ISAInstruction {
  Register dest_;
  Limb src_;

public:
  ReceiveInputISAInstruction(const Register &dest, const Limb &src,
                             const LimbIndexType base)
      : dest_(dest), src_(src), ISAInstruction(OpCode::Rec, base) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({&dest_});
  }

  std::vector<Register *> srcs() override { return std::vector<Register *>(); }

  std::string ppOp() const override {
    std::stringstream s;
    s << "rec " << dest_ << ": " << src_ << " | " << base_;
    return s.str();
  }
};

class SendISAInstruction : public ISAInstruction {
  Register src_;
  Limb dest_;

public:
  SendISAInstruction(const Register &src, const Limb &dest,
                     const LimbIndexType base)
      : src_(src), dest_(dest), ISAInstruction(OpCode::Snd, base) {}

  std::vector<Register *> dests() override { return std::vector<Register *>(); }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({&src_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "snd " << src_ << ": " << dest_ << " | " << base_;
    return s.str();
  }
};

class UnOpISAInstruction : public ISAInstruction {
  Register src_;
  Register dest_;
  int32_t rot_idx_;

public:
  UnOpISAInstruction(const OpCode op, const Register &dest, const Register &src,
                     LimbIndexType base)
      : dest_(dest), src_(src), rot_idx_(0), ISAInstruction(op, base) {}

  UnOpISAInstruction(const OpCode op, const Register &dest, const Register &src,
                     const int32_t rot_idx, LimbIndexType base)
      : dest_(dest), src_(src), rot_idx_(rot_idx), ISAInstruction(op, base) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({&dest_});
  }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({&src_});
  }

  std::string ppOp() const override {

    std::stringstream s;
    switch (op_) {
    case OpCode::Neg:
      s << "neg";
      break;
    case OpCode::Rot:
      s << "rot " << rot_idx_;
      break;
    case OpCode::Con:
      s << "con";
      break;
    case OpCode::Int:
      s << "int";
      break;
    case OpCode::Ntt:
      s << "ntt";
      break;
    case OpCode::Mov:
      s << "mov";
      break;
    }
    s << " " << dest_ << ": " << src_ << " | " << base_;
    return s.str();
  }
};

class BinOpISAInstruction : public ISAInstruction {
  Register src1_, src2_;
  Register dest_;

public:
  BinOpISAInstruction(const OpCode op, const Register &dest,
                      const Register &src1, const Register &src2,
                      LimbIndexType base)
      : dest_(dest), src1_(src1), src2_(src2), ISAInstruction(op, base) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({&dest_});
  }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({&src1_, &src2_});
  }

  std::string ppOp() const override {

    std::stringstream s;
    switch (op_) {
    case OpCode::Add:
      s << "add";
      break;
    case OpCode::AdS:
      s << "ads";
      break;
    case OpCode::Sub:
      s << "sub";
      break;
    case OpCode::SuS:
      s << "sus";
      break;
    case OpCode::Mul:
      s << "mul";
      break;
    case OpCode::MuP:
      s << "mup";
      break;
    case OpCode::MuS:
      s << "mus";
      break;
    case OpCode::SuD:
      s << "sud";
      break;
    }
    s << " " << dest_ << ": " << src1_ << ", " << src2_ << " | " << base_;
    return s.str();
  }
};

class PlISAInstruction1 : public ISAInstruction {
  // using OpCode = SSAInstruction::OpCode;
  BaseConversionUnit bco_dest_;
  Register src1_;
  bool intt_required_ = true;

public:
  PlISAInstruction1(const BaseConversionUnit &bco_dest, const Register &src1,
                    LimbIndexType base, const bool intt_required)
      : bco_dest_(bco_dest), src1_(src1), intt_required_(intt_required),
        ISAInstruction(OpCode::Pip1, base) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({});
  }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    if (intt_required_) {
      s << "pl1 ";
    } else {
      s << "bcw ";
    }
    s << bco_dest_ << ": " << src1_ << " | " << base_;
    return s.str();
  }

  bool is_pl_instruction() const override { return true; }
};

class JoinGlobalISAInstruction : public ISAInstruction {

private:
  std::pair<Register, bool> dest_;
  std::pair<Register, bool> src1_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  JoinGlobalISAInstruction(const Register &src1, const LimbIndexType base,
                           const uint64_t sync_id, const uint64_t sync_size)
      : dest_(std::make_pair(Register(), false)),
        src1_(std::make_pair(src1, true)), sync_id_(sync_id),
        sync_size_(sync_size), ISAInstruction(OpCode::Joi, base) {}
  JoinGlobalISAInstruction(const Register &dest, const Register &src1,
                           const LimbIndexType base, const uint64_t sync_id,
                           const uint64_t sync_size)
      : dest_(std::make_pair(dest, true)), src1_(std::make_pair(src1, true)),
        sync_id_(sync_id), sync_size_(sync_size),
        ISAInstruction(OpCode::Joi, base) {}
  JoinGlobalISAInstruction(const LimbIndexType base, const uint64_t sync_id,
                           const uint64_t sync_size)
      : dest_(std::make_pair(Register(), false)),
        src1_(std::make_pair(Register(), false)), sync_id_(sync_id),
        sync_size_(sync_size), ISAInstruction(OpCode::Joi, base) {}

  std::vector<Register *> dests() override {
    if (dest_.second == true) {
      return std::vector<Register *>({&dest_.first});
    } else {
      return std::vector<Register *>({});
    }
  }

  std::vector<Register *> srcs() override {
    if (src1_.second == true) {
      return std::vector<Register *>({&src1_.first});
    } else {
      return std::vector<Register *>({});
    }
  }

  std::string ppOp() const override {
    std::stringstream s;
    // s << "@ " << start_at_time_ << " :: ";
    s << "joi @ " << sync_id_ << ":" << sync_size_ << " ";
    if (dest_.second == true) {
      s << dest_.first;
    }
    s << ": ";
    if (src1_.second == true) {
      s << src1_.first;
    }
    s << " | " << base_;
    return s.str();
  }

  bool is_join_instruction() const override { return true; }
};

class RecvISAInstruction : public ISAInstruction {
  Register dest_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  RecvISAInstruction(const Register &dest, const LimbIndexType base,
                     const uint64_t sync_id, const uint64_t sync_size)
      : dest_(dest), sync_id_(sync_id), sync_size_(sync_size),
        ISAInstruction(OpCode::Rcv, base) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({&dest_});
  }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({});
  }

  std::string ppOp() const override {
    std::stringstream s;
    // s << "@ " << start_at_time_ << " :: ";
    s << "rcv @ " << sync_id_ << ":" << sync_size_ << " " << dest_ << ": ";
    return s.str();
  }
};

class DistISAInstruction : public ISAInstruction {
  Register src1_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  DistISAInstruction(const Register &src1, const LimbIndexType base,
                     const uint64_t sync_id, const uint64_t sync_size)
      : src1_(src1), sync_id_(sync_id), sync_size_(sync_size),
        ISAInstruction(OpCode::Dis, base) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({});
  }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "dis @ " << sync_id_ << ":" << sync_size_ << " "
      << ": " << src1_;
    return s.str();
  }
};

class BaseConversionUnitInitialiseISAInstruction : public ISAInstruction {
  BaseConversionUnit bco_unit_;
  std::set<LimbIndexType> dest_base_indices_;
  std::shared_ptr<std::set<LimbIndexType>> source_base_indices_;

public:
  BaseConversionUnitInitialiseISAInstruction(
      const BaseConversionUnit &bco_unit,
      const std::set<LimbIndexType>
          &dest_base_indices) //, const std::shared_ptr<std::set<LimbIndexType>>
                              //& source_base_indices)
      : bco_unit_(bco_unit), dest_base_indices_(dest_base_indices),
        source_base_indices_(bco_unit_.source_base_indices_),
        ISAInstruction(OpCode::BcoInit, 0) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({});
  }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "bci " << bco_unit_ << ": [";
    int count = 0;
    for (const LimbIndexType limb : dest_base_indices_) {
      if (count != 0) {
        s << ",";
      }
      s << limb;
      count++;
    }
    s << "], [";
    count = 0;
    for (const LimbIndexType limb : *source_base_indices_) {
      if (count != 0) {
        s << ",";
      }
      s << limb;
      count++;
    }
    s << "]";
    return s.str();
  }

  bool is_pl_instruction() const override { return false; }
};

class ResolveInitialiseISAInstruction : public ISAInstruction {
  PolynomialRegisterGroup prg_;
  std::shared_ptr<std::set<LimbIndexType>> source_base_indices_;

public:
  ResolveInitialiseISAInstruction(
      const PolynomialRegisterGroup
          &prg) //, const std::shared_ptr<std::set<LimbIndexType>> &
                // source_base_indices)
      : prg_(prg), ISAInstruction(OpCode::RsvInit, 0) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({});
  }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "rsi " << prg_; // << ": [";
    // int count = 0;
    // for (const LimbIndexType limb: *source_base_indices_){
    //   if(count != 0){
    //     s << ",";
    //   }
    //   s << limb;
    //   count++;
    // }
    // s << "]";
    return s.str();
  }

  bool is_pl_instruction() const override { return false; }
};

class ResolveISAInstruction : public ISAInstruction {
  // using OpCode = SSAInstruction::OpCode;
  PolynomialRegisterGroup dest_;
  Register src1_;
  std::set<LimbIndexType> resolve_base_indices_;

public:
  ResolveISAInstruction(const PolynomialRegisterGroup &dest,
                        const Register &src1,
                        const std::set<LimbIndexType> &resolve_base_indices,
                        LimbIndexType base)
      : dest_(dest), src1_(src1), resolve_base_indices_(resolve_base_indices),
        ISAInstruction(OpCode::Rsv, base) {}

  std::vector<Register *> dests() override {
    std::vector<Register *> ret;
    for (auto &reg : dest_.registers()) {
      ret.push_back(&reg);
    }
    return ret;
  }

  std::vector<Register *> srcs() override {
    return std::vector<Register *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "rsv " << dest_ << ": " << src1_;

    s << ": [";
    int count = 0;
    for (const LimbIndexType limb : resolve_base_indices_) {
      if (count != 0) {
        s << ",";
      }
      s << limb;
      count++;
    }
    s << "]";

    s << " | " << base_;
    return s.str();
  }

  bool is_pl_instruction() const override { return false; }
};

class ModISAInstruction : public ISAInstruction {
  // using OpCode = SSAInstruction::OpCode;
  Register dest_;
  PolynomialRegisterGroup src1_;

public:
  ModISAInstruction(const Register &dest, const PolynomialRegisterGroup &src1,
                    LimbIndexType base)
      : dest_(dest), src1_(src1), ISAInstruction(OpCode::Mod, base) {}

  std::vector<Register *> dests() override {
    return std::vector<Register *>({&dest_});
  }

  std::vector<Register *> srcs() override {
    std::vector<Register *> ret;
    for (auto &reg : src1_.registers()) {
      ret.push_back(&reg);
    }
    return ret;
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "mod " << dest_ << ": " << src1_ << " | " << base_;
    return s.str();
  }

  bool is_pl_instruction() const override { return false; }
};

template <typename T>
using RegisterMapType =
    std::unordered_map<Register, T, RegisterHash, RegisterCompare>;

} // namespace Backend
} // namespace Cinnamon
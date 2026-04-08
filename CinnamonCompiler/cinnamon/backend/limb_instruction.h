// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include "fmt/core.h"
#include <memory>
#include <vector>

#include "cinnamon/backend/isa_instruction.h"
#include "cinnamon/backend/limb.h"
#include "cinnamon/backend/terms.h"

namespace Cinnamon {
namespace Backend {

// Limb IR Instruction in Cinnamon
class LimbInstruction {
public:
  enum OpCode {
    Joi, // Join
    Add,
    Sub,
    Neg,
    JoL, // Local Join
    MuP, // Plaintext multiply
    Syn, // Sync
    Mul,
    Rot,
    Con,
    Ntt,
    Int,
    Bco,
    Div,
    Pip1,
    Pip2,
    Pip3,
    Pip4,
    Pip5,
    SuD, // subtract and divide by the polynomial base
    Dis, // Distrbute globally
    Rcv, // Receive from Global Distributor
    Drm, // Distibute/Receive and Move
    Inp, // Input
    Mov, // Move
    Rec, // Recieve
    Snd, // Recieve
    Pip6,
    Pip7,
    Pip8,
    Rsv,
    Mod

  };
  // using OpCode = SSAInstruction::OpCode;

  virtual std::vector<Limb *> dests() = 0;
  virtual std::vector<Limb *> srcs() = 0;
  virtual std::vector<Polynomial *> polynomial_dests() = 0;
  virtual std::vector<Polynomial *> polynomial_srcs() = 0;
  virtual std::string ppOp() const = 0;
  // virtual std::shared_ptr<LimbInstruction> clone() const = 0;
  // virtual void set_limbs() = 0;
  // virtual bool verify_operands() const = 0;

  LimbInstruction::OpCode opcode() const { return op_; };
  virtual bool is_pl_instruction() const { return false; }
  virtual bool is_join_instruction() const { return false; }
  LimbIndexType limb_idx() const { return limb_; }

  virtual Polynomial *base_conversion_dest() { return nullptr; }

  // virtual std::shared_ptr<ISAInstruction> get_register_instruction(const
  // LimbMap<Register> & limb_map_table, const std::unordered_map<TermIndexType,
  // BaseConversionUnit> & bcu_map_table ) const {return nullptr; };
  virtual std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const = 0;

protected:
  LimbInstruction(OpCode op, const LimbIndexType limb)
      : op_(op) /*, start_at_time_(-1)*/, limb_(limb){};
  LimbInstruction(){};
  OpCode op_;
  LimbIndexType limb_;
};

class InpLimbInstruction : public LimbInstruction {
  Limb dest_;

public:
  InpLimbInstruction(const Limb &dest, const LimbIndexType limb)
      : dest_(dest), LimbInstruction(OpCode::Inp, limb) {
    assert(dest_.is_input());
  }

  std::vector<Limb *> dests() override { return std::vector<Limb *>({&dest_}); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>(); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "inp";
    s << " ";
    s << dest_;
    return s.str();
  }

  virtual std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {
    return nullptr;
  };
};
class ReceiveInputLimbInstruction : public LimbInstruction {
  Limb dest_;

public:
  ReceiveInputLimbInstruction(const Limb &dest, const LimbIndexType limb)
      : dest_(dest), LimbInstruction(OpCode::Rec, limb) {
    assert(dest_.is_receive());
  }

  std::vector<Limb *> dests() override { return std::vector<Limb *>({&dest_}); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>(); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "rec";
    s << " ";
    s << dest_;
    return s.str();
  }

  virtual std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {
    auto dest_reg = limb_map_table.at(dest_);
    return std::make_shared<ReceiveInputISAInstruction>(dest_reg, dest_, limb_);
  };
};

class SendLimbInstruction : public LimbInstruction {
  Limb src_;

public:
  SendLimbInstruction(const Limb &src, const LimbIndexType limb)
      : src_(src), LimbInstruction(OpCode::Snd, limb) {
    assert(src_.is_send());
  }

  std::vector<Limb *> dests() override { return std::vector<Limb *>(); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>({&src_}); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "snd : ";
    s << src_;
    return s.str();
  }

  virtual std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {
    auto src_reg = limb_map_table.at(src_);
    return std::make_shared<SendISAInstruction>(src_reg, src_, limb_);
  };
};

class UnOpLimbInstruction : public LimbInstruction {
  // using OpCode = SSAInstruction::OpCode;
  Limb dest_;
  Limb src1_;
  int32_t rot_idx_;

public:
  UnOpLimbInstruction(OpCode op, const Limb &dest, const Limb &src1,
                      const LimbIndexType limb)
      : dest_(dest), src1_(src1), rot_idx_(0), LimbInstruction(op, limb) {
    switch (op) {
    case OpCode::Neg:
      break;
    case OpCode::Int:
      break;
    case OpCode::Ntt:
      break;
    case OpCode::Mov:
      break;
    case OpCode::Con:
      break;
    default:
      throw std::runtime_error("Invalid OpCode for Unary op: " + op);
    }
  }

  UnOpLimbInstruction(OpCode op, const Limb &dest, const Limb &src1,
                      const int32_t rot_idx, const LimbIndexType limb)
      : dest_(dest), src1_(src1), rot_idx_(rot_idx),
        LimbInstruction(OpCode::Rot, limb) {
    switch (op) {
    case OpCode::Rot:
      break;
    default:
      throw std::runtime_error("Invalid OpCode for Unary op: " + op);
    }

    if (rot_idx == 0) {
      op_ = OpCode::Mov;
    }
  }

  std::vector<Limb *> dests() override { return std::vector<Limb *>({&dest_}); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>({&src1_}); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  Limb *dest() { return &dest_; }

  Limb *src() { return &src1_; }

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
      s << "con " << rot_idx_;
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
    s << " " << dest_ << ": " << src1_;
    return s.str();
  }

  ISAInstruction::OpCode get_register_instruction_opcode() const {
    switch (op_) {
    case Neg: {
      return ISAInstruction::OpCode::Neg;
      break;
    }
    case Rot: {
      return ISAInstruction::OpCode::Rot;
      break;
    }
    case Con: {
      return ISAInstruction::OpCode::Con;
      break;
    }
    case Ntt: {
      return ISAInstruction::OpCode::Ntt;
      break;
    }
    case Int: {
      return ISAInstruction::OpCode::Int;
      break;
    }
    case Mov: {
      return ISAInstruction::OpCode::Mov;
      break;
    }
    case Pip1: {
      return ISAInstruction::OpCode::Pip1;
      break;
    }
    default:
      assert(0);
    }
    assert(0);
    return ISAInstruction::Add;
  }

  std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {

    auto reg_op = get_register_instruction_opcode();
    auto dest_reg = limb_map_table.at(dest_);
    Register src1_reg;
    if (src1_.is_bcor()) {
      src1_reg = bcu_map_table.at(src1_.term_idx()).read_register(src1_);
    } else {
      src1_reg = limb_map_table.at(src1_);
    }
    if (op_ == OpCode::Rot) {
      return std::make_shared<UnOpISAInstruction>(reg_op, dest_reg, src1_reg,
                                                  rot_idx_, limb_);
    }

    return std::make_shared<UnOpISAInstruction>(reg_op, dest_reg, src1_reg,
                                                limb_);
  };
};

class BinOpLimbInstruction : public LimbInstruction {
  Limb dest_;
  Limb src1_;
  Limb src2_;

public:
  BinOpLimbInstruction(OpCode op, const Limb &dest, const Limb &src1,
                       const Limb &src2, const LimbIndexType limb)
      : dest_(dest), src1_(src1), src2_(src2), LimbInstruction(op, limb) {
    switch (op) {

    case OpCode::Add:
    case OpCode::Sub:
    case OpCode::MuP:;
    case OpCode::Mul:
    case OpCode::SuD:
      break;
    default:
      throw std::runtime_error("Invalid OpCode for Unary op: " + op);
    }
  }

  std::vector<Limb *> dests() override { return std::vector<Limb *>({&dest_}); }

  std::vector<Limb *> srcs() override {
    return std::vector<Limb *>({&src1_, &src2_});
  }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    switch (op_) {
    case OpCode::Add:
      s << "add";
      break;
    case OpCode::Sub:
      s << "sub";
      break;
    case OpCode::MuP:
      s << "mup";
      break;
    case OpCode::Mul:
      s << "mul";
      break;
    case OpCode::SuD:
      s << "sud";
      break;
    }
    s << " " << dest_ << ": " << src1_ << ", " << src2_;

    return s.str();
  }

  ISAInstruction::OpCode get_register_instruction_opcode() const {
    switch (op_) {
    case Add: {
      assert(!src1_.is_scalar());
      if (src2_.is_scalar()) {
        return ISAInstruction::OpCode::AdS;
        break;
      } else {
        return ISAInstruction::OpCode::Add;
        break;
      }
    }
    case Sub: {
      assert(!src1_.is_scalar());
      if (src2_.is_scalar()) {
        return ISAInstruction::OpCode::SuS;
        break;
      } else {
        return ISAInstruction::OpCode::Sub;
        break;
      }
    }
    case MuP: {
      assert(!src1_.is_scalar());
      if (src2_.is_scalar()) {
        return ISAInstruction::OpCode::MuS;
        break;
      } else {
        return ISAInstruction::OpCode::MuP;
        break;
      }
    }
    case Mul: {
      return ISAInstruction::OpCode::Mul;
      break;
    }
    case SuD: {
      return ISAInstruction::OpCode::SuD;
      break;
    }
    default:
      assert(0);
    }

    // Unreachable...
    assert(0);
    return ISAInstruction::OpCode::Add;
  }

  std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {

    auto reg_op = get_register_instruction_opcode();
    auto dest_reg = limb_map_table.at(dest_);
    auto src1_reg = limb_map_table.at(src1_);
    Register src2_reg;
    if (src2_.is_bcor()) {
      src2_reg = bcu_map_table.at(src2_.term_idx()).read_register(src2_);
    } else {
      src2_reg = limb_map_table.at(src2_);
    }
    return std::make_shared<BinOpISAInstruction>(reg_op, dest_reg, src1_reg,
                                                 src2_reg, limb_);
  };
};

class PlLimbInstruction1 : public LimbInstruction {
  // using OpCode = SSAInstruction::OpCode;
  Polynomial bco_dest_;
  Limb src1_;
  bool intt_required_ = true;

public:
  PlLimbInstruction1(const Polynomial &bco_dest, const Limb &src1,
                     const LimbIndexType limb, const bool intt_required)
      : bco_dest_(bco_dest), src1_(src1), intt_required_(intt_required),
        LimbInstruction(OpCode::Pip1, limb) {
    assert(bco_dest_.is_bcor());
  }

  std::vector<Limb *> dests() override { return std::vector<Limb *>({}); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>({&src1_}); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    // s << "@ " << start_at_time_ << " :: ";
    s << "pl1 " << bco_dest_ << ": " << src1_;
    return s.str();
  }

  bool is_pl_instruction() const override { return true; }

  std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {

    BaseConversionUnit bco_dest_reg = bcu_map_table.at(bco_dest_.term_idx());
    auto src1_reg = limb_map_table.at(src1_);
    // auto src2_reg = limb_map_table.at(src2_);
    return std::make_shared<PlISAInstruction1>(bco_dest_reg, src1_reg, limb_,
                                               intt_required_);
  };

  Polynomial *base_conversion_dest() override { return &bco_dest_; }
};

class JoinGlobalLimbInstruction : public LimbInstruction {

private:
  Limb dest_;
  Limb src1_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  JoinGlobalLimbInstruction(const LimbIndexType limb, const uint64_t sync_id,
                            const uint64_t sync_size)
      : sync_id_((sync_id << 16) + limb), sync_size_(sync_size),
        LimbInstruction(OpCode::Joi, limb) {}
  JoinGlobalLimbInstruction(const Limb &src1, const LimbIndexType limb,
                            const uint64_t sync_id, const uint64_t sync_size)
      : src1_(src1), sync_id_((sync_id << 16) + limb), sync_size_(sync_size),
        LimbInstruction(OpCode::Joi, limb) {}
  JoinGlobalLimbInstruction(const Limb &dest, const Limb &src1,
                            const LimbIndexType limb, const uint64_t sync_id,
                            const uint64_t sync_size)
      : dest_(dest), src1_(src1), sync_id_((sync_id << 16) + limb),
        sync_size_(sync_size), LimbInstruction(OpCode::Joi, limb) {}

  std::vector<Limb *> dests() override {
    if (dest_.term_share() == nullptr || dest_.term() == nullptr) {
      return std::vector<Limb *>({});
    } else {
      return std::vector<Limb *>({&dest_});
    }
  }

  std::vector<Limb *> srcs() override {
    if (src1_.term_share() == nullptr || src1_.term() == nullptr) {
      return std::vector<Limb *>({});
    } else {
      return std::vector<Limb *>({&src1_});
    }
  }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    // s << "@ " << start_at_time_ << " :: ";
    s << "joi @ " << sync_id_ << ":" << sync_size_ << " ";
    if (dest_.term_share() != nullptr && dest_.term() != nullptr) {
      s << dest_;
    }
    s << ": ";
    if (src1_.term_share() != nullptr && src1_.term() != nullptr) {
      s << src1_;
    }
    return s.str();
  }

  bool is_join_instruction() const override { return true; }

  std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {

    if (dest_.term_share() != nullptr && dest_.term() != nullptr) {
      Register src1_reg = limb_map_table.at(src1_);
      Register dest_reg = limb_map_table.at(dest_);
      return std::make_shared<JoinGlobalISAInstruction>(
          dest_reg, src1_reg, limb_, sync_id_, sync_size_);
    }
    if (src1_.term_share() != nullptr && src1_.term() != nullptr) {
      Register src1_reg = limb_map_table.at(src1_);
      return std::make_shared<JoinGlobalISAInstruction>(src1_reg, limb_,
                                                        sync_id_, sync_size_);
    }
    return std::make_shared<JoinGlobalISAInstruction>(limb_, sync_id_,
                                                      sync_size_);
  };
};

class RecvLimbInstruction : public LimbInstruction {
  // using OpCode = SSAInstruction::OpCode;
  Limb dest_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  RecvLimbInstruction(const Limb &dest, const LimbIndexType limb,
                      const uint64_t sync_id, const uint64_t sync_size)
      : dest_(dest), sync_id_((sync_id << 16) + limb), sync_size_(sync_size),
        LimbInstruction(OpCode::Rcv, limb) {}

  std::vector<Limb *> dests() override { return std::vector<Limb *>({&dest_}); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>({}); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    // s << "@ " << start_at_time_ << " :: ";
    s << "rcv @ " << sync_id_ << ":" << sync_size_ << " " << dest_ << ": ";
    return s.str();
  }

  std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {

    Register dest_reg = limb_map_table.at(dest_);
    return std::make_shared<RecvISAInstruction>(dest_reg, limb_, sync_id_,
                                                sync_size_);
  };
};

class DistLimbInstruction : public LimbInstruction {
  // using OpCode = SSAInstruction::OpCode;
  Limb src1_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  DistLimbInstruction(const Limb &src1, const LimbIndexType limb,
                      const uint64_t sync_id, const uint64_t sync_size)
      : src1_(src1), sync_id_((sync_id << 16) + limb), sync_size_(sync_size),
        LimbInstruction(OpCode::Dis, limb) {}

  std::vector<Limb *> dests() override { return std::vector<Limb *>({}); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>({&src1_}); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "dis @ " << sync_id_ << ":" << sync_size_ << " "
      << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {

    Register src1_reg = limb_map_table.at(src1_);
    return std::make_shared<DistISAInstruction>(src1_reg, limb_, sync_id_,
                                                sync_size_);
  };
};

class ResolveLimbInstruction : public LimbInstruction {
  // using OpCode = SSAInstruction::OpCode;
  Polynomial dest_;
  Limb src1_;

public:
  ResolveLimbInstruction(const Polynomial &dest, const Limb &src1,
                         const LimbIndexType limb)
      : dest_(dest), src1_(src1), LimbInstruction(OpCode::Rsv, limb) {
    // assert(dest_.is_bcor());
  }

  std::vector<Limb *> dests() override { return std::vector<Limb *>({}); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>({&src1_}); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "rsv " << dest_ << ": " << src1_;
    return s.str();
  }

  bool is_pl_instruction() const override { return false; }

  std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {

    PolynomialRegisterGroup dest_prg =
        polynomial_map_table.at(dest_.term_idx());
    auto src1_reg = limb_map_table.at(src1_);
    return std::make_shared<ResolveISAInstruction>(dest_prg, src1_reg,
                                                   dest_.limbs(), limb_);
  };

  Polynomial *base_conversion_dest() override {
    // return &bco_dest_;
    return nullptr;
  }
};

class ModLimbInstruction : public LimbInstruction {
  // using OpCode = SSAInstruction::OpCode;
  Limb dest_;
  Polynomial src1_;

public:
  ModLimbInstruction(const Limb &dest, const Polynomial &src1,
                     const LimbIndexType limb)
      : dest_(dest), src1_(src1), LimbInstruction(OpCode::Mod, limb) {
    // assert(dest_.is_bcor());
  }

  std::vector<Limb *> dests() override { return std::vector<Limb *>({&dest_}); }

  std::vector<Limb *> srcs() override { return std::vector<Limb *>({}); }

  std::vector<Polynomial *> polynomial_dests() override {
    return std::vector<Polynomial *>();
  }

  std::vector<Polynomial *> polynomial_srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "mod " << dest_ << ": " << src1_;
    return s.str();
  }

  bool is_pl_instruction() const override { return false; }

  std::shared_ptr<ISAInstruction> get_register_instruction(
      const LimbMap<Register> &limb_map_table,
      const std::unordered_map<TermIndexType, BaseConversionUnit>
          &bcu_map_table,
      const std::unordered_map<TermIndexType, PolynomialRegisterGroup>
          &polynomial_map_table) const override {

    PolynomialRegisterGroup src1_prg =
        polynomial_map_table.at(src1_.term_idx());
    auto dest_reg = limb_map_table.at(dest_);
    return std::make_shared<ModISAInstruction>(dest_reg, src1_prg, limb_);
  };

  Polynomial *base_conversion_dest() override {
    // return &bco_dest_;
    return nullptr;
  }
};

using TermLimbIndex = std::pair<TermIndexType, LimbIndexType>;
struct pair_hash {
  template <class T1, class T2>
  std::size_t operator()(const std::pair<T1, T2> &pair) const {
    return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
  }
};

using ChainIndexType = uint64_t;
class Chain {

private:
  ChainIndexType index_;
  std::vector<std::shared_ptr<LimbInstruction>> instructions_;
  std::unordered_set<Limb, LimbHash, LimbCompare> inputs_;
  std::unordered_set<Limb, LimbHash, LimbCompare> outputs_;

public:
  Chain(uint64_t index) : index_(index){};

  const ChainIndexType &index() const { return index_; }
  const std::vector<std::shared_ptr<LimbInstruction>> &instructions() const {
    return instructions_;
  }
  std::vector<std::shared_ptr<LimbInstruction>> &instructions() {
    return instructions_;
  }
  void push_instruction(const std::shared_ptr<LimbInstruction> &instr) {
    instructions_.push_back(instr);
  }

  void add_input(const Limb &limb) {
    // auto key = TermLimbIndex(limb.term_idx(),limb.limb_idx());
    inputs_.insert(limb);
  }
  void add_output(const Limb &limb) {
    // auto key = TermLimbIndex(limb.term_idx(),limb.limb_idx());
    outputs_.insert(limb);
  }

  std::string ppInputs() const {
    if (inputs_.size() == 0) {
      return "";
    }
    std::stringstream s;
    auto it = inputs_.begin();
    s << *it;
    it++;
    for (; it != inputs_.end(); it++) {
      s << " ," << *it;
    }
    return s.str();
  }

  std::string ppOutputs() const {
    if (outputs_.size() == 0) {
      return "";
    }
    std::stringstream s;
    auto it = outputs_.begin();
    s << *it;
    it++;
    for (; it != outputs_.end(); it++) {
      s << " ," << *it;
    }
    return s.str();
  }
};

std::vector<std::shared_ptr<Chain>> Chains;

} // namespace Backend
} // namespace Cinnamon

template <> struct fmt::formatter<Cinnamon::Backend::LimbInstruction> {

  constexpr auto parse(format_parse_context &ctx)
      -> format_parse_context::iterator {

    // Parse the presentation format and store it in the formatter:
    auto it = ctx.begin(), end = ctx.end();

    // Check if reached the end of the range:
    if (it != end && *it != '}') ctx.on_error("invalid format");

    // Return an iterator past the end of the parsed range:
    return it;
  }

  auto format(const Cinnamon::Backend::LimbInstruction &instr,
              format_context &ctx) const -> format_context::iterator {
    std::stringstream s;
    s << instr.ppOp();
    return fmt::format_to(ctx.out(), "{}", s.str());
  }
};
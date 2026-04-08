// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include "cinnamon/backend/limb_instruction.h"
#include "cinnamon/backend/terms.h"
#include <memory>

namespace Cinnamon {
namespace Backend {

// Polynomial IR Instructions in Cinnamon
class PolynomialInstruction {
public:
  using OpCode = LimbInstruction::OpCode;

  PolynomialInstruction::OpCode opcode() const { return op_; };

  virtual std::vector<Polynomial *> dests() = 0;
  virtual std::vector<Polynomial *> srcs() = 0;
  virtual std::string ppOp() const = 0;
  virtual std::shared_ptr<PolynomialInstruction> clone() const = 0;
  virtual void set_limbs(size_t partition_id, size_t num_partitions) = 0;
  virtual bool verify_operands() const = 0;
  virtual bool is_pl_instruction() const { return false; }
  virtual bool is_mod_instruction() const { return false; }
  virtual bool is_join_instruction() const { return false; }
  virtual bool is_move_instruction() const { return false; }
  virtual std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb) const {
    return nullptr;
  };

protected:
  PolynomialInstruction(OpCode op) : op_(op){};
  PolynomialInstruction(){};
  OpCode op_;

private:
};

class InpInstruction : public PolynomialInstruction {
  Polynomial dest_;

public:
  InpInstruction(const Polynomial &dest)
      : dest_(dest), PolynomialInstruction(PolynomialInstruction::OpCode::Inp) {
    assert(dest_.is_input());
  }

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>();
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "inp";
    s << "\\n";
    s << dest_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<InpInstruction>(*this);
  }

  bool verify_operands() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return nullptr;
    }
    auto dest_limb = Limb(dest_, limb_idx);
    return std::make_shared<InpLimbInstruction>(dest_limb, limb_idx);
  }
};

class UnOpInstruction : public PolynomialInstruction {
  Polynomial dest_;
  Polynomial src1_;
  int32_t rot_idx_;

public:
  UnOpInstruction(PolynomialInstruction::OpCode op, const Polynomial &dest,
                  const Polynomial &src1)
      : dest_(dest), src1_(src1), rot_idx_(0), PolynomialInstruction(op) {
    switch (op) {
    case OpCode::Neg:
      break;
    case OpCode::Div:
      break;
    case OpCode::Joi:
      break;
    case OpCode::JoL:
      break;
    case OpCode::Int:
      break;
    case OpCode::Ntt:
      break;
    case OpCode::Con:
      break;
    case OpCode::Rsv:
      break;
    case OpCode::Mod:
      break;
    case OpCode::Mov:
      break;
    default:
      throw std::runtime_error("Invalid OpCode for Unary op: " + op);
    }
  }

  UnOpInstruction(PolynomialInstruction::OpCode op, const Polynomial &dest,
                  const Polynomial &src1, const int32_t rot_idx)
      : dest_(dest), src1_(src1), rot_idx_(rot_idx), PolynomialInstruction(op) {
    switch (op) {
    case OpCode::Rot:
      break;
    default:
      throw std::runtime_error("Invalid OpCode for Unary op: " + op);
    }
  }

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  decltype(rot_idx_) rot_idx() const { return rot_idx_; }

  std::string ppOp() const override {
    std::stringstream s;
    switch (op_) {
    case OpCode::Neg:
      s << "neg";
      break;
    case OpCode::Rot:
      s << "rot " << rot_idx_;
      break;
    case OpCode::Div:
      s << "div";
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
    case OpCode::Joi:
      s << "joi";
      break;
    case OpCode::JoL:
      s << "jol";
      break;
    case OpCode::Rsv:
      s << "rsv";
      break;
    case OpCode::Mod:
      s << "mod";
      break;
    case OpCode::Mov:
      s << "mov";
      break;
    }
    s << "\\n" << dest_ << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<UnOpInstruction>(*this);
  }

  bool verify_operands() const override {
    if (op_ == OpCode::SuD) {
      return dest_.num_limbs() == src1_.num_limbs() ||
             dest_.num_limbs() == src1_.num_limbs() - 1;
    } else if (op_ == OpCode::Mod) {
      return true;
    }

    return dest_.num_limbs() == src1_.num_limbs();
  }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return nullptr;
    }
    auto &src1_limbs = src1_.limbs();
    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      return nullptr;
    }
    auto dest_limb = Limb(dest_, limb_idx);
    auto src1_limb = Limb(src1_, limb_idx);
    if (op_ == OpCode::Rot) {
      return std::make_shared<UnOpLimbInstruction>(
          OpCode::Rot, dest_limb, src1_limb, rot_idx_, limb_idx);
    }
    return std::make_shared<UnOpLimbInstruction>(op_, dest_limb, src1_limb,
                                                 limb_idx);
  }
};

class BinOpInstruction : public PolynomialInstruction {
  Polynomial dest_;
  Polynomial src1_;
  Polynomial src2_;

public:
  BinOpInstruction(PolynomialInstruction::OpCode op, const Polynomial &dest,
                   const Polynomial &src1, const Polynomial &src2)
      : dest_(dest), src1_(src1), src2_(src2), PolynomialInstruction(op) {
    switch (op) {
    case OpCode::Add:
    case OpCode::Sub:
    case OpCode::MuP:;
    case OpCode::Mul:
      break;
    case OpCode::SuD:
      break;
    default:
      throw std::runtime_error("Invalid OpCode for Binary op: " + op);
    }
  }

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_, &src2_});
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
    s << "\\n" << dest_ << ": " << src1_ << ", " << src2_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<BinOpInstruction>(*this);
  }

  bool verify_operands() const override {
    auto dest_limbs = dest_.num_limbs();
    auto src1_limbs = src1_.num_limbs();
    auto src2_limbs = src2_.num_limbs();

    if (op_ == OpCode::Add || op_ == OpCode::Sub) {
      auto max_limbs = src1_limbs;
      if (src2_limbs > max_limbs) {
        max_limbs = src2_limbs;
      }
      return dest_limbs == max_limbs;
    } else if (op_ == OpCode::MuP) {
      // TODO: Fill this
      // return true;
    } else if (op_ == OpCode::SuD) {
      return src2_limbs == 1;
    }

    if (src1_limbs != dest_limbs) {
      return false;
    }
    if (src2_limbs != dest_limbs) {
      return false;
    }
    return true;
  }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
    if (op_ == OpCode::MuP) {
      src2_.set_limbs(dest_.limbs());
    } else {
      src2_.set_limbs_from_termshare(partition_id, num_partitions);
    }
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return nullptr;
    }

    auto &src1_limbs = src1_.limbs();
    auto &src2_limbs = src2_.limbs();

    if (op_ == OpCode::Add || op_ == OpCode::Sub) {

      uint8_t both_srcs_have_limb = 0;

      if (src1_limbs.find(limb_idx) != src1_limbs.end()) {
        both_srcs_have_limb |= 1;
      }
      if (src2_limbs.find(limb_idx) != src2_limbs.end()) {
        both_srcs_have_limb |= 2;
      }

      switch (both_srcs_have_limb) {
      case 0:
        return nullptr;
        break;
      case 1: {
        auto dest_limb = Limb(dest_, limb_idx);
        auto src_limb = Limb(src1_, limb_idx);
        return std::make_shared<UnOpLimbInstruction>(OpCode::Mov, dest_limb,
                                                     src_limb, limb_idx);
        break;
      }
      case 2: {
        if (op_ == OpCode::Add) {
          auto dest_limb = Limb(dest_, limb_idx);
          auto src_limb = Limb(src2_, limb_idx);
          return std::make_shared<UnOpLimbInstruction>(OpCode::Mov, dest_limb,
                                                       src_limb, limb_idx);
          break;
        } else if (op_ == OpCode::Sub) {
          auto dest_limb = Limb(dest_, limb_idx);
          auto src_limb = Limb(src2_, limb_idx);
          return std::make_shared<UnOpLimbInstruction>(OpCode::Neg, dest_limb,
                                                       src_limb, limb_idx);
          break;
        }
        break;
      }
      case 3:
        break;
      default:
        assert(0);
      }
    }

    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      return nullptr;
    }
    auto dest_limb = Limb(dest_, limb_idx);
    auto src1_limb = Limb(src1_, limb_idx);
    Limb src2_limb;
    if (op_ == OpCode::SuD) {
      assert(src2_limbs.size() == 1);
      auto last_limb_idx = *src2_limbs.begin();
      src2_limb = Limb(src2_, last_limb_idx);

    } else {
      if (src2_limbs.find(limb_idx) == src2_limbs.end()) {
        return nullptr;
      }
      src2_limb = Limb(src2_, limb_idx);
    }
    return std::make_shared<BinOpLimbInstruction>(op_, dest_limb, src1_limb,
                                                  src2_limb, limb_idx);
  }
};

class PlInstruction1 : public PolynomialInstruction {
  Polynomial bco_dest_;
  Polynomial src1_;
  bool intt_required_ = true;

public:
  PlInstruction1(const Polynomial &bco_dest, const Polynomial &src1)
      : bco_dest_(bco_dest), src1_(src1),
        PolynomialInstruction(PolynomialInstruction::OpCode::Pip1) {
    assert(bco_dest_.is_bcor());
  }

  PlInstruction1(const Polynomial &bco_dest, const Polynomial &src1,
                 bool intt_required)
      : bco_dest_(bco_dest), src1_(src1), intt_required_(intt_required),
        PolynomialInstruction(PolynomialInstruction::OpCode::Pip1) {
    assert(bco_dest_.is_bcor());
  }

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&bco_dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "pl1\\n" << bco_dest_ << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<PlInstruction1>(*this);
  }

  bool verify_operands() const override {
    // return bco_dest_.num_limbs() == src1_.level();
    return true;
  }

  bool is_pl_instruction() const override { return true; }
  void set_limbs(size_t partition_id, size_t num_partitions) override {
    bco_dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &src1_limbs = src1_.limbs();
    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      return nullptr;
    }
    auto src1_limb = Limb(src1_, limb_idx);
    return std::make_shared<PlLimbInstruction1>(bco_dest_, src1_limb, limb_idx,
                                                intt_required_);
  }
};

class PlInstruction7 : public PolynomialInstruction {
  Polynomial dest_;
  Polynomial src1_;
  Polynomial src2_;

public:
  PlInstruction7(const Polynomial &dest, const Polynomial &src1,
                 const Polynomial &src2)
      : dest_(dest), src1_(src1), src2_(src2),
        PolynomialInstruction(PolynomialInstruction::OpCode::Pip7) {
    assert(src2_.is_bcor());
  }

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_, &src2_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "pl7\\n" << dest_ << ": " << src1_ << ", " << src2_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<PlInstruction7>(*this);
  }

  bool verify_operands() const override { return true; }

  bool is_pl_instruction() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
    src2_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return nullptr;
    }
    auto &src1_limbs = src1_.limbs();
    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      return nullptr;
    }
    auto &src2_limbs = src2_.limbs();
    if (src2_limbs.find(limb_idx) == src2_limbs.end()) {
      return nullptr;
    }
    auto dest_limb = Limb(dest_, limb_idx);
    auto src1_limb = Limb(src1_, limb_idx);
    auto src2_limb = Limb(src2_, limb_idx);
    return std::make_shared<BinOpLimbInstruction>(
        OpCode::SuD, dest_limb, src1_limb, src2_limb, limb_idx);
  }
};

class PlInstruction8 : public PolynomialInstruction {
  Polynomial dest1_;
  Polynomial dest2_;
  Polynomial bsrc1_;
  Polynomial src2_;

public:
  PlInstruction8(const Polynomial &dest1, const Polynomial &dest2,
                 const Polynomial &bsrc1, const Polynomial &src2)
      : dest1_(dest1), dest2_(dest2), bsrc1_(bsrc1), src2_(src2),
        PolynomialInstruction(PolynomialInstruction::OpCode::Pip8) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest1_, &dest2_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&bsrc1_, &src2_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "pl8\\n"
      << dest1_ << ", " << dest2_ << ": " << bsrc1_ << ", " << src2_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<PlInstruction8>(*this);
  }

  bool verify_operands() const override { return true; }

  bool is_pl_instruction() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest1_.set_limbs_from_termshare(partition_id, num_partitions);
    dest2_.set_limbs_from_termshare(partition_id, num_partitions);
    bsrc1_.set_limbs_from_termshare(partition_id, num_partitions);
    src2_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest1_limbs = dest1_.limbs(); // Ext
    auto &dest2_limbs = dest2_.limbs(); // Com
    auto &bsrc1_limbs = bsrc1_.limbs(); // ExtC
    auto &src2_limbs = src2_.limbs();   // Spl
    uint8_t both_dests_have_limb = 0;
    uint8_t both_srcs_have_limb = 0;

    if (dest1_limbs.find(limb_idx) != dest1_limbs.end()) {
      both_dests_have_limb |= 1;
    }
    if (dest2_limbs.find(limb_idx) != dest2_limbs.end()) {
      both_dests_have_limb |= 2;
    }

    if (bsrc1_limbs.find(limb_idx) != bsrc1_limbs.end()) {
      both_srcs_have_limb |= 1;
    }
    if (src2_limbs.find(limb_idx) != src2_limbs.end()) {
      both_srcs_have_limb |= 2;
    }

    switch (both_dests_have_limb) {
    case 0:
      return nullptr;
      break;
    case 1: {
      auto dest_limb = Limb(dest1_, limb_idx);
      Limb src_limb;
      if (both_srcs_have_limb == 1) {
        src_limb = Limb(bsrc1_, limb_idx);
        return std::make_shared<UnOpLimbInstruction>(OpCode::Ntt, dest_limb,
                                                     src_limb, limb_idx);
      } else if (both_srcs_have_limb == 2) {
        src_limb = Limb(src2_, limb_idx);
        return std::make_shared<UnOpLimbInstruction>(OpCode::Mov, dest_limb,
                                                     src_limb, limb_idx);
      } else {
        assert(0);
      }

    }; break;
    case 2: {
      auto dest_limb = Limb(dest2_, limb_idx);
      assert(bsrc1_limbs.find(limb_idx) != bsrc1_limbs.end());
      auto src_limb = Limb(bsrc1_, limb_idx);
      return std::make_shared<UnOpLimbInstruction>(OpCode::Ntt, dest_limb,
                                                   src_limb, limb_idx);
    }; break;
    default:
      assert(0);
    }

    return nullptr;
  }
};

class JoinLocalInstruction : public PolynomialInstruction {

private:
  Polynomial dest_;
  Polynomial src1_;
  Polynomial src2_;

public:
  JoinLocalInstruction(const Polynomial &dest, const Polynomial &src1)
      : dest_(dest), src1_(src1), PolynomialInstruction(OpCode::JoL) {}
  JoinLocalInstruction(const Polynomial &dest, const Polynomial &src1,
                       const Polynomial &src2)
      : dest_(dest), src1_(src1), src2_(src2),
        PolynomialInstruction(OpCode::JoL) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    if (src2_.term_share() == nullptr || src2_.term() == nullptr) {
      return std::vector<Polynomial *>({&src1_});
    } else {
      return std::vector<Polynomial *>({&src1_, &src2_});
    }
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "jol\\n" << dest_ << ": " << src1_;
    if (src2_.term_share() != nullptr && src2_.term() != nullptr) {
      s << ", " << src2_;
    }
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<JoinLocalInstruction>(*this);
  }

  bool verify_operands() const override {
    auto dest_limbs = dest_.num_limbs();
    auto src1_limbs = src1_.num_limbs();
    if (src2_.term_share() == nullptr || src2_.term() == nullptr) {
      return dest_limbs == src1_limbs;
    }
    auto src2_limbs = src2_.num_limbs();
    if (src1_.num_limbs() == 0) {
      return dest_limbs == src2_limbs;
    } else if (src2_.num_limbs() == 0) {
      return dest_limbs == src1_limbs;
    }

    return (dest_limbs == src1_limbs) && (dest_limbs == src2_limbs);
  }

  void add_src2(const Polynomial &src2) { src2_ = src2; }

  bool is_join_instruction() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
    if (src2_.term_share() != nullptr && src2_.term() != nullptr) {
      src2_.set_limbs_from_termshare(partition_id, num_partitions);
    }
  }
  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return nullptr;
    }
    auto &src1_limbs = src1_.limbs();
    auto &src2_limbs = src2_.limbs();
    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      assert(src2_limbs.find(limb_idx) != src2_limbs.end());
      auto dest_limb = Limb(dest_, limb_idx);
      auto src2_limb = Limb(src2_, limb_idx);
      return std::make_shared<UnOpLimbInstruction>(OpCode::Mov, dest_limb,
                                                   src2_limb, limb_idx);
    }
    if (src2_limbs.find(limb_idx) == src2_limbs.end()) {
      return nullptr;
    }
    auto dest_limb = Limb(dest_, limb_idx);
    auto src1_limb = Limb(src1_, limb_idx);
    auto src2_limb = Limb(src2_, limb_idx);
    return std::make_shared<BinOpLimbInstruction>(
        OpCode::Add, dest_limb, src1_limb, src2_limb, limb_idx);
  }
};

class SyncGlobalInstruction : public PolynomialInstruction {

private:
  uint64_t sync_id_;
  std::set<LimbIndexType> sync_limbs_;
  uint64_t sync_size_;

public:
  SyncGlobalInstruction(uint64_t sync_id, uint64_t sync_size,
                        const std::set<LimbIndexType> &sync_limbs)
      : sync_id_(sync_id), sync_size_(sync_size), sync_limbs_(sync_limbs),
        PolynomialInstruction(OpCode::Syn) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "syn @ " << sync_id_ << ":" << sync_size_ << "\\n"
      << ": ";
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<SyncGlobalInstruction>(*this);
  }

  bool verify_operands() const override {
    return true;
    // TODO:
  }

  const std::set<LimbIndexType> &sync_limbs() const { return sync_limbs_; }

  bool is_join_instruction() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {}

  void set_sync_id(const uint64_t sync_id) { sync_id_ = sync_id; }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    return std::make_shared<JoinGlobalLimbInstruction>(limb_idx, sync_id_,
                                                       sync_size_);
  }
};

class JoinGlobalInstruction : public PolynomialInstruction {

private:
  Polynomial dest_;
  Polynomial src1_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  JoinGlobalInstruction(const Polynomial &dest, const Polynomial &src1)
      : dest_(dest), src1_(src1), sync_id_(0),
        PolynomialInstruction(OpCode::Joi) {}
  JoinGlobalInstruction(uint64_t sync_id)
      : sync_id_(sync_id), PolynomialInstruction(OpCode::Joi) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "joi @ " << sync_id_ << ":" << sync_size_ << "\\n"
      << dest_ << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<JoinGlobalInstruction>(*this);
  }

  std::shared_ptr<PolynomialInstruction> make_sync_instruction() const {
    return std::make_shared<SyncGlobalInstruction>(sync_id_, sync_size_,
                                                   src1_.limbs());
  }

  bool verify_operands() const override {
    // TODO:
    return src1_.num_limbs() == src1_.level();
  }

  bool is_join_instruction() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  void set_sync_id(const uint64_t sync_id) { sync_id_ = sync_id; }

  void set_sync_size(const uint64_t sync_size) { sync_size_ = sync_size; }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &src1_limbs = src1_.limbs();
    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      return nullptr;
    }
    auto src1_limb = Limb(src1_, limb_idx);
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return std::make_shared<JoinGlobalLimbInstruction>(src1_limb, limb_idx,
                                                         sync_id_, sync_size_);
    }
    auto dest_limb = Limb(dest_, limb_idx);
    return std::make_shared<JoinGlobalLimbInstruction>(
        dest_limb, src1_limb, limb_idx, sync_id_, sync_size_);
  }
};

class RecvInstruction : public PolynomialInstruction {
  Polynomial dest_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  RecvInstruction(const Polynomial &dest)
      : dest_(dest), sync_id_(0), sync_size_(0),
        PolynomialInstruction(OpCode::Rcv) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "rcv @ " << sync_id_ << ":" << sync_size_ << "\\n" << dest_ << ": ";
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<RecvInstruction>(*this);
  }

  // bool verify_operands() const override { return dest_.num_limbs() == 1; }
  bool verify_operands() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  void set_sync_id(const uint64_t sync_id) { sync_id_ = sync_id; }

  void set_sync_size(const uint64_t sync_size) { sync_size_ = sync_size; }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return nullptr;
    }
    auto dest_limb = Limb(dest_, limb_idx);
    return std::make_shared<RecvLimbInstruction>(dest_limb, limb_idx, sync_id_,
                                                 sync_size_);
  }
};

class DistInstruction : public PolynomialInstruction {
  Polynomial src1_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  DistInstruction(const Polynomial &src1)
      : src1_(src1), sync_id_(0), sync_size_(0),
        PolynomialInstruction(OpCode::Dis) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "dis @ " << sync_id_ << ":" << sync_size_ << "\\n"
      << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<DistInstruction>(*this);
  }

  std::shared_ptr<PolynomialInstruction> make_recv_instruction() const {
    auto recv_instr = std::make_shared<RecvInstruction>(this->src1_);
    recv_instr->set_sync_id(sync_id_);
    recv_instr->set_sync_size(sync_size_);
    return recv_instr;
  }

  // bool verify_operands() const override { return src1_.num_limbs() == 1; }
  bool verify_operands() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  void set_sync_id(const uint64_t sync_id) { sync_id_ = sync_id; }

  void set_sync_size(const uint64_t sync_size) { sync_size_ = sync_size; }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    if (sync_size_ == 1) {
      return nullptr;
    }
    auto src1_limbs = src1_.limbs();
    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      return nullptr;
    }
    auto src1_limb = Limb(src1_, limb_idx);
    return std::make_shared<DistLimbInstruction>(src1_limb, limb_idx, sync_id_,
                                                 sync_size_);
  }
};

class DistRecvMovInstruction : public PolynomialInstruction {
  Polynomial src1_;
  Polynomial dest_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  DistRecvMovInstruction(const Polynomial &dest, Polynomial &src1)
      : dest_(dest), src1_(src1), sync_id_(0), sync_size_(0),
        PolynomialInstruction(OpCode::Drm) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "drm @ " << sync_id_ << ":" << sync_size_ << "\\n"
      << dest_ << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<DistRecvMovInstruction>(*this);
  }

  std::shared_ptr<PolynomialInstruction> make_dist_instruction() const {
    auto dist_instr = std::make_shared<DistInstruction>(this->src1_);
    dist_instr->set_sync_id(sync_id_);
    dist_instr->set_sync_size(sync_size_);
    return dist_instr;
  }

  std::shared_ptr<PolynomialInstruction> make_mov_instruction() const {
    auto mov_instr = std::make_shared<UnOpInstruction>(OpCode::Mov, this->dest_,
                                                       this->src1_);
    return mov_instr;
  }

  // bool verify_operands() const override { return src1_.num_limbs() == 1; }
  bool verify_operands() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  void set_sync_id(const uint64_t sync_id) { sync_id_ = sync_id; }

  void set_sync_size(const uint64_t sync_size) { sync_size_ = sync_size; }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    throw std::runtime_error("This function should not be called");
  }
};

class ReceiveInstruction : public PolynomialInstruction {
  Polynomial src1_;
  Polynomial dest_;
  uint64_t src_partition_size_;
  uint64_t src_partition_id_;
  uint64_t dest_partition_size_;
  uint64_t dest_partition_id_;
  uint64_t sync_id_;
  uint64_t sync_size_;

public:
  ReceiveInstruction(const Polynomial &dest, Polynomial &src1)
      : dest_(dest), src1_(src1), sync_id_(0), sync_size_(2),
        src_partition_size_(0), src_partition_id_(-1), dest_partition_size_(0),
        dest_partition_id_(-1), PolynomialInstruction(OpCode::Rec) {
    assert(dest_.limbtype() == Term::LimbType::Spl);
    assert(src1_.limbtype() == Term::LimbType::Spl);
  }

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "rec @ " << sync_id_ << ":" << sync_size_ << "\\n"
      << dest_ << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<ReceiveInstruction>(*this);
  }

  std::shared_ptr<PolynomialInstruction> make_dist_instruction() const {
    auto dist_instr = std::make_shared<DistInstruction>(this->src1_);
    dist_instr->set_sync_id(sync_id_);
    dist_instr->set_sync_size(sync_size_);
    return dist_instr;
  }

  std::shared_ptr<PolynomialInstruction> make_mov_instruction() const {
    auto mov_instr = std::make_shared<UnOpInstruction>(OpCode::Mov, this->dest_,
                                                       this->src1_);
    return mov_instr;
  }

  // bool verify_operands() const override { return src1_.num_limbs() == 1; }
  bool verify_operands() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  void set_sync_id(const uint64_t sync_id) { sync_id_ = sync_id; }

  // void set_sync_size(const uint64_t sync_size) { sync_size_ = sync_size; }

  void set_dest_partition_size(const uint64_t dest_partition_size) {
    dest_partition_size_ = dest_partition_size;
  }

  void set_dest_partition_id(const uint64_t dest_partition_id) {
    dest_partition_id_ = dest_partition_id;
  }

  void set_src_partition_size(const uint64_t src_partition_size) {
    src_partition_size_ = src_partition_size;
  }

  void set_src_partition_id(const uint64_t src_partition_id) {
    src_partition_id_ = src_partition_id;
  }

  auto dest_partition_size() { return dest_partition_size_; }

  auto dest_partition_id() { return dest_partition_id_; }

  auto src_partition_size() { return src_partition_size_; }

  auto src_partition_id() { return src_partition_id_; }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {

    auto &dest_limbs = dest_.limbs();
    auto &src1_limbs = src1_.limbs();

    if (dest_limbs.find(limb_idx) != dest_limbs.end() &&
        src1_limbs.find(limb_idx) != src1_limbs.end()) {
      auto dest_limb = Limb(dest_, limb_idx);
      auto src1_limb = Limb(src1_, limb_idx);
      return std::make_shared<UnOpLimbInstruction>(OpCode::Mov, dest_limb,
                                                   src1_limb, limb_idx);
    }
    if (dest_limbs.find(limb_idx) != dest_limbs.end() &&
        src1_limbs.find(limb_idx) == src1_limbs.end()) {
      auto dest_limb = Limb(dest_, limb_idx);
      return std::make_shared<RecvLimbInstruction>(dest_limb, limb_idx,
                                                   sync_id_, sync_size_);
    }
    if (dest_limbs.find(limb_idx) == dest_limbs.end() &&
        src1_limbs.find(limb_idx) != src1_limbs.end()) {
      auto src1_limb = Limb(src1_, limb_idx);
      return std::make_shared<DistLimbInstruction>(src1_limb, limb_idx,
                                                   sync_id_, sync_size_);
    }
    return nullptr;
    // throw std::runtime_error("This function should not be called");
  }
};

class ResolveInstruction : public PolynomialInstruction {
  Polynomial dest_;
  Polynomial src1_;

public:
  ResolveInstruction(const Polynomial &dest, const Polynomial &src1)
      : dest_(dest), src1_(src1), PolynomialInstruction(OpCode::Rsv) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "rsv " << dest_ << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<ResolveInstruction>(*this);
  }

  bool verify_operands() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &src1_limbs = src1_.limbs();
    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      return nullptr;
    }
    auto src1_limb = Limb(src1_, limb_idx);
    return std::make_shared<ResolveLimbInstruction>(dest_, src1_limb, limb_idx);
  }
};

class ModInstruction : public PolynomialInstruction {
  Polynomial dest_;
  Polynomial src1_;

public:
  ModInstruction(const Polynomial &dest, const Polynomial &src1)
      : dest_(dest), src1_(src1), PolynomialInstruction(OpCode::Mod) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "mod " << dest_ << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<ModInstruction>(*this);
  }

  bool verify_operands() const override { return true; }

  bool is_mod_instruction() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return nullptr;
    }
    auto dest_limb = Limb(dest_, limb_idx);
    return std::make_shared<ModLimbInstruction>(dest_limb, src1_, limb_idx);
  }
};

class MoveInstruction : public PolynomialInstruction {
  Polynomial dest_;
  Polynomial src1_;

public:
  MoveInstruction(const Polynomial &dest, const Polynomial &src1)
      : dest_(dest), src1_(src1), PolynomialInstruction(OpCode::Mov) {}

  std::vector<Polynomial *> dests() override {
    return std::vector<Polynomial *>({&dest_});
  }

  std::vector<Polynomial *> srcs() override {
    return std::vector<Polynomial *>({&src1_});
  }

  std::string ppOp() const override {
    std::stringstream s;
    s << "mov " << dest_ << ": " << src1_;
    return s.str();
  }

  std::shared_ptr<PolynomialInstruction> clone() const override {
    return std::make_shared<MoveInstruction>(*this);
  }

  bool verify_operands() const override {
    return dest_.num_limbs() <= src1_.num_limbs();
  }

  bool is_move_instruction() const override { return true; }

  void set_limbs(size_t partition_id, size_t num_partitions) override {
    dest_.set_limbs_from_termshare(partition_id, num_partitions);
    src1_.set_limbs_from_termshare(partition_id, num_partitions);
  }

  std::shared_ptr<LimbInstruction>
  get_limb_instruction(LimbIndexType limb_idx) const override {
    auto &dest_limbs = dest_.limbs();
    if (dest_limbs.find(limb_idx) == dest_limbs.end()) {
      return nullptr;
    }
    auto &src1_limbs = src1_.limbs();
    if (src1_limbs.find(limb_idx) == src1_limbs.end()) {
      return nullptr;
    }
    auto dest_limb = Limb(dest_, limb_idx);
    auto src1_limb = Limb(src1_, limb_idx);
    return std::make_shared<UnOpLimbInstruction>(OpCode::Mov, dest_limb,
                                                 src1_limb, limb_idx);
  }
};

} // namespace Backend
} // namespace Cinnamon

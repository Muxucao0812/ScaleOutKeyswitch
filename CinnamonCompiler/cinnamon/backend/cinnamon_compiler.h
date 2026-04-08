// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <cctype>
#include <cstdarg>
#include <cstdlib>

#include <fmt/core.h>
#include <fmt/ostream.h>

#include "cinnamon/backend/big_node.h"
#include "cinnamon/backend/keyswitch_digits.h"
#include "cinnamon/backend/logger.h"
#include "cinnamon/backend/polynomial_instruction.h"
#include "cinnamon/backend/register_allocation.h"
#include "cinnamon/backend/terms.h"
#include "cinnamon/frontend/program.h"
#include "cinnamon/frontend/term_map.h"
#include "cinnamon/util/logging.h"
#include "cinnamon/util/overloaded.h"

#include <optional>
#include <tuple>
namespace Cinnamon {
namespace Backend {
class CinnamonCompiler {
  using RuntimeValue = std::variant<Ciphertext, Plaintext, CiphertextVector>;

  bool USE_BASIC_PARALLEL_KEYSWITCHING = false;

  Program &program;
  TermMapOptional<RuntimeValue> Objects;

  std::string output_prefix;

  uint32_t levels;

  uint64_t instruction_count = 0;

  uint64_t nextTermIndex = 1;
  uint64_t nextTermShareIndex = 1;

  uint64_t ks_pl_count = 1;

  uint8_t num_partitions = 1;
  uint8_t current_partition_size = num_partitions;
  uint8_t current_partition_id = 0;

  uint64_t num_vregs = 1024;

  std::vector<BigNodeIndexType> top_sort;
  std::vector<std::vector<BigNodeIndexType>> top_sort_partitions;
  std::vector<std::vector<ChainIndexType>> top_sort_chains;

  bool isCipher(const Frontend::Term::Ptr &t) {
    return std::holds_alternative<Ciphertext>(Objects.at(t));
  }
  bool isPlain(const Frontend::Term::Ptr &t) {
    return std::holds_alternative<Plaintext>(Objects.at(t));
  }

  bool isCiphertextVector(const Frontend::Term::Ptr &t) {
    return std::holds_alternative<CiphertextVector>(Objects.at(t));
  }

  std::map<TermIndexType, std::shared_ptr<Term>> terms;
  std::map<TermIndexType, std::shared_ptr<TermShare>> term_shares;

  Polynomial make_treg(uint16_t level) {
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;
    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    return Polynomial(term_share, level);
  }

  decltype(ks_pl_count) get_pl_idx() { return ks_pl_count++; }

  Polynomial make_modup_treg(const uint16_t level) {

    return make_treg(level);
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    return Polynomial(term_share, level);
  }

  Polynomial make_treg_from(const Polynomial &other) {
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;
    auto level = other.level();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    return Polynomial(term_share, level);
  }

  Polynomial make_copy_treg_from(const Polynomial *other) {
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index, other->term());
    terms[index] = term;
    auto level = other->level();
    // uint16_t first_limb_idx = other.start_idx();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    auto poly = Polynomial(term_share, level);
    poly.set_bignode_idx(other->bignode_index());
    poly.set_limbs(other->limbs());
    poly.set_shares(other->shares());
    return poly;
  }

  Polynomial make_treg_with_term(const Polynomial *other,
                                 const TermIndexType &index) {
    auto term = terms[index];
    auto level = other->level();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    auto poly = Polynomial(term_share, level);
    poly.set_bignode_idx(other->bignode_index());
    poly.set_limbs(other->limbs());
    poly.set_shares(other->shares());
    return poly;
  }

  Polynomial make_new_term_share_from(const Polynomial *other) {
    auto level = other->level();
    auto term = other->term();
    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    auto poly = Polynomial(term_share, level);
    poly.set_bignode_idx(other->bignode_index());
    return poly;
  }

  Polynomial make_rescale_treg_from(const Polynomial &other) {
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;
    auto level = other.level() - 1;
    assert(level > 0);

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    return Polynomial(term_share, level);
  }

  Polynomial make_modswitch_treg_from(Polynomial &other) {
    auto term = other.term();
    auto level = other.level() - 1;
    assert(level > 0);
    // uint16_t first_limb_idx = other.start_idx();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;

    auto poly = Polynomial(term_share, level);
    poly.set_bignode_idx(other.bignode_index());
    return poly;
  }

  Polynomial make_bcor_from(const Polynomial &other) {
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;
    term->set_as_bcor();
    // auto limbs = other.num_limbs();
    auto level = other.level();
    // uint16_t first_limb_idx = other.start_idx();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    return Polynomial(term_share, level);

    // return Polynomial(term, level);
  }

  Polynomial make_bcor(const uint16_t level) {
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;
    term->set_as_bcor();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;
    return Polynomial(term_share, level);

    // return Polynomial(term, level);
  }
  enum KeySwitchType {
    Broadcast,
    Aggregation,
    BasicParallel,
  };

  // return global and local keyswitch digit numbers
  std::pair<uint16_t, uint16_t>
  get_keyswitch_dnum_28bit(uint16_t level, KeySwitchType key_switch_type) {
    if (key_switch_type == KeySwitchType::Aggregation) {
      return get_keyswitch_dnum_aggregation_28bit(level, num_partitions,
                                                  current_partition_size);
    } else if (key_switch_type == KeySwitchType::Broadcast) {
      return get_keyswitch_dnum_broadcast_28bit(level, num_partitions,
                                                current_partition_size);
    } else if (key_switch_type == KeySwitchType::BasicParallel) {
      return get_keyswitch_dnum_broadcast_28bit(level, num_partitions,
                                                current_partition_size);
    }
    throw std::runtime_error("Invalid key switch type");
    return std::pair(0, 0);
  }

  std::pair<uint16_t, uint16_t>
  get_keyswitch_dnum(uint16_t level, KeySwitchType key_switch_type) {
    return get_keyswitch_dnum_28bit(level, key_switch_type);
  }

  struct EvalKeyIndexType {
    KeySwitchType key_switch_type;
    EvalKeyType evk_type;
    uint8_t level;
    uint8_t extension_size;
    uint8_t partition_size;
    uint8_t partition_id;
    int32_t rot_idx;

    EvalKeyIndexType(KeySwitchType key_switch_type, EvalKeyType evk_type,
                     uint8_t level, uint8_t extension_size,
                     uint8_t partition_size, uint8_t partition_id,
                     int32_t rot_idx)
        : key_switch_type(key_switch_type), evk_type(evk_type), level(level),
          extension_size(extension_size), partition_size(partition_size),
          partition_id(partition_id), rot_idx(rot_idx) {}
  };

  struct EvalKeyIndexTypeHash {
    std::size_t operator()(const EvalKeyIndexType &evk_index) const {
      // return std::hash<KeySwitchType>()() ^
      // std::hash<LimbIndexType>()(limb.limb_idx());
      return std::hash<KeySwitchType>()(evk_index.key_switch_type) ^
             std::hash<EvalKeyType>()(evk_index.evk_type) ^
             std::hash<uint8_t>()(evk_index.level) ^
             std::hash<uint8_t>()(evk_index.extension_size) ^
             std::hash<uint8_t>()(evk_index.partition_size) ^
             std::hash<uint8_t>()(evk_index.partition_id) ^
             std::hash<int32_t>()(evk_index.rot_idx);
    }
  };

  struct EvalKeyIndexTypeCompare {
    bool operator()(const EvalKeyIndexType &lhs,
                    const EvalKeyIndexType &rhs) const {
      // return lhs.term_idx() == rhs.term_idx() && lhs.limb_idx() ==
      // rhs.limb_idx();
      return lhs.key_switch_type == rhs.key_switch_type &&
             lhs.evk_type == rhs.evk_type && lhs.level == rhs.level &&
             lhs.extension_size == rhs.extension_size &&
             lhs.partition_size == rhs.partition_size &&
             lhs.partition_id == rhs.partition_id && lhs.rot_idx == rhs.rot_idx;
    }
  };

  template <typename T>
  using EvalKeyIndexTypeMap =
      std::unordered_map<EvalKeyIndexType, T, EvalKeyIndexTypeHash,
                         EvalKeyIndexTypeCompare>;

  EvalKeyIndexTypeMap<uint64_t> evalkey_input_indices;
  EvalKeyIndexTypeMap<BigNodeIndexType> evalkey_input_bn0;
  EvalKeyIndexTypeMap<BigNodeIndexType> evalkey_input_bn1;
  EvalKeyIndexTypeMap<BigNodeIndexType> evalkey_input_bn0_;
  EvalKeyIndexTypeMap<BigNodeIndexType> evalkey_input_bn1_;
  using PolyPair = std::pair<Polynomial, Polynomial>;

  // Returns exisiting evalkey term if it exists otherwise creates a new one
  std::pair<PolyPair, PolyPair>
  get_evalkey(const Polynomial &input, EvalKeyType evk_type, int32_t rot_idx,
              KeySwitchType key_switch_type, uint16_t extension_size) {
    uint16_t level = input.level();

    using LimbType = Term::LimbType;
    LimbType limbtype = LimbType::Usp; // Type of the first part of the evalkey
    LimbType limbtype_ =
        LimbType::Ext; // Type of the extension part of the evalkey

    bool evk_dont_split_shares = false;

    if (evk_type == EvalKeyType::Bootstrap ||
        evk_type == EvalKeyType::Bootstrap2) {
      // level = levels + 1;
      level = rot_idx + 1; // overloading rot_idx to get the level of the output
      extension_size = input.level(); // + 1;
      limbtype = LimbType::Spl; // In a bootstrap operation, the first half of
                                // the evalkey can be split across chiplets
      evk_dont_split_shares = true;
    } else if (key_switch_type == KeySwitchType::Broadcast) {
      limbtype = LimbType::Par;
      evk_dont_split_shares = true;
    } else if (key_switch_type == KeySwitchType::BasicParallel) {
      limbtype = LimbType::Par;
      limbtype_ = LimbType::Ext_Split;
      evk_dont_split_shares = true;
    }

    auto evalkey_map_idx =
        EvalKeyIndexType(key_switch_type, evk_type, level, extension_size,
                         current_partition_size, current_partition_id, rot_idx);
    auto it = evalkey_input_indices.find(evalkey_map_idx);
    uint64_t evalkey_index;

    BigNodeIndexType bn0;
    BigNodeIndexType bn0_;
    BigNodeIndexType bn1;
    BigNodeIndexType bn1_;

    if (it != evalkey_input_indices.end()) {
      evalkey_index = it->second;
      bn0 = evalkey_input_bn0[evalkey_map_idx];
      bn1 = evalkey_input_bn1[evalkey_map_idx];
      bn0_ = evalkey_input_bn0_[evalkey_map_idx];
      bn1_ = evalkey_input_bn1_[evalkey_map_idx];
    } else {
      std::stringstream s;
      s << "K:" << evk_type << ":" << level << ":" << extension_size << ":"
        << rot_idx << ":";
      std::string evalkey_name = std::move(s.str());
      auto eval_key0 = make_input(level);
      eval_key0.set_limbtype(limbtype);
      eval_key0.set_symbol(evalkey_name + "c0");
      auto eval_key1 = make_input(level);
      eval_key1.set_limbtype(limbtype);
      eval_key1.set_symbol(evalkey_name + "c1");
      auto eval_key0_ = make_input(level);
      eval_key0_.set_limbtype(limbtype_);
      eval_key0_.set_extension_size(extension_size);
      eval_key0_.set_symbol(evalkey_name + "c0");
      auto eval_key1_ = make_input(level);
      eval_key1_.set_limbtype(limbtype_);
      eval_key1_.set_extension_size(extension_size);
      eval_key1_.set_symbol(evalkey_name + "c1");

      evalkey_index = eval_key0.term_share_idx();
      evalkey_input_indices[evalkey_map_idx] = evalkey_index;
      bn0 = eval_key0.bignode_index();
      bn1 = eval_key1.bignode_index();
      bn0_ = eval_key0_.bignode_index();
      bn1_ = eval_key1_.bignode_index();
      evalkey_input_bn0[evalkey_map_idx] = bn0;
      evalkey_input_bn1[evalkey_map_idx] = bn1;
      evalkey_input_bn0_[evalkey_map_idx] = bn0_;
      evalkey_input_bn1_[evalkey_map_idx] = bn1_;
    }

    PolyPair evk0;
    PolyPair evk1;
    Polynomial poly0;
    Polynomial poly0_;
    Polynomial poly1;
    Polynomial poly1_;

    auto &term0 = term_shares[evalkey_index];
    auto &term1 = term_shares[evalkey_index + 1];
    auto &term0_ = term_shares[evalkey_index + 2];
    auto &term1_ = term_shares[evalkey_index + 3];
    term0->term()->set_as_eval_key(evk_type, 0,
                                   evk_dont_split_shares); // 0, not random
    term0_->term()->set_as_eval_key(evk_type, 0,
                                    evk_dont_split_shares); // 0, not random
    term1->term()->set_as_eval_key(evk_type, 1,
                                   evk_dont_split_shares); // 1, random part
    term1_->term()->set_as_eval_key(evk_type, 1,
                                    evk_dont_split_shares); // 1, random part

    poly0 = Polynomial(term0, level);
    poly1 = Polynomial(term1, level);
    poly0_ = Polynomial(term0_, level);
    poly1_ = Polynomial(term1_, level);
    poly0.set_bignode_idx(bn0);
    poly1.set_bignode_idx(bn1);
    poly0_.set_bignode_idx(bn0_);
    poly1_.set_bignode_idx(bn1_);

    evk0 = PolyPair(poly0, poly0_);
    evk1 = PolyPair(poly1, poly1_);
    return std::move(std::pair<PolyPair, PolyPair>(evk0, evk1));
  }

  using InstructionSharedPtr = std::shared_ptr<PolynomialInstruction>;
  Polynomial make_input(uint16_t level) {

    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;
    term->set_as_input();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;

    auto poly = Polynomial(term_share, level);
    auto node = create_bignode();

    node->push_instruction(std::make_shared<InpInstruction>(poly));
    poly.set_bignode_idx(node->index());
    return poly;
  }

  Polynomial make_plaintext(uint16_t level) {
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;
    term->set_as_plaintext();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;

    auto poly = Polynomial(term_share, level);
    auto node = create_bignode();
    node->push_instruction(std::make_shared<InpInstruction>(poly));
    poly.set_bignode_idx(node->index());
    return poly;
  }

  Polynomial make_scalar(uint16_t level) {
    auto index = nextTermIndex++;
    auto term = std::make_shared<Term>(index);
    terms[index] = term;
    term->set_as_plaintext();
    term->set_as_scalar();

    auto index_ts = nextTermShareIndex++;
    auto term_share = std::make_shared<TermShare>(index_ts, term, level);
    term_shares[index_ts] = term_share;

    auto poly = Polynomial(term_share, level);
    auto node = create_bignode();
    node->push_instruction(std::make_shared<InpInstruction>(poly));
    poly.set_bignode_idx(node->index());
    return poly;
  }

  // decltype(Chains)&Chains = Chains;
  std::shared_ptr<Chain> create_chain() {
    auto ch = std::make_shared<Chain>(Chains.size());
    Chains.push_back(ch);
    return ch;
  }

  std::shared_ptr<BigNode> create_bignode() {
    std::shared_ptr<Group> gr(new Group(Groups.size(), BigNodes.size()));
    std::shared_ptr<BigNode> bn(new BigNode(BigNodes.size(), Groups.size(),
                                            current_partition_size,
                                            current_partition_id));
    Groups.push_back(gr);
    BigNodes.push_back(bn);
    assert(current_partition_size <= num_partitions);
    return bn;
  }

  std::shared_ptr<BigNode> create_bignode(uint8_t partition_size,
                                          uint8_t partition_id) {
    std::shared_ptr<Group> gr(new Group(Groups.size(), BigNodes.size()));
    std::shared_ptr<BigNode> bn(new BigNode(BigNodes.size(), Groups.size(),
                                            partition_size, partition_id));
    Groups.push_back(gr);
    BigNodes.push_back(bn);
    assert(current_partition_size <= num_partitions);
    return bn;
  }

  std::shared_ptr<BigNode>
  create_bignode_in_group(std::shared_ptr<BigNode> src) {
    std::shared_ptr<BigNode> bn(new BigNode(BigNodes.size(), src->group_index(),
                                            current_partition_size,
                                            current_partition_id));
    BigNodes.push_back(bn);
    assert(current_partition_size <= num_partitions);
    return bn;
  }

  std::vector<BigNodeIndexType> split_bignode(std::shared_ptr<BigNode> bn) {
    decltype(bn) new_bn;
    std::map<TermIndexType, Polynomial> termshare_remap;

    std::vector<BigNodeIndexType> bn_split;
    using OpCode = PolynomialInstruction::OpCode;

    std::set<uint16_t> accumulated;
    std::map<uint64_t, Polynomial> prev;

    auto split_size = bn->split_up().size();
    assert(split_size != 0);
    auto cnt = 0;

    auto process_instruction =
        [&](const InstructionSharedPtr &instr_, const std::set<uint16_t> &split,
            size_t &jol_instruction_count) -> InstructionSharedPtr {
      bool insert_instruction = true;
      auto instr = instr_->clone();

      if (instr->opcode() == OpCode::Int) {
        auto srcs = instr->srcs();
        assert(srcs.size() == 1);
        auto &shares = srcs[0]->shares();
        assert(shares.size() == 1);
        auto share = *shares.begin();
        if (split.find(share) != split.end()) {
          // new_bn->puse_instruction(std::move(instr));
          return instr;
        }
        return nullptr;
      }

      auto srcs = instr->srcs();
      auto src_count = 0;
      for (auto &src : srcs) {
        if (instr->opcode() == PolynomialInstruction::OpCode::Int) {
          assert(0);
        }

        if (instr->opcode() == OpCode::SuD && src_count == 1) {
          continue;
        }

        auto old_termshare_idx = src->term_share_idx();
        auto it = termshare_remap.find(old_termshare_idx);
        if (it == termshare_remap.end()) {
          *src = make_new_term_share_from(src);
          termshare_remap[old_termshare_idx] = *src;
        } else {
          *src = it->second;
        }

        auto &shares = src->shares();
        std::set<uint16_t> intersect;
        std::set_intersection(shares.begin(), shares.end(), split.begin(),
                              split.end(),
                              std::inserter(intersect, intersect.begin()));
        src->set_shares(intersect);
        src_count++;
      }

      auto dests = instr->dests();
      for (auto &dst : dests) {
        auto &shares = dst->shares();
        std::set<uint16_t> intersect;
        std::set_intersection(shares.begin(), shares.end(), split.begin(),
                              split.end(),
                              std::inserter(intersect, intersect.begin()));

        if (instr->opcode() == OpCode::JoL) {
          jol_instruction_count++;
          if (cnt != 0) {
            auto instr_j =
                std::dynamic_pointer_cast<JoinLocalInstruction>(instr);
            assert(instr_j != nullptr);
            instr_j->add_src2(prev[jol_instruction_count]);
          }
          if (cnt != split_size - 1) {
            *dst = make_copy_treg_from(dst);
            dst->set_output(false);
            prev[jol_instruction_count] = *dst;
            if (prev[jol_instruction_count].limbtype() == Term::LimbType::Spl) {
              prev[jol_instruction_count].set_limbtype(Term::LimbType::Usp);
            }
          }
          dst->set_shares(accumulated);
          if (cnt == 0) {
            auto src_ = instr->srcs()[0];
            auto dst_ = instr->dests()[0];
            *(src_->term_share()) = *(dst_->term_share());
            insert_instruction = false;
          }
        } else {
          auto old_termshare_idx = dst->term_share_idx();
          auto it = termshare_remap.find(old_termshare_idx);
          if (it == termshare_remap.end()) {
            *dst = make_new_term_share_from(dst);
            termshare_remap[old_termshare_idx] = *dst;
          } else {
            *dst = it->second;
          }
          dst->set_shares(intersect);
        }
        dst->set_bignode_idx(new_bn->index());
      }
      if (insert_instruction) {
        return instr;
      }
      return nullptr;
    };

    for (auto &split : bn->split_up()) {
      termshare_remap.clear();
      new_bn = create_bignode(bn->partition_size(), bn->partition_id());
      auto new_bn_idx = new_bn->index();
      auto &instrs = bn->instructions();

      decltype(accumulated) uni;
      set_union(split.begin(), split.end(), accumulated.begin(),
                accumulated.end(), std::inserter(uni, uni.begin()));
      accumulated = std::move(uni);

      bool insert_instruction = true;
      size_t jol_instruction_count = 0;
      for (auto &instr_ : instrs) {
        auto instr = process_instruction(instr_, split, jol_instruction_count);
        if (instr) {
          new_bn->push_instruction(std::move(instr));
        }
      }
      new_bn->set_ks_pl_idx(bn->ks_pl_idx());
      new_bn->set_ks_pl_no(bn->ks_pl_no());
      new_bn->set_inputs(bn->inputs());
      new_bn->set_outputs(bn->outputs());
      new_bn->set_dont_split_modular(bn->dont_split_modular());
      bn_split.push_back(new_bn->index());
      cnt++;
    }
    new_bn = nullptr;

    // Insert anything that is remaining
    {
      termshare_remap.clear();
      std::shared_ptr<BigNode> new_bn = nullptr;
      auto &instrs = bn->instructions();
      for (auto &instr_ : instrs) {
        bool insert_instr = false;
        auto instr = instr_->clone();

        if (instr->opcode() == OpCode::Int) {
          auto srcs = instr->srcs();
          assert(srcs.size() == 1);
          auto &shares = srcs[0]->shares();

          assert(shares.size() == 1);
          auto share = *shares.begin();
          if (accumulated.find(share) == accumulated.end()) {
            if (new_bn == nullptr) {
              new_bn = create_bignode(bn->partition_size(), bn->partition_id());
            }
            new_bn->push_instruction(std::move(instr));
          }
          continue;
        }

        auto srcs = instr->srcs();
        auto src_count = 0;
        for (auto &src : srcs) {

          if (instr->opcode() == PolynomialInstruction::OpCode::Int) {
            assert(0);
          }

          // if(src->limbtype() == Term::LimbType::Las){
          //   continue;
          // }
          if (instr->opcode() == OpCode::SuD && src_count == 1) {
            continue;
          }

          auto &shares = src->shares();
          decltype(accumulated) diff;
          std::set_difference(shares.begin(), shares.end(), accumulated.begin(),
                              accumulated.end(),
                              std::inserter(diff, diff.begin()));

          if (diff.size() != 0) {
            switch (instr->opcode()) {
            case OpCode::Pip1:
            case OpCode::Pip2:
            case OpCode::Pip3:
            case OpCode::Pip4:
            case OpCode::JoL:
              assert(0);
            default:
              break;
            }
            auto old_termshare_idx = src->term_share_idx();
            auto it = termshare_remap.find(old_termshare_idx);
            if (it == termshare_remap.end()) {
              *src = make_new_term_share_from(src);
              termshare_remap[old_termshare_idx] = *src;
            } else {
              *src = it->second;
            }
            src->set_shares(diff);
            insert_instr = true;
          }
          src_count++;
        }

        auto dests = instr->dests();
        for (auto &dst : dests) {
          auto &shares = dst->shares();
          decltype(accumulated) diff;
          std::set_difference(shares.begin(), shares.end(), accumulated.begin(),
                              accumulated.end(),
                              std::inserter(diff, diff.begin()));

          if (diff.size() != 0) {
            switch (instr->opcode()) {
            case OpCode::Pip1:
            case OpCode::Pip2:
            case OpCode::Pip3:
            case OpCode::Pip4:
            case OpCode::JoL:
              assert(0);
            default:
              break;
            }
            auto old_termshare_idx = dst->term_share_idx();
            auto it = termshare_remap.find(old_termshare_idx);
            if (it == termshare_remap.end()) {
              *dst = make_new_term_share_from(dst);
              termshare_remap[old_termshare_idx] = *dst;
            } else {
              *dst = it->second;
            }

            dst->set_shares(diff);
            insert_instr = true;
            if (new_bn == nullptr) {
              new_bn = create_bignode(bn->partition_size(), bn->partition_id());
            }
            dst->set_bignode_idx(new_bn->index());
          }
        }
        if (insert_instr) {
          new_bn->push_instruction(std::move(instr));
        }
      }
      if (new_bn != nullptr) {
        new_bn->set_ks_pl_idx(bn->ks_pl_idx());
        new_bn->set_inputs(bn->inputs());
        new_bn->set_outputs(bn->outputs());
        bn_split.push_back(new_bn->index());
      }
    }

    return bn_split;
  }

  std::vector<BigNodeIndexType>
  split_bignode_dont_split_modular(std::shared_ptr<BigNode> bn) {
    assert(num_partitions > 1);
    decltype(bn) new_bn;
    std::vector<BigNodeIndexType> bn_split;
    using OpCode = PolynomialInstruction::OpCode;
    assert(bn->dont_split_modular());

    auto bn_partition_size = bn->partition_size();
    auto bn_partition_start_id = bn->partition_size() * bn->partition_id();
    for (auto partition_id = 0; partition_id < num_partitions; partition_id++) {
      new_bn = create_bignode(bn_partition_size, bn->partition_id());
      if (partition_id < bn_partition_start_id) {
        // TODO: Check this
        bn_split.push_back(new_bn->index());
        continue;
      } else if (partition_id >= bn_partition_start_id + bn_partition_size) {
        // TODO: Check this
        bn_split.push_back(new_bn->index());
        continue;
      }

      auto new_bn_idx = new_bn->index();
      auto &instrs = bn->instructions();
      for (auto &instr_ : instrs) {

        if (instr_->opcode() == OpCode::Rec) {
          assert(0);
        }
        if (instr_->opcode() == OpCode::Drm) {
          auto srcs = instr_->srcs();
          assert(srcs.size() == 1);
          auto src = srcs[0];
          auto shares = src->shares();

          std::set<LimbIndexType> dist_shares;
          std::set<LimbIndexType> recv_shares;
          for (auto &share : shares) {
            if ((share % bn_partition_size) ==
                (partition_id % bn_partition_size)) {
              dist_shares.insert(share);
            } else {
              recv_shares.insert(share);
            }
          }
          std::shared_ptr<PolynomialInstruction> dist_instr;
          std::shared_ptr<PolynomialInstruction> mov_instr;
          if (instr_->opcode() == OpCode::Drm) {
            auto instr_drm =
                std::dynamic_pointer_cast<DistRecvMovInstruction>(instr_);
            assert(instr_drm != nullptr);
            dist_instr = instr_drm->make_dist_instruction();
            mov_instr = instr_drm->make_mov_instruction();
          } else {
            dist_instr = instr_->clone();
          }
          if (dist_shares.size() != 0) {
            // auto dist_instr = instr_->clone();
            auto dist_src = dist_instr->srcs()[0];
            *dist_src = make_new_term_share_from(dist_src);
            dist_src->set_shares(dist_shares);
            dist_src->set_bignode_idx(new_bn_idx);
            new_bn->push_instruction(std::move(dist_instr));
          }
          if (recv_shares.size() != 0) {
            auto instr_dis =
                std::dynamic_pointer_cast<DistInstruction>(dist_instr);
            assert(instr_dis != nullptr);
            auto recv_instr = instr_dis->make_recv_instruction();
            auto recv_dst = recv_instr->dests()[0];
            *recv_dst = make_new_term_share_from(recv_dst);
            recv_dst->set_shares(recv_shares);
            recv_dst->set_bignode_idx(new_bn_idx);
            new_bn->push_instruction(std::move(recv_instr));
          }
          if (mov_instr) {
            new_bn->push_instruction(std::move(mov_instr));
          }
          continue;
        }

        bool empty_instr = true;
        std::set<uint16_t> empty_shares;
        auto instr = instr_->clone();
        for (auto &src : instr->srcs()) {
          if (src->level() <= (partition_id % bn_partition_size)) {
            *src = make_new_term_share_from(src);
            src->set_shares(empty_shares);
            src->set_bignode_idx(new_bn_idx);
          } else {
            empty_instr = false;
          }
        }
        for (auto &dst : instr->dests()) {
          if (dst->level() <= (partition_id % bn_partition_size)) {
            *dst = make_new_term_share_from(dst);
            dst->set_shares(empty_shares);
            dst->set_bignode_idx(new_bn_idx);
          } else {
            empty_instr = false;
          }
        }
        if (!empty_instr) {
          new_bn->push_instruction(std::move(instr));
        }
      }
      new_bn->set_ks_pl_idx(bn->ks_pl_idx());
      new_bn->set_inputs(bn->inputs());
      new_bn->set_outputs(bn->outputs());
      new_bn->set_dont_split_modular(bn->dont_split_modular());
      bn_split.push_back(new_bn->index());
    }
    return bn_split;
  }

  std::vector<BigNodeIndexType>
  split_bignode_modular(std::shared_ptr<BigNode> bn) {

    assert(num_partitions > 1);
    decltype(bn) new_bn;
    std::vector<BigNodeIndexType> bn_split;
    using OpCode = PolynomialInstruction::OpCode;

    if (bn->dont_split_modular()) {
      return split_bignode_dont_split_modular(bn);
    }

    auto bn_partition_size = bn->partition_size();
    auto bn_partition_start_id = bn->partition_size() * bn->partition_id();
    for (auto partition_id = 0; partition_id < num_partitions; partition_id++) {
      new_bn = create_bignode(bn->partition_size(), bn->partition_id());
      if (partition_id < bn_partition_start_id) {
        // TODO: Check this
        bn_split.push_back(new_bn->index());
        continue;
      } else if (partition_id >= bn_partition_start_id + bn_partition_size) {
        // TODO: Check this
        bn_split.push_back(new_bn->index());
        continue;
      }
      auto new_bn_idx = new_bn->index();
      auto &instrs = bn->instructions();
      for (auto &instr_ : instrs) {
        if (instr_->opcode() == OpCode::Dis ||
            instr_->opcode() == OpCode::Drm) {
          auto srcs = instr_->srcs();
          assert(srcs.size() == 1);
          auto src = srcs[0];
          auto shares = src->shares();

          std::set<LimbIndexType> dist_shares;
          std::set<LimbIndexType> recv_shares;
          for (auto &share : shares) {
            if ((share % bn_partition_size) ==
                (partition_id % bn_partition_size)) {
              dist_shares.insert(share);
            } else {
              recv_shares.insert(share);
            }
          }
          std::shared_ptr<PolynomialInstruction> dist_instr;
          std::shared_ptr<PolynomialInstruction> mov_instr;
          if (instr_->opcode() == OpCode::Drm) {
            auto instr_drm =
                std::dynamic_pointer_cast<DistRecvMovInstruction>(instr_);
            assert(instr_drm != nullptr);
            dist_instr = instr_drm->make_dist_instruction();
            mov_instr = instr_drm->make_mov_instruction();
          } else {
            dist_instr = instr_->clone();
          }
          if (dist_shares.size() != 0) {
            auto dist_src = dist_instr->srcs()[0];
            *dist_src = make_new_term_share_from(dist_src);
            dist_src->set_shares(dist_shares);
            dist_src->set_bignode_idx(new_bn_idx);
            new_bn->push_instruction(std::move(dist_instr));
          }
          if (recv_shares.size() != 0) {
            auto instr_dis =
                std::dynamic_pointer_cast<DistInstruction>(dist_instr);
            assert(instr_dis != nullptr);
            auto recv_instr = instr_dis->make_recv_instruction();
            auto recv_dst = recv_instr->dests()[0];
            *recv_dst = make_new_term_share_from(recv_dst);
            recv_dst->set_shares(recv_shares);
            recv_dst->set_bignode_idx(new_bn_idx);
            new_bn->push_instruction(std::move(recv_instr));
            if (recv_dst->limbtype() == Term::LimbType::Ext_Split) {
              recv_dst->set_interpret_extension_split_from_shares(true);
            }
          }
          if (mov_instr) {
            if (mov_instr->srcs()[0]->limbtype() == Term::LimbType::Ext_Split) {
              mov_instr->srcs()[0]->set_interpret_extension_split_from_shares(
                  true);
            }
            new_bn->push_instruction(std::move(mov_instr));
          }
          continue;
        }

        if (instr_->opcode() == OpCode::Rec) {
          auto instr = instr_->clone();
          auto instr_rec = std::dynamic_pointer_cast<ReceiveInstruction>(instr);
          auto srcs = instr_rec->srcs();
          assert(srcs.size() == 1);
          auto src = srcs[0];
          auto src_shares = src->shares();

          auto src_bn_partition_size = instr_rec->src_partition_size();
          auto src_bn_partition_start_id =
              instr_rec->src_partition_id() * src_bn_partition_size;

          std::set<LimbIndexType> src_shares_;
          for (auto &share : src_shares) {
            if (partition_id >= src_bn_partition_start_id &&
                partition_id <
                    (src_bn_partition_start_id + src_bn_partition_size)) {
              if ((share % src_bn_partition_size) ==
                  (partition_id % src_bn_partition_size)) {
                src_shares_.insert(share);
              }
            }
          }
          *src = make_new_term_share_from(src);
          src->set_shares(src_shares_);
          src->set_bignode_idx(new_bn_idx);

          auto dests = instr_rec->dests();
          assert(dests.size() == 1);
          auto dest = dests[0];
          auto dest_shares = dest->shares();

          auto dest_bn_partition_size = instr_rec->dest_partition_size();
          auto dest_bn_partition_start_id =
              instr_rec->dest_partition_id() * dest_bn_partition_size;

          std::set<LimbIndexType> dest_shares_;
          for (auto &share : dest_shares) {
            if (partition_id >= dest_bn_partition_start_id &&
                partition_id <
                    (dest_bn_partition_start_id + dest_bn_partition_size)) {
              if ((share % dest_bn_partition_size) ==
                  (partition_id % dest_bn_partition_size)) {
                dest_shares_.insert(share);
              }
            }
          }

          *dest = make_new_term_share_from(dest);
          dest->set_shares(dest_shares_);
          dest->set_bignode_idx(new_bn_idx);
          new_bn->push_instruction(instr_rec);
          continue;
        }

        bool empty_instr = false;
        auto instr = instr_->clone();
        auto srcs = instr->srcs();
        auto src_count = 0;
        for (auto &src : srcs) {
          if (instr->opcode() == OpCode::Mod ||
              instr->opcode() == OpCode::Rsv) {
            auto &shares = src->shares();
            *src = make_new_term_share_from(src);
            src->set_shares(shares);
            src->set_bignode_idx(new_bn_idx);
            continue;
          }
          if (instr->opcode() == OpCode::SuD && src_count == 1) {
            auto &shares = src->shares();
            *src = make_new_term_share_from(src);
            src->set_shares(shares);
            src->set_bignode_idx(new_bn_idx);
            continue;
          }

          auto &shares = src->shares();
          *src = make_new_term_share_from(src);
          std::set<LimbIndexType> new_shares;
          for (auto &share : shares) {
            if ((share % bn_partition_size) ==
                (partition_id % bn_partition_size)) {
              new_shares.insert(share);
            }
          }
          src->set_shares(new_shares);
          src->set_bignode_idx(new_bn_idx);
          src_count++;
        }
        auto dests = instr->dests();
        for (auto &dst : dests) {
          if (instr->opcode() == OpCode::Rsv) {
            auto &shares = dst->shares();
            *dst = make_new_term_share_from(dst);
            dst->set_shares(shares);
            dst->set_bignode_idx(new_bn_idx);
            continue;
          }
          auto &shares = dst->shares();
          std::set<LimbIndexType> new_shares;
          for (auto &share : shares) {
            if ((share % bn_partition_size) ==
                (partition_id % bn_partition_size)) {
              new_shares.insert(share);
            }
          }
          if (new_shares.empty()) {
            empty_instr = true;
          } else {
            *dst = make_new_term_share_from(dst);
            dst->set_shares(new_shares);
            dst->set_bignode_idx(new_bn_idx);
          }
        }
        if (!empty_instr) {
          new_bn->push_instruction(std::move(instr));
        } else if (instr_->opcode() == OpCode::Joi) {
          // Need to add empty join instructions to ensure all partitions have a
          // synchronisaton barrier
          auto instr_joi =
              std::dynamic_pointer_cast<JoinGlobalInstruction>(instr_);
          assert(instr_joi != nullptr);
          auto sync_instr = instr_joi->make_sync_instruction();
          new_bn->push_instruction(std::move(sync_instr));
        }
      }
      new_bn->set_ks_pl_idx(bn->ks_pl_idx());
      new_bn->set_inputs(bn->inputs());
      new_bn->set_outputs(bn->outputs());
      bn_split.push_back(new_bn->index());
    }
    return bn_split;
  }

  void add(Ciphertext &output, const Frontend::Term::Ptr &args1,
           const Frontend::Term::Ptr &args2) {

    if (!isCipher(args1)) {
      assert(isCipher(args2));
      add(output, args2, args1);
      return;
    }
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    std::visit(
        Overloaded{
            [&](Ciphertext &input2) {
              output.ct0 = make_treg_from(input1.ct0);
              std::shared_ptr<BigNode> bn0 = create_bignode();
              bn0->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Add, output.ct0, input1.ct0, input2.ct0));
              output.ct0.set_bignode_idx(bn0->index());

              output.ct1 = make_treg_from(input1.ct1);
              std::shared_ptr<BigNode> bn1 = create_bignode();
              bn1->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Add, output.ct1, input1.ct1, input2.ct1));
              output.ct1.set_bignode_idx(bn1->index());

              if (input1.ct2.has_value() && input2.ct2.has_value()) {
                auto t2 = make_treg_from(input1.ct2.value());
                std::shared_ptr<BigNode> bn2 = create_bignode();
                bn2->push_instruction(std::make_shared<BinOpInstruction>(
                    OpCode::Add, t2, input1.ct2.value(), input2.ct2.value()));
                t2.set_bignode_idx(bn2->index());
                output.ct2 = t2;
              } else if (input1.ct2.has_value()) {
                output.ct2 = input1.ct2;
              } else {
                output.ct2 = input2.ct2;
              }
            },
            [&](Plaintext &input2) {
              output.ct0 = make_treg_from(input1.ct0);
              std::shared_ptr<BigNode> bn0 = create_bignode();
              bn0->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Add, output.ct0, input1.ct0, input2.pt));
              output.ct0.set_bignode_idx(bn0->index());
              output.ct1 = input1.ct1;
              output.ct2 = input1.ct2;
            },
            [&](CiphertextVector &) {
              throw std::runtime_error("Unsupported operation encountered");
            }},
        Objects.at(args2));
  }

  void negate(Ciphertext &output, const Frontend::Term::Ptr &args1) {

    assert(isCipher(args1));
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;

    output.ct0 = make_treg_from(input1.ct0);
    std::shared_ptr<BigNode> bn0 = create_bignode();
    bn0->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Neg, output.ct0, input1.ct0));
    output.ct0.set_bignode_idx(bn0->index());

    output.ct1 = make_treg_from(input1.ct1);
    std::shared_ptr<BigNode> bn1 = create_bignode();
    bn1->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Neg, output.ct1, input1.ct1));
    output.ct1.set_bignode_idx(bn1->index());

    if (input1.ct2.has_value()) {
      auto t2 = make_treg_from(input1.ct2.value());
      std::shared_ptr<BigNode> bn2 = create_bignode();
      bn2->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Neg, t2, input1.ct2.value()));
      t2.set_bignode_idx(bn2->index());
      output.ct2 = t2;
    }
  }

  void sub(Ciphertext &output, const Frontend::Term::Ptr &args1,
           const Frontend::Term::Ptr &args2, bool invert) {

    if (!isCipher(args1)) {
      assert(isCipher(args2));
      sub(output, args2, args1, true);
      return;
    }
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    std::visit(
        Overloaded{
            [&](Ciphertext &input2) {
              assert(invert == false);
              output.ct0 = make_treg_from(input1.ct0);
              std::shared_ptr<BigNode> bn0 = create_bignode();
              bn0->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Sub, output.ct0, input1.ct0, input2.ct0));
              output.ct0.set_bignode_idx(bn0->index());

              output.ct1 = make_treg_from(input1.ct1);
              std::shared_ptr<BigNode> bn1 = create_bignode();
              bn1->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Sub, output.ct1, input1.ct1, input2.ct1));
              output.ct1.set_bignode_idx(bn1->index());
              if (input1.ct2.has_value() && input2.ct2.has_value()) {
                auto t2 = make_treg_from(input1.ct2.value());
                std::shared_ptr<BigNode> bn2 = create_bignode();
                bn2->push_instruction(std::make_shared<BinOpInstruction>(
                    OpCode::Sub, t2, input1.ct2.value(), input2.ct2.value()));
                t2.set_bignode_idx(bn2->index());
                output.ct2 = t2;
              } else if (input2.ct2.has_value()) {
                auto t2 = make_treg_from(input2.ct2.value());
                std::shared_ptr<BigNode> bn2 = create_bignode();
                bn2->push_instruction(std::make_shared<UnOpInstruction>(
                    OpCode::Neg, t2, input2.ct2.value()));
                t2.set_bignode_idx(bn2->index());
                output.ct2 = t2;
              } else {
                output.ct2 = input1.ct2;
              }
            },
            [&](Plaintext &input2) {
              output.ct0 = make_treg_from(input1.ct0);
              std::shared_ptr<BigNode> bn0 = create_bignode();
              if (invert) {
                auto t0 = make_treg_from(input1.ct1);
                bn0->push_instruction(std::make_shared<UnOpInstruction>(
                    OpCode::Neg, t0, input1.ct0));
                t0.set_bignode_idx(bn0->index());
                bn0->push_instruction(std::make_shared<BinOpInstruction>(
                    OpCode::Add, output.ct0, t0, input2.pt));

                output.ct1 = make_treg_from(input1.ct1);
                std::shared_ptr<BigNode> bn1 = create_bignode();
                bn1->push_instruction(std::make_shared<UnOpInstruction>(
                    OpCode::Neg, output.ct1, input1.ct1));
                output.ct1.set_bignode_idx(bn1->index());

                if (input1.ct2.has_value()) {
                  auto t2 = make_treg_from(input1.ct2.value());
                  std::shared_ptr<BigNode> bn2 = create_bignode();
                  bn2->push_instruction(std::make_shared<UnOpInstruction>(
                      OpCode::Neg, t2, input1.ct2.value()));
                  t2.set_bignode_idx(bn2->index());
                  output.ct2 = t2;
                }

              } else {
                bn0->push_instruction(std::make_shared<BinOpInstruction>(
                    OpCode::Sub, output.ct0, input1.ct0, input2.pt));
                output.ct1 = input1.ct1;
                output.ct2 = input1.ct2;
              }

              output.ct0.set_bignode_idx(bn0->index());
            },
            [&](CiphertextVector &) {
              throw std::runtime_error("Unsupported operation encountered");
            }},
        Objects.at(args2));
  }

  void mul(Ciphertext &output, const Frontend::Term::Ptr &args1,
           const Frontend::Term::Ptr &args2) {

    if (!isCipher(args1)) {
      assert(isCipher(args2));
      mul(output, args2, args1);
      return;
    }
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    std::visit(
        Overloaded{[&](Ciphertext &input2) {
                     assert(!input1.ct2.has_value());
                     assert(!input2.ct2.has_value());

                     auto t2 = make_treg_from(input1.ct1);

                     std::shared_ptr<BigNode> bn2 = create_bignode();
                     bn2->push_instruction(std::make_shared<BinOpInstruction>(
                         OpCode::Mul, t2, input1.ct1, input2.ct1));
                     t2.set_bignode_idx(bn2->index());

                     output.ct2 = t2;

                     output.ct0 = make_treg_from(input1.ct0);
                     auto bn0 = create_bignode();
                     bn0->push_instruction(std::make_shared<BinOpInstruction>(
                         OpCode::Mul, output.ct0, input1.ct0, input2.ct0));
                     output.ct0.set_bignode_idx(bn0->index());

                     auto t01 = make_treg_from(input1.ct0);
                     auto t10 = make_treg_from(input1.ct1);
                     auto t1 = make_treg_from(input1.ct1);
                     output.ct1 = make_treg_from(input1.ct1);
                     std::shared_ptr<BigNode> bn1 = create_bignode();
                     bn1->push_instruction(std::make_shared<BinOpInstruction>(
                         OpCode::Mul, t01, input1.ct0, input2.ct1));
                     t01.set_bignode_idx(bn1->index());
                     bn1->push_instruction(std::make_shared<BinOpInstruction>(
                         OpCode::Mul, t10, input1.ct1, input2.ct0));
                     t10.set_bignode_idx(bn1->index());
                     bn1->push_instruction(std::make_shared<BinOpInstruction>(
                         OpCode::Add, t1, t01, t10));
                     t1.set_bignode_idx(bn1->index());
                     output.ct1 = t1;
                   },
                   [&](Plaintext &input2) {
                     output.ct0 = make_treg_from(input1.ct0);
                     std::shared_ptr<BigNode> bn0 = create_bignode();
                     bn0->push_instruction(std::make_shared<BinOpInstruction>(
                         OpCode::MuP, output.ct0, input1.ct0, input2.pt));
                     output.ct0.set_bignode_idx(bn0->index());

                     output.ct1 = make_treg_from(input1.ct1);
                     std::shared_ptr<BigNode> bn1 = create_bignode();
                     bn1->push_instruction(std::make_shared<BinOpInstruction>(
                         OpCode::MuP, output.ct1, input1.ct1, input2.pt));
                     output.ct1.set_bignode_idx(bn1->index());

                     if (input1.ct2.has_value()) {
                       auto t2 = make_treg_from(input1.ct2.value());
                       std::shared_ptr<BigNode> bn2 = create_bignode();
                       bn2->push_instruction(std::make_shared<BinOpInstruction>(
                           OpCode::MuP, t2, input1.ct2.value(), input2.pt));
                       t2.set_bignode_idx(bn2->index());

                       output.ct2 = t2;
                     }
                   },
                   [&](const CiphertextVector &input2) {
                     throw std::runtime_error(
                         "Unsupported operation encountered");
                   }},
        Objects.at(args2));
  }

  void multiply_rotate_accumulate(Ciphertext &output,
                                  const std::vector<Frontend::Term::Ptr> &args,
                                  std::vector<int32_t> rotation_indices) {
    assert((args.size() == rotation_indices.size() + 1));
    assert(isCipher(args[0]));
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args[0]));
    std::vector<Plaintext> plaintext_inputs;

    assert(!input1.ct2.has_value());
    for (int i = 1; i < args.size(); i++) {
      assert(isPlain(args[i]));
      Plaintext &plaintext_input = std::get<Plaintext>(Objects.at(args[i]));
      plaintext_inputs.push_back(plaintext_input);
    }

    // bool has_multiply = !plaintext_inputs.empty();
    output.level = input1.level;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    auto level = input1.level;

    auto key_switch_type = KeySwitchType::Aggregation;
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);

    using LimbType = Term::LimbType;

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;

    auto count = 0;
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto bound = ii < (level % dnum_loc) ? split_size + 1 : split_size;
      std::set<uint16_t> s;
      for (auto i = 0; i < bound; i++) {
        s.insert(s.end(), count);
        count++;
      }
      split.push_back(s);
    }

    std::optional<Polynomial> zero_rot_ct1_sum;
    Polynomial ct0_sum;
    std::optional<std::pair<Polynomial, Polynomial>> k0_sum;
    std::optional<std::pair<Polynomial, Polynomial>> k1_sum;

    for (int i = 0; i < rotation_indices.size(); i++) {
      auto rot_idx = rotation_indices[i];
      auto bn = create_bignode();
      Polynomial rot0;
      Polynomial rot1;
      Polynomial prod0;
      Polynomial prod1;

      prod0 = make_treg_from(input1.ct0);
      prod1 = make_treg_from(input1.ct1);
      auto &plaintext = plaintext_inputs.at(i);
      bn->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::MuP, prod0, input1.ct0, plaintext.pt));
      bn->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::MuP, prod1, input1.ct1, plaintext.pt));
      prod0.set_bignode_idx(bn->index());
      prod1.set_bignode_idx(bn->index());

      if (rot_idx == 0) {
        rot0 = prod0;
        rot1 = prod1;
      } else {
        rot0 = make_treg_from(prod0);
        rot1 = make_treg_from(prod1);
        bn->push_instruction(std::make_shared<UnOpInstruction>(
            OpCode::Rot, rot0, prod0, rot_idx));
        bn->push_instruction(std::make_shared<UnOpInstruction>(
            OpCode::Rot, rot1, prod1, rot_idx));
        rot0.set_bignode_idx(bn->index());
        rot1.set_bignode_idx(bn->index());
      }

      if (i == 0) {
        ct0_sum = rot0;
      } else {
        auto tsum = make_treg_from(rot0);
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum, rot0, ct0_sum));
        tsum.set_bignode_idx(bn->index());
        ct0_sum = tsum;
      }

      if (rot_idx == 0) {
        if (!zero_rot_ct1_sum.has_value()) {
          zero_rot_ct1_sum = rot1;
        } else {
          auto tsum = make_treg_from(rot1);
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum, rot1, zero_rot_ct1_sum.value()));
          tsum.set_bignode_idx(bn->index());
          zero_rot_ct1_sum = tsum;
        }
        continue;
      }

      auto pl_idx = get_pl_idx();
      auto b0 = make_bcor_from(rot1);
      b0.set_limbtype(LimbType::ExC);
      b0.set_extension_size(extension_size);

      auto evalkeys = get_evalkey(rot1, EvalKeyType::Rotation, rot_idx,
                                  key_switch_type, extension_size);
      bn = create_bignode();
      bn->set_ks_pl_no(1);
      bn->push_instruction(std::make_shared<PlInstruction1>(b0, rot1));
      bn->set_split_up(split);
      bn->set_ks_pl_idx(pl_idx);
      b0.set_bignode_idx(bn->index());

      std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn);

      auto t1 = make_treg_from(rot1);
      t1.set_limbtype(LimbType::Usp);
      auto t2 = make_treg_from(rot1);
      t2.set_limbtype(LimbType::Ext);
      t2.set_extension_size(extension_size);

      bn1->push_instruction(std::make_shared<PlInstruction8>(t1, t2, b0, rot1));
      bn1->set_split_up(split);
      bn1->set_ks_pl_head_idx(bn);
      bn1->set_ks_pl_idx(pl_idx);
      t1.set_bignode_idx(bn1->index());
      t2.set_bignode_idx(bn1->index());

      bn->set_next_bignode_in_group(bn1);

      std::pair<Polynomial, Polynomial> t3;
      t3.first = make_treg_from(t1);
      t3.first.set_limbtype(LimbType::Usp);
      t3.second = make_treg_from(t2);
      t3.second.set_limbtype(LimbType::Ext);
      t3.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t4;
      t4.first = make_treg_from(t1);
      t4.first.set_limbtype(LimbType::Usp);
      t4.second = make_treg_from(t2);
      t4.second.set_limbtype(LimbType::Ext);
      t4.second.set_extension_size(extension_size);

      auto evk0 = evalkeys.first;
      auto evk1 = evalkeys.second;
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.first, t1, evk0.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.second, t2, evk0.second));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.first, t1, evk1.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.second, t2, evk1.second));

      t3.first.set_bignode_idx(bn1->index());
      t3.second.set_bignode_idx(bn1->index());
      t4.first.set_bignode_idx(bn1->index());
      t4.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t5;
      t5.first = make_treg_from(t1);
      t5.first.set_limbtype(LimbType::Usp);
      t5.second = make_treg_from(t2);
      t5.second.set_limbtype(LimbType::Ext);
      t5.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t6;
      t6.first = make_treg_from(t1);
      t6.first.set_limbtype(LimbType::Usp);
      t6.second = make_treg_from(t2);
      t6.second.set_limbtype(LimbType::Ext);
      t6.second.set_extension_size(extension_size);

      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

      t5.first.set_bignode_idx(bn1->index());
      t5.second.set_bignode_idx(bn1->index());
      t6.first.set_bignode_idx(bn1->index());
      t6.second.set_bignode_idx(bn1->index());

      bn1 = create_bignode();

      if (!k0_sum.has_value()) {
        k0_sum = t5;
      } else {
        auto &k0_sum_val = k0_sum.value();
        std::pair<Polynomial, Polynomial> tsum;
        tsum.first = make_treg_from(t5.first);
        tsum.first.set_limbtype(LimbType::Usp);
        tsum.second = make_treg_from(t5.second);
        tsum.second.set_limbtype(LimbType::Ext);
        tsum.second.set_extension_size(extension_size);
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.first, t5.first, k0_sum_val.first));
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.second, t5.second, k0_sum_val.second));
        tsum.first.set_bignode_idx(bn1->index());
        tsum.second.set_bignode_idx(bn1->index());
        k0_sum = tsum;
      }

      if (!k1_sum.has_value()) {
        k1_sum = t6;
      } else {
        auto &k1_sum_val = k1_sum.value();
        std::pair<Polynomial, Polynomial> tsum;
        tsum.first = make_treg_from(t6.first);
        tsum.first.set_limbtype(LimbType::Usp);
        tsum.second = make_treg_from(t6.second);
        tsum.second.set_limbtype(LimbType::Ext);
        tsum.second.set_extension_size(extension_size);
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.first, t6.first, k1_sum_val.first));
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.second, t6.second, k1_sum_val.second));
        tsum.first.set_bignode_idx(bn1->index());
        tsum.second.set_bignode_idx(bn1->index());
        k1_sum = tsum;
      }
    }

    assert(k0_sum.has_value());
    assert(k1_sum.has_value());

    auto bn = create_bignode();
    auto b1 = make_bcor_from(k0_sum.value().first);
    b1.set_limbtype(LimbType::Usp);
    bn->push_instruction(
        std::make_shared<PlInstruction1>(b1, k0_sum.value().second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(k1_sum.value().first);
    b2.set_limbtype(LimbType::Usp);
    bn2->push_instruction(
        std::make_shared<PlInstruction1>(b2, k1_sum.value().second));
    b2.set_bignode_idx(bn2->index());

    Polynomial t11 = make_treg_from(k0_sum.value().first);
    bn2->push_instruction(
        std::make_shared<PlInstruction7>(t11, k0_sum.value().first, b1));
    t11.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn);
    bn3->set_ks_pl_no(3);

    Polynomial t12 = make_treg_from(k1_sum.value().first);
    if (dnum_glo != 1) {
      t11.set_limbtype(LimbType::Usp);
      t11.set_join_reqd(true);
      t12.set_limbtype(LimbType::Usp);
      t12.set_join_reqd(true);
    }
    bn3->push_instruction(
        std::make_shared<PlInstruction7>(t12, k1_sum.value().first, b2));
    t12.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);

    auto bn4 = create_bignode();
    Polynomial t13 = make_treg_from(t11);
    bn4->push_instruction(
        std::make_shared<BinOpInstruction>(OpCode::Add, t13, ct0_sum, t11));
    t13.set_bignode_idx(bn4->index());

    Polynomial t14 = make_treg_from(t12);
    if (zero_rot_ct1_sum.has_value()) {
      auto bn5 = create_bignode();
      bn5->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Add, t14, zero_rot_ct1_sum.value(), t12));
      t14.set_bignode_idx(bn5->index());
    } else {
      t14 = t12;
    }

    output.ct0 = t13;
    output.ct1 = t14;
    return;
  }

  void rotate_multiply_accumulate(Ciphertext &output,
                                  const std::vector<Frontend::Term::Ptr> &args,
                                  std::vector<int32_t> rotation_indices) {
    assert((args.size() == rotation_indices.size() + 1) || (args.size() == 1));
    assert(isCipher(args[0]));
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args[0]));
    assert(!input1.ct2.has_value());
    std::vector<Plaintext> plaintext_inputs;
    for (int i = 1; i < args.size(); i++) {
      assert(isPlain(args[i]));
      Plaintext &plaintext_input = std::get<Plaintext>(Objects.at(args[i]));
      plaintext_inputs.push_back(plaintext_input);
    }

    bool has_multiply = !plaintext_inputs.empty();
    output.level = input1.level;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    auto level = input1.level;

    auto key_switch_type = KeySwitchType::Aggregation;
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);

    using LimbType = Term::LimbType;

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;

    auto count = 0;
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto bound = ii < (level % dnum_loc) ? split_size + 1 : split_size;
      std::set<uint16_t> s;
      for (auto i = 0; i < bound; i++) {
        s.insert(s.end(), count);
        count++;
      }
      split.push_back(s);
    }

    std::optional<Polynomial> zero_rot_ct1_sum;
    Polynomial ct0_sum;
    std::optional<std::pair<Polynomial, Polynomial>> k0_sum;
    std::optional<std::pair<Polynomial, Polynomial>> k1_sum;

    for (int i = 0; i < rotation_indices.size(); i++) {
      auto rot_idx = rotation_indices[i];
      auto bn = create_bignode();
      Polynomial rot0;
      Polynomial rot1;
      if (rot_idx == 0) {
        rot0 = input1.ct0;
        rot1 = input1.ct1;
      } else {
        rot0 = make_treg_from(input1.ct0);
        rot1 = make_treg_from(input1.ct1);
        bn->push_instruction(std::make_shared<UnOpInstruction>(
            OpCode::Rot, rot0, input1.ct0, rot_idx));
        bn->push_instruction(std::make_shared<UnOpInstruction>(
            OpCode::Rot, rot1, input1.ct1, rot_idx));
        rot0.set_bignode_idx(bn->index());
        rot1.set_bignode_idx(bn->index());
      }
      Polynomial prod0;
      Polynomial prod1;
      if (has_multiply) {
        prod0 = make_treg_from(rot0);
        prod1 = make_treg_from(rot1);
        auto &plaintext = plaintext_inputs.at(i);
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::MuP, prod0, rot0, plaintext.pt));
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::MuP, prod1, rot1, plaintext.pt));
        prod0.set_bignode_idx(bn->index());
        prod1.set_bignode_idx(bn->index());
      } else {
        prod0 = rot0;
        prod1 = rot1;
      }

      if (i == 0) {
        ct0_sum = prod0;
      } else {
        auto tsum = make_treg_from(prod0);
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum, prod0, ct0_sum));
        tsum.set_bignode_idx(bn->index());
        ct0_sum = tsum;
      }

      if (rot_idx == 0) {
        if (!zero_rot_ct1_sum.has_value()) {
          zero_rot_ct1_sum = prod1;
        } else {
          auto tsum = make_treg_from(prod1);
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum, prod1, zero_rot_ct1_sum.value()));
          tsum.set_bignode_idx(bn->index());
          zero_rot_ct1_sum = tsum;
        }
        continue;
      }

      auto pl_idx = get_pl_idx();
      auto b0 = make_bcor_from(prod1);
      b0.set_limbtype(LimbType::ExC);
      b0.set_extension_size(extension_size);

      auto evalkeys = get_evalkey(prod1, EvalKeyType::Rotation, rot_idx,
                                  key_switch_type, extension_size);
      bn = create_bignode();
      bn->set_ks_pl_no(1);
      bn->push_instruction(std::make_shared<PlInstruction1>(b0, prod1));
      bn->set_split_up(split);
      bn->set_ks_pl_idx(pl_idx);
      b0.set_bignode_idx(bn->index());

      std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn);

      auto t1 = make_treg_from(prod1);
      t1.set_limbtype(LimbType::Usp);
      auto t2 = make_treg_from(prod1);
      t2.set_limbtype(LimbType::Ext);
      t2.set_extension_size(extension_size);

      bn1->push_instruction(
          std::make_shared<PlInstruction8>(t1, t2, b0, prod1));
      bn1->set_split_up(split);
      bn1->set_ks_pl_head_idx(bn);
      bn1->set_ks_pl_idx(pl_idx);
      t1.set_bignode_idx(bn1->index());
      t2.set_bignode_idx(bn1->index());

      bn->set_next_bignode_in_group(bn1);

      std::pair<Polynomial, Polynomial> t3;
      t3.first = make_treg_from(t1);
      t3.first.set_limbtype(LimbType::Usp);
      t3.second = make_treg_from(t2);
      t3.second.set_limbtype(LimbType::Ext);
      t3.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t4;
      t4.first = make_treg_from(t1);
      t4.first.set_limbtype(LimbType::Usp);
      t4.second = make_treg_from(t2);
      t4.second.set_limbtype(LimbType::Ext);
      t4.second.set_extension_size(extension_size);

      auto evk0 = evalkeys.first;
      auto evk1 = evalkeys.second;
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.first, t1, evk0.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.second, t2, evk0.second));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.first, t1, evk1.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.second, t2, evk1.second));

      t3.first.set_bignode_idx(bn1->index());
      t3.second.set_bignode_idx(bn1->index());
      t4.first.set_bignode_idx(bn1->index());
      t4.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t5;
      t5.first = make_treg_from(t1);
      t5.first.set_limbtype(LimbType::Usp);
      t5.second = make_treg_from(t2);
      t5.second.set_limbtype(LimbType::Ext);
      t5.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t6;
      t6.first = make_treg_from(t1);
      t6.first.set_limbtype(LimbType::Usp);
      t6.second = make_treg_from(t2);
      t6.second.set_limbtype(LimbType::Ext);
      t6.second.set_extension_size(extension_size);

      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

      t5.first.set_bignode_idx(bn1->index());
      t5.second.set_bignode_idx(bn1->index());
      t6.first.set_bignode_idx(bn1->index());
      t6.second.set_bignode_idx(bn1->index());

      bn1 = create_bignode();

      if (!k0_sum.has_value()) {
        k0_sum = t5;
      } else {
        auto &k0_sum_val = k0_sum.value();
        std::pair<Polynomial, Polynomial> tsum;
        tsum.first = make_treg_from(t5.first);
        tsum.first.set_limbtype(LimbType::Usp);
        tsum.second = make_treg_from(t5.second);
        tsum.second.set_limbtype(LimbType::Ext);
        tsum.second.set_extension_size(extension_size);
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.first, t5.first, k0_sum_val.first));
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.second, t5.second, k0_sum_val.second));
        tsum.first.set_bignode_idx(bn1->index());
        tsum.second.set_bignode_idx(bn1->index());
        k0_sum = tsum;
      }

      if (!k1_sum.has_value()) {
        k1_sum = t6;
      } else {
        auto &k1_sum_val = k1_sum.value();
        std::pair<Polynomial, Polynomial> tsum;
        tsum.first = make_treg_from(t6.first);
        tsum.first.set_limbtype(LimbType::Usp);
        tsum.second = make_treg_from(t6.second);
        tsum.second.set_limbtype(LimbType::Ext);
        tsum.second.set_extension_size(extension_size);
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.first, t6.first, k1_sum_val.first));
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.second, t6.second, k1_sum_val.second));
        tsum.first.set_bignode_idx(bn1->index());
        tsum.second.set_bignode_idx(bn1->index());
        k1_sum = tsum;
      }
    }

    assert(k0_sum.has_value());
    assert(k1_sum.has_value());

    auto bn = create_bignode();
    auto b1 = make_bcor_from(k0_sum.value().first);
    b1.set_limbtype(LimbType::Usp);
    bn->push_instruction(
        std::make_shared<PlInstruction1>(b1, k0_sum.value().second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(k1_sum.value().first);
    b2.set_limbtype(LimbType::Usp);
    bn2->push_instruction(
        std::make_shared<PlInstruction1>(b2, k1_sum.value().second));
    b2.set_bignode_idx(bn2->index());

    Polynomial t11 = make_treg_from(k0_sum.value().first);
    bn2->push_instruction(
        std::make_shared<PlInstruction7>(t11, k0_sum.value().first, b1));
    t11.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn);
    bn3->set_ks_pl_no(3);

    Polynomial t12 = make_treg_from(k1_sum.value().first);
    if (dnum_glo != 1) {
      t11.set_limbtype(LimbType::Usp);
      t11.set_join_reqd(true);
      t12.set_limbtype(LimbType::Usp);
      t12.set_join_reqd(true);
    }
    bn3->push_instruction(
        std::make_shared<PlInstruction7>(t12, k1_sum.value().first, b2));
    t12.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);

    auto bn4 = create_bignode();
    Polynomial t13 = make_treg_from(t11);
    bn4->push_instruction(
        std::make_shared<BinOpInstruction>(OpCode::Add, t13, ct0_sum, t11));
    t13.set_bignode_idx(bn4->index());

    Polynomial t14 = make_treg_from(t12);
    if (zero_rot_ct1_sum.has_value()) {
      auto bn5 = create_bignode();
      bn5->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Add, t14, zero_rot_ct1_sum.value(), t12));
      t14.set_bignode_idx(bn5->index());
    } else {
      t14 = t12;
    }

    output.ct0 = t13;
    output.ct1 = t14;
    return;
  }

  std::vector<Polynomial>
  bsgs_babystep_multiply_accumulate_keyswitch_ct0_iterations(
      Polynomial &input1, const std::vector<Plaintext> &plaintexts,
      const std::vector<int32_t> babystep_rotation_indices,
      const uint32_t num_giantsteps) {

    using OpCode = PolynomialInstruction::OpCode;
    std::vector<Polynomial> babystep_rotations;
    std::shared_ptr<BigNode> bn1 = nullptr;

    for (int i = 0; i < babystep_rotation_indices.size(); i++) {

      bn1 = create_bignode();
      auto rot_idx = babystep_rotation_indices[i];
      Polynomial t1;
      if (rot_idx == 0) {
        babystep_rotations.push_back(input1);
        continue;
      }
      t1 = make_treg_from(input1);
      bn1->push_instruction(
          std::make_shared<UnOpInstruction>(OpCode::Rot, t1, input1, rot_idx));
      t1.set_bignode_idx(bn1->index());
      babystep_rotations.push_back(t1);
    }

    assert(babystep_rotation_indices.size() == babystep_rotations.size());

    std::vector<Polynomial> babystep_accumulated;

    for (int j = 0; j < num_giantsteps; j++) {
      Polynomial prev;
      for (int i = 0; i < babystep_rotations.size(); i++) {

        auto &t1 = babystep_rotations[i];
        auto t2 = make_treg_from(t1);
        auto &plaintext = plaintexts[i + j * babystep_rotation_indices.size()];
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::MuP, t2, t1, plaintext.pt));
        t2.set_bignode_idx(bn1->index());

        Polynomial t3;
        if (i != 0) {
          t3 = make_treg_from(t2);
          bn1->push_instruction(
              std::make_shared<BinOpInstruction>(OpCode::Add, t3, t2, prev));
          t3.set_bignode_idx(bn1->index());
        } else {
          t3 = t2;
        }
        prev = t3;
      }
      babystep_accumulated.push_back(prev);
    }

    return babystep_accumulated;
  }

  std::tuple<std::vector<Polynomial>, std::vector<Polynomial>>
  bsgs_babystep_multiply_accumulate_keyswitch_iterations(
      const Ciphertext &input,
      const std::vector<int32_t> babystep_rotation_indices) {

    using LimbType = Term::LimbType;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;
    std::shared_ptr<BigNode> bn;

    std::vector<Polynomial> ct0_babystep_rotations;
    std::vector<Polynomial> ct1_babystep_rotations;

    Polynomial ct0_prev = input.ct0;
    Polynomial ct1_prev = input.ct1;

    int32_t step_size = 0;

    for (int i = 0; i < babystep_rotation_indices.size(); i++) {

      int32_t rot_idx = babystep_rotation_indices[i] - step_size;
      step_size = babystep_rotation_indices[i];

      if (rot_idx == 0) {
        ct0_babystep_rotations.push_back(ct0_prev);
        ct1_babystep_rotations.push_back(ct1_prev);
        continue;
      }

      Ciphertext rotate_output;
      Ciphertext rotate_input;
      rotate_input.ct0 = ct0_prev;
      rotate_input.ct1 = ct1_prev;
      rotate_internal(rotate_output, rotate_input, rot_idx);
      ct0_babystep_rotations.push_back(rotate_output.ct0);
      ct1_babystep_rotations.push_back(rotate_output.ct1);
      ct0_prev = rotate_output.ct0;
      ct1_prev = rotate_output.ct1;
    }

    return std::make_tuple(ct0_babystep_rotations, ct1_babystep_rotations);
  }

  std::tuple<std::vector<Polynomial>, std::vector<Polynomial>>
  hoisted_basic_parallel_keyswitching_internal(
      const Ciphertext &input, const std::vector<int32_t> rotation_indices) {

    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    using LimbType = Term::LimbType;
    std::shared_ptr<BigNode> bn;

    std::vector<Polynomial> ct0_rotations;
    std::vector<Polynomial> ct1_rotations;

    // Polynomial ct0_prev = input.ct0;
    // Polynomial ct1_prev = input.ct1;

    auto key_switch_type = KeySwitchType::BasicParallel;
    auto level = input.ct1.level();
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);
    assert(dnum_glo == 1);

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;
    // for (auto ii = 0; ii < num_partitions; ii++) {
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto count = 0;
      std::set<uint16_t> s;
      for (auto l = ii; l < level; l += dnum_loc) {
        s.insert(l);
        count++;
        if (count == extension_size) {
          split.push_back(s);
          s.clear();
          count = 0;
        }
      }
      if (!s.empty()) {
        split.push_back(s);
        s.clear();
        count = 0;
      }
    }

    auto t0 = input.ct1;

    std::shared_ptr<BigNode> bn0 = nullptr;
    // if(num_partitions > 1){
    if (current_partition_size > 1) {
      std::shared_ptr<BigNode> bn_dist = create_bignode();
      auto t0_move = make_treg_from(t0);
      t0_move.set_limbtype(LimbType::Spl);
      t0_move.set_bignode_idx(bn_dist->index());
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t0_move, t0));
      t0 = t0_move;
      bn0 = bn_dist;
    } else {
      bn0 = create_bignode();
    }

    // XXX: The different parts of the evalkeys should be different bignodes
    // while reading
    auto pl_idx = get_pl_idx();
    // std::shared_ptr<BigNode> bn0 = create_bignode();
    bn0->set_ks_pl_no(1);

    auto b0 = make_bcor_from(t0);
    b0.set_limbtype(LimbType::PaE_Split);
    b0.set_extension_size(extension_size);
    bn0->push_instruction(std::make_shared<PlInstruction1>(b0, t0));
    bn0->set_split_up(split);
    bn0->set_dont_split_modular(true);
    bn0->set_ks_pl_idx(pl_idx);
    b0.set_bignode_idx(bn0->index());

    auto t1 = make_treg_from(input.ct1);
    t1.set_limbtype(LimbType::Par);
    auto t2 = make_treg_from(input.ct1);
    t2.set_limbtype(LimbType::Ext_Split2);
    t2.set_extension_size(extension_size);

    std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn0);
    bn1->set_ks_pl_no(2);
    bn1->push_instruction(std::make_shared<PlInstruction8>(t1, t2, b0, t0));
    bn1->set_split_up(split);
    bn1->set_dont_split_modular(true);
    bn1->set_ks_pl_head_idx(bn0);
    bn1->set_ks_pl_idx(pl_idx);
    t1.set_bignode_idx(bn1->index());
    t2.set_bignode_idx(bn1->index());

    bn0->set_next_bignode_in_group(bn1);

    for (int i = 0; i < rotation_indices.size(); i++) {

      int32_t rot_idx = rotation_indices.at(i);

      if (rot_idx == 0) {
        ct0_rotations.push_back(input.ct0);
        ct1_rotations.push_back(input.ct1);
        continue;
      }

      auto evalkeys = get_evalkey(input.ct1, EvalKeyType::Rotation, rot_idx,
                                  key_switch_type, extension_size);

      auto evk0 = evalkeys.first;
      auto evk1 = evalkeys.second;

      std::pair<Polynomial, Polynomial> t1_;
      t1_.first = make_treg_from(t1);
      t1_.first.set_limbtype(LimbType::Par);
      t1_.second = make_treg_from(t2);
      t1_.second.set_limbtype(LimbType::Ext_Split);
      t1_.second.set_extension_size(extension_size);
      bn1 = create_bignode();
      bn1->set_split_up(split);
      bn1->set_dont_split_modular(true);

      bn1->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t1_.first, t1, rot_idx));
      bn1->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t1_.second, t2, rot_idx));
      t1_.first.set_bignode_idx(bn1->index());
      t1_.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t3;
      t3.first = make_treg_from(t1);
      t3.first.set_limbtype(LimbType::Par);
      t3.second = make_treg_from(t2);
      t3.second.set_limbtype(LimbType::Ext_Split);
      t3.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t4;
      t4.first = make_treg_from(t1);
      t4.first.set_limbtype(LimbType::Par);
      t4.second = make_treg_from(t2);
      t4.second.set_limbtype(LimbType::Ext_Split);
      t4.second.set_extension_size(extension_size);

      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.first, t1_.first, evk0.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.second, t1_.second, evk0.second));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.first, t1_.first, evk1.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.second, t1_.second, evk1.second));

      t3.first.set_bignode_idx(bn1->index());
      t3.second.set_bignode_idx(bn1->index());
      t4.first.set_bignode_idx(bn1->index());
      t4.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t5;
      t5.first = make_treg_from(t1);
      t5.first.set_limbtype(LimbType::Par);
      t5.second = make_treg_from(t2);
      t5.second.set_limbtype(LimbType::Ext_Split);
      t5.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t6;
      t6.first = make_treg_from(t1);
      t6.first.set_limbtype(LimbType::Par);
      t6.second = make_treg_from(t2);
      t6.second.set_limbtype(LimbType::Ext_Split);
      t6.second.set_extension_size(extension_size);

      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

      t5.first.set_bignode_idx(bn1->index());
      t5.second.set_bignode_idx(bn1->index());
      t6.first.set_bignode_idx(bn1->index());
      t6.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t7;
      t7.first = t5.first;

      std::pair<Polynomial, Polynomial> t8;
      t8.first = t6.first;

      if (current_partition_size > 1) {

        t7.second = make_treg_from(t5.second);
        t7.second.set_limbtype(LimbType::Ext);
        t7.second.set_extension_size(extension_size);

        t8.second = make_treg_from(t6.second);
        t8.second.set_limbtype(LimbType::Ext);
        t8.second.set_extension_size(extension_size);

        std::shared_ptr<BigNode> bn_dist = create_bignode();
        bn_dist->push_instruction(
            std::make_shared<DistRecvMovInstruction>(t7.second, t5.second));
        bn_dist->push_instruction(
            std::make_shared<DistRecvMovInstruction>(t8.second, t6.second));

        t7.second.set_bignode_idx(bn_dist->index());
        t8.second.set_bignode_idx(bn_dist->index());

      } else {
        t7.second = t5.second;
        t8.second = t6.second;
      }

      auto bn = create_bignode();
      auto b1 = make_bcor_from(t7.first);
      b1.set_limbtype(LimbType::Par);
      bn->push_instruction(std::make_shared<PlInstruction1>(b1, t7.second));
      b1.set_bignode_idx(bn->index());

      bn->set_ks_pl_no(1);

      auto bn2 = create_bignode_in_group(bn);
      bn2->set_ks_pl_head_idx(bn);
      bn2->set_ks_pl_no(2);

      auto b2 = make_bcor_from(t8.first);
      b2.set_limbtype(LimbType::Par);
      bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t8.second));
      b2.set_bignode_idx(bn2->index());

      Polynomial t11 = make_treg_from(t5.first);
      Polynomial t13 = make_treg_from(t11);
      Polynomial t15 = make_treg_from(t11);
      bn2->push_instruction(
          std::make_shared<PlInstruction7>(t11, t7.first, b1));
      t11.set_bignode_idx(bn2->index());
      bn2->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t13, input.ct0, rot_idx));
      t13.set_bignode_idx(bn2->index());
      bn2->push_instruction(
          std::make_shared<BinOpInstruction>(OpCode::Add, t15, t11, t13));
      t15.set_bignode_idx(bn2->index());

      bn->set_next_bignode_in_group(bn2);

      auto bn3 = create_bignode_in_group(bn2);
      bn3->set_ks_pl_head_idx(bn);
      bn3->set_ks_pl_no(3);

      Polynomial t12 = make_treg_from(t6.first);
      bn3->push_instruction(
          std::make_shared<PlInstruction7>(t12, t8.first, b2));
      t12.set_bignode_idx(bn3->index());

      bn2->set_next_bignode_in_group(bn3);

      ct0_rotations.push_back(t15);
      ct1_rotations.push_back(t12);
    }

    return std::make_tuple(ct0_rotations, ct1_rotations);
  }

  std::tuple<std::vector<Polynomial>, std::vector<Polynomial>>
  hoisted_input_broadcast_keyswitching_internal(
      const Ciphertext &input, const std::vector<int32_t> rotation_indices) {

    using LimbType = Term::LimbType;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;
    std::shared_ptr<BigNode> bn;

    std::vector<Polynomial> ct0_rotations;
    std::vector<Polynomial> ct1_rotations;

    Polynomial ct0_prev = input.ct0;
    Polynomial ct1_prev = input.ct1;

    auto key_switch_type = KeySwitchType::Broadcast;
    auto level = input.ct1.level();
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);
    assert(dnum_glo == 1);

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;
    // for (auto ii = 0; ii < num_partitions; ii++) {
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto count = 0;
      std::set<uint16_t> s;
      for (auto l = ii; l < level; l += dnum_loc) {
        s.insert(l);
        count++;
        if (count == extension_size) {
          split.push_back(s);
          s.clear();
          count = 0;
        }
      }
      if (!s.empty()) {
        split.push_back(s);
        s.clear();
        count = 0;
      }
    }

    auto t0 = input.ct1;
    Polynomial b0;

    std::shared_ptr<BigNode> bn0 = nullptr;
    auto pl_idx = get_pl_idx();
    if (current_partition_size > 1) {
      std::shared_ptr<BigNode> bn_intt = create_bignode();
      t0 = make_treg_from(input.ct1);
      t0.set_bignode_idx(bn_intt->index());
      bn_intt->push_instruction(
          std::make_shared<UnOpInstruction>(OpCode::Int, t0, input.ct1));
      std::shared_ptr<BigNode> bn_dist = create_bignode();
      auto t0_move = make_treg_from(t0);
      t0_move.set_limbtype(LimbType::Spl);
      t0_move.set_bignode_idx(bn_dist->index());
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t0_move, t0));
      t0 = t0_move;
      bn0 = bn_dist;
      auto pl_idx = get_pl_idx();
      bn0->set_ks_pl_no(1);

      b0 = make_bcor_from(t0);
      b0.set_limbtype(LimbType::PaE);
      b0.set_extension_size(extension_size);
      bn0->push_instruction(std::make_shared<PlInstruction1>(
          b0, t0, false /*No inverse NTT required */));
      bn0->set_split_up(split);
      bn0->set_dont_split_modular(true);
      bn0->set_ks_pl_idx(pl_idx);
      b0.set_bignode_idx(bn0->index());
    } else {
      bn0 = create_bignode();
      bn0->set_ks_pl_no(1);

      b0 = make_bcor_from(t0);
      b0.set_limbtype(LimbType::PaE);
      b0.set_extension_size(extension_size);
      bn0->push_instruction(std::make_shared<PlInstruction1>(b0, t0));
      bn0->set_split_up(split);
      bn0->set_dont_split_modular(true);
      bn0->set_ks_pl_idx(pl_idx);
      b0.set_bignode_idx(bn0->index());
    }

    auto t1 = make_treg_from(input.ct1);
    t1.set_limbtype(LimbType::Par);
    auto t2 = make_treg_from(input.ct1);
    t2.set_limbtype(LimbType::Ext);
    t2.set_extension_size(extension_size);

    std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn0);
    bn1->set_ks_pl_no(2);
    bn1->push_instruction(
        std::make_shared<PlInstruction8>(t1, t2, b0, input.ct1));
    bn1->set_split_up(split);
    bn1->set_dont_split_modular(true);
    bn1->set_ks_pl_head_idx(bn0);
    bn1->set_ks_pl_idx(pl_idx);
    t1.set_bignode_idx(bn1->index());
    t2.set_bignode_idx(bn1->index());

    bn0->set_next_bignode_in_group(bn1);

    for (int i = 0; i < rotation_indices.size(); i++) {

      int32_t rot_idx = rotation_indices[i];

      if (rot_idx == 0) {
        ct0_rotations.push_back(input.ct0);
        ct1_rotations.push_back(input.ct1);
        continue;
      }

      auto evalkeys = get_evalkey(input.ct1, EvalKeyType::Rotation, rot_idx,
                                  key_switch_type, extension_size);

      auto evk0 = evalkeys.first;
      auto evk1 = evalkeys.second;

      std::pair<Polynomial, Polynomial> t1_;
      t1_.first = make_treg_from(t1);
      t1_.first.set_limbtype(LimbType::Par);
      t1_.second = make_treg_from(t2);
      t1_.second.set_limbtype(LimbType::Ext);
      t1_.second.set_extension_size(extension_size);
      bn1 = create_bignode();
      bn1->set_split_up(split);
      bn1->set_dont_split_modular(true);

      bn1->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t1_.first, t1, rot_idx));
      bn1->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t1_.second, t2, rot_idx));
      t1_.first.set_bignode_idx(bn1->index());
      t1_.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t3;
      t3.first = make_treg_from(t1);
      t3.first.set_limbtype(LimbType::Par);
      t3.second = make_treg_from(t2);
      t3.second.set_limbtype(LimbType::Ext);
      t3.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t4;
      t4.first = make_treg_from(t1);
      t4.first.set_limbtype(LimbType::Par);
      t4.second = make_treg_from(t2);
      t4.second.set_limbtype(LimbType::Ext);
      t4.second.set_extension_size(extension_size);

      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.first, t1_.first, evk0.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.second, t1_.second, evk0.second));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.first, t1_.first, evk1.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.second, t1_.second, evk1.second));

      t3.first.set_bignode_idx(bn1->index());
      t3.second.set_bignode_idx(bn1->index());
      t4.first.set_bignode_idx(bn1->index());
      t4.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t5;
      t5.first = make_treg_from(t1);
      t5.first.set_limbtype(LimbType::Par);
      t5.second = make_treg_from(t2);
      t5.second.set_limbtype(LimbType::Ext);
      t5.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t6;
      t6.first = make_treg_from(t1);
      t6.first.set_limbtype(LimbType::Par);
      t6.second = make_treg_from(t2);
      t6.second.set_limbtype(LimbType::Ext);
      t6.second.set_extension_size(extension_size);

      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

      t5.first.set_bignode_idx(bn1->index());
      t5.second.set_bignode_idx(bn1->index());
      t6.first.set_bignode_idx(bn1->index());
      t6.second.set_bignode_idx(bn1->index());

      auto bn = create_bignode();
      // auto bn = bn1;
      auto b1 = make_bcor_from(t5.first);
      b1.set_limbtype(LimbType::Par);
      bn->push_instruction(std::make_shared<PlInstruction1>(b1, t5.second));
      b1.set_bignode_idx(bn->index());

      bn->set_ks_pl_no(1);

      auto bn2 = create_bignode_in_group(bn);
      bn2->set_ks_pl_head_idx(bn);
      bn2->set_ks_pl_no(2);

      auto b2 = make_bcor_from(t6.first);
      b2.set_limbtype(LimbType::Par);
      bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t6.second));
      b2.set_bignode_idx(bn2->index());

      Polynomial t11 = make_treg_from(t5.first);
      Polynomial t13 = make_treg_from(t11);
      Polynomial t15 = make_treg_from(t11);
      bn2->push_instruction(
          std::make_shared<PlInstruction7>(t11, t5.first, b1));
      t11.set_bignode_idx(bn2->index());
      bn2->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t13, input.ct0, rot_idx));
      t13.set_bignode_idx(bn2->index());
      bn2->push_instruction(
          std::make_shared<BinOpInstruction>(OpCode::Add, t15, t11, t13));
      t15.set_bignode_idx(bn2->index());

      bn->set_next_bignode_in_group(bn2);

      auto bn3 = create_bignode_in_group(bn2);
      bn3->set_ks_pl_head_idx(bn);
      bn3->set_ks_pl_no(3);

      Polynomial t12 = make_treg_from(t6.first);
      bn3->push_instruction(
          std::make_shared<PlInstruction7>(t12, t6.first, b2));
      t12.set_bignode_idx(bn3->index());

      bn2->set_next_bignode_in_group(bn3);

      ct0_rotations.push_back(t15);
      ct1_rotations.push_back(t12);
    }

    return std::make_tuple(ct0_rotations, ct1_rotations);
  }

  PolyPair bsgs_giantstep_accumulate_keyswitch_iterations(
      const std::tuple<std::vector<Polynomial>, std::vector<Polynomial>>
          &babysteps,
      const std::vector<Plaintext> &plaintexts,
      const std::vector<int32_t> giantstep_rotation_indices,
      const SplitType &split, const uint32_t extension_size,
      const uint32_t dnum_glo) {

    using LimbType = Term::LimbType;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    std::shared_ptr<BigNode> bn;
    std::optional<Polynomial>
        zero_rot_idx_sum; /* Sum of terms that are not rotated*/

    std::vector<Polynomial> babystep_rotations_ct0 = std::get<0>(babysteps);
    std::vector<Polynomial> babystep_rotations_ct1 = std::get<1>(babysteps);

    Polynomial giantstep_ct0_sum;

    std::optional<Polynomial> giantstep_zero_rot_idx_ct1_sum;
    std::optional<PolyPair> giantstep_k0_sum;
    std::optional<PolyPair> giantstep_k1_sum;

    size_t num_babysteps = babystep_rotations_ct0.size();

    for (int i = 0; i < giantstep_rotation_indices.size(); i++) {

      Polynomial babystep_accumulated_ct0;
      Polynomial babystep_accumulated_ct1;
      auto rot_idx = giantstep_rotation_indices[i];
      auto evalkeys =
          get_evalkey(babystep_rotations_ct0[0], EvalKeyType::Rotation, rot_idx,
                      KeySwitchType::Aggregation, extension_size);
      bn = create_bignode();
      for (int j = 0; j < num_babysteps; j++) {
        auto &plaintext = plaintexts[j + i * num_babysteps];
        auto t0 = make_treg_from(babystep_rotations_ct0[j]);
        auto t1 = make_treg_from(babystep_rotations_ct1[j]);
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::MuP, t0, babystep_rotations_ct0[j], plaintext.pt));
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::MuP, t1, babystep_rotations_ct1[j], plaintext.pt));
        t0.set_bignode_idx(bn->index());
        t1.set_bignode_idx(bn->index());

        if (j == 0) {
          babystep_accumulated_ct0 = t0;
          babystep_accumulated_ct1 = t1;
        } else {
          auto t2 = make_treg_from(t0);
          auto t3 = make_treg_from(t1);
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, t2, t0, babystep_accumulated_ct0));
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, t3, t1, babystep_accumulated_ct1));
          t2.set_bignode_idx(bn->index());
          t3.set_bignode_idx(bn->index());
          babystep_accumulated_ct0 = t2;
          babystep_accumulated_ct1 = t3;
        }
      }

      // auto rot_idx = giantstep_rotation_indices[i];
      Polynomial rot0;
      Polynomial rot1;
      if (rot_idx == 0) {
        rot0 = babystep_accumulated_ct0;
        rot1 = babystep_accumulated_ct1;
      } else {
        rot0 = make_treg_from(babystep_accumulated_ct0);
        rot1 = make_treg_from(babystep_accumulated_ct1);
        bn->push_instruction(std::make_shared<UnOpInstruction>(
            OpCode::Rot, rot0, babystep_accumulated_ct0, rot_idx));
        bn->push_instruction(std::make_shared<UnOpInstruction>(
            OpCode::Rot, rot1, babystep_accumulated_ct1, rot_idx));
        rot0.set_bignode_idx(bn->index());
        rot1.set_bignode_idx(bn->index());
      }

      if (i == 0) {
        giantstep_ct0_sum = rot0;
      } else {
        auto tsum = make_treg_from(rot0);
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum, rot0, giantstep_ct0_sum));
        tsum.set_bignode_idx(bn->index());
        giantstep_ct0_sum = tsum;
      }

      if (rot_idx == 0) {
        if (!giantstep_zero_rot_idx_ct1_sum.has_value()) {
          giantstep_zero_rot_idx_ct1_sum = rot1;
        } else {
          auto tsum = make_treg_from(rot1);
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum, rot1, giantstep_zero_rot_idx_ct1_sum.value()));
          tsum.set_bignode_idx(bn->index());
          giantstep_zero_rot_idx_ct1_sum = tsum;
        }
        continue;
      }

      auto pl_idx = get_pl_idx();
      auto b0 = make_bcor_from(rot1);
      b0.set_limbtype(LimbType::ExC);
      b0.set_extension_size(extension_size);

      bn = create_bignode();
      bn->set_ks_pl_no(1);
      bn->push_instruction(std::make_shared<PlInstruction1>(b0, rot1));
      bn->set_split_up(split);
      bn->set_ks_pl_idx(pl_idx);
      b0.set_bignode_idx(bn->index());

      std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn);

      auto t1 = make_treg_from(rot1);
      t1.set_limbtype(LimbType::Usp);
      auto t2 = make_treg_from(rot1);
      t2.set_limbtype(LimbType::Ext);
      t2.set_extension_size(extension_size);

      bn1->push_instruction(std::make_shared<PlInstruction8>(t1, t2, b0, rot1));
      bn1->set_split_up(split);
      bn1->set_ks_pl_head_idx(bn);
      bn1->set_ks_pl_idx(pl_idx);
      t1.set_bignode_idx(bn1->index());
      t2.set_bignode_idx(bn1->index());

      bn->set_next_bignode_in_group(bn1);

      std::pair<Polynomial, Polynomial> t3;
      t3.first = make_treg_from(t1);
      t3.first.set_limbtype(LimbType::Usp);
      t3.second = make_treg_from(t2);
      t3.second.set_limbtype(LimbType::Ext);
      t3.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t4;
      t4.first = make_treg_from(t1);
      t4.first.set_limbtype(LimbType::Usp);
      t4.second = make_treg_from(t2);
      t4.second.set_limbtype(LimbType::Ext);
      t4.second.set_extension_size(extension_size);

      auto evk0 = evalkeys.first;
      auto evk1 = evalkeys.second;
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.first, t1, evk0.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.second, t2, evk0.second));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.first, t1, evk1.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.second, t2, evk1.second));

      t3.first.set_bignode_idx(bn1->index());
      t3.second.set_bignode_idx(bn1->index());
      t4.first.set_bignode_idx(bn1->index());
      t4.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t5;
      t5.first = make_treg_from(t1);
      t5.first.set_limbtype(LimbType::Usp);
      t5.second = make_treg_from(t2);
      t5.second.set_limbtype(LimbType::Ext);
      t5.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t6;
      t6.first = make_treg_from(t1);
      t6.first.set_limbtype(LimbType::Usp);
      t6.second = make_treg_from(t2);
      t6.second.set_limbtype(LimbType::Ext);
      t6.second.set_extension_size(extension_size);

      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

      t5.first.set_bignode_idx(bn1->index());
      t5.second.set_bignode_idx(bn1->index());
      t6.first.set_bignode_idx(bn1->index());
      t6.second.set_bignode_idx(bn1->index());

      bn1 = create_bignode();

      if (!giantstep_k0_sum.has_value()) {
        giantstep_k0_sum = t5;
      } else {
        auto &k0_sum_val = giantstep_k0_sum.value();
        std::pair<Polynomial, Polynomial> tsum;
        tsum.first = make_treg_from(t5.first);
        tsum.first.set_limbtype(LimbType::Usp);
        tsum.second = make_treg_from(t5.second);
        tsum.second.set_limbtype(LimbType::Ext);
        tsum.second.set_extension_size(extension_size);
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.first, t5.first, k0_sum_val.first));
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.second, t5.second, k0_sum_val.second));
        tsum.first.set_bignode_idx(bn1->index());
        tsum.second.set_bignode_idx(bn1->index());
        giantstep_k0_sum = tsum;
      }

      if (!giantstep_k1_sum.has_value()) {
        giantstep_k1_sum = t6;
      } else {
        auto &k1_sum_val = giantstep_k1_sum.value();
        std::pair<Polynomial, Polynomial> tsum;
        tsum.first = make_treg_from(t6.first);
        tsum.first.set_limbtype(LimbType::Usp);
        tsum.second = make_treg_from(t6.second);
        tsum.second.set_limbtype(LimbType::Ext);
        tsum.second.set_extension_size(extension_size);
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.first, t6.first, k1_sum_val.first));
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum.second, t6.second, k1_sum_val.second));
        tsum.first.set_bignode_idx(bn1->index());
        tsum.second.set_bignode_idx(bn1->index());
        giantstep_k1_sum = tsum;
      }
    }

    assert(giantstep_k0_sum.has_value());
    assert(giantstep_k1_sum.has_value());

    bn = create_bignode();
    auto b1 = make_bcor_from(giantstep_k0_sum.value().first);
    b1.set_limbtype(LimbType::Usp);
    bn->push_instruction(
        std::make_shared<PlInstruction1>(b1, giantstep_k0_sum.value().second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(giantstep_k1_sum.value().first);
    b2.set_limbtype(LimbType::Usp);
    bn2->push_instruction(
        std::make_shared<PlInstruction1>(b2, giantstep_k1_sum.value().second));
    b2.set_bignode_idx(bn2->index());

    Polynomial t11 = make_treg_from(giantstep_k0_sum.value().first);
    bn2->push_instruction(std::make_shared<PlInstruction7>(
        t11, giantstep_k0_sum.value().first, b1));
    t11.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn);
    bn3->set_ks_pl_no(3);

    Polynomial t12 = make_treg_from(giantstep_k1_sum.value().first);
    if (dnum_glo != 1) {
      t11.set_limbtype(LimbType::Usp);
      t11.set_join_reqd(true);
      t12.set_limbtype(LimbType::Usp);
      t12.set_join_reqd(true);
    }
    bn3->push_instruction(std::make_shared<PlInstruction7>(
        t12, giantstep_k1_sum.value().first, b2));
    t12.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);

    auto bn4 = create_bignode();
    Polynomial t13 = make_treg_from(t11);
    bn4->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Add, t13, giantstep_ct0_sum, t11));
    t13.set_bignode_idx(bn4->index());

    Polynomial t14 = make_treg_from(t12);
    if (giantstep_zero_rot_idx_ct1_sum.has_value()) {
      auto bn5 = create_bignode();
      bn5->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Add, t14, giantstep_zero_rot_idx_ct1_sum.value(), t12));
      t14.set_bignode_idx(bn5->index());
    } else {
      t14 = t12;
    }

    return PolyPair(t13, t14);
  }

  PolyPair rotate_accumulate_internal_input_broadcast(
      const std::vector<Ciphertext> &ciphertexts,
      const std::vector<int32_t> rotation_indices, const SplitType &split__,
      const uint32_t extension_size__, const uint32_t dnum_glo__) {

    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    using LimbType = Term::LimbType;

    auto key_switch_type = KeySwitchType::Broadcast;
    auto level = ciphertexts[0].ct1.level();
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);
    assert(dnum_glo == 1);

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;
    // for (auto ii = 0; ii < num_partitions; ii++) {
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto count = 0;
      std::set<uint16_t> s;
      for (auto l = ii; l < level; l += dnum_loc) {
        s.insert(l);
        count++;
        if (count == extension_size) {
          split.push_back(s);
          s.clear();
          count = 0;
        }
      }
      if (!s.empty()) {
        split.push_back(s);
        s.clear();
        count = 0;
      }
    }

    Polynomial ct0_rot_sum;
    std::optional<Polynomial> ct1_zero_rot_sum;
    bool k_sum_set = false;
    std::pair<Polynomial, Polynomial> k0_sum;
    std::pair<Polynomial, Polynomial> k1_sum;

    for (size_t i = 0; i < ciphertexts.size(); i++) {
      auto rot_idx = rotation_indices.at(i);

      if (rot_idx == 0) {
        if (!ct1_zero_rot_sum.has_value()) {
          ct1_zero_rot_sum = ciphertexts[i].ct1;
        } else {
          auto tsum = make_treg_from(ciphertexts[i].ct1);
          auto bn = create_bignode();
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum, ciphertexts[i].ct1, ct1_zero_rot_sum.value()));
          tsum.set_bignode_idx(bn->index());
          ct1_zero_rot_sum = tsum;
        }
        if (i == 0) {
          ct0_rot_sum = ciphertexts[i].ct0;
        } else {
          auto tsum = make_treg_from(ciphertexts[i].ct0);
          auto bn = create_bignode();
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum, ciphertexts[i].ct0, ct0_rot_sum));
          tsum.set_bignode_idx(bn->index());
          ct0_rot_sum = tsum;
        }
        continue;
      }

      assert(rot_idx != 0);

      auto evalkeys = get_evalkey(ciphertexts[0].ct1, EvalKeyType::Rotation,
                                  rot_idx, key_switch_type, extension_size);
      auto evk0 = evalkeys.first;
      auto evk1 = evalkeys.second;

      auto bn = create_bignode();
      auto t0_rot = make_treg_from(ciphertexts[i].ct1);
      bn->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t0_rot, ciphertexts[i].ct1, rot_idx));
      t0_rot.set_bignode_idx(bn->index());

      bn = create_bignode();
      auto t0_rot_ = make_treg_from(ciphertexts[i].ct0);
      bn->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t0_rot_, ciphertexts[i].ct0, rot_idx));
      t0_rot_.set_bignode_idx(bn->index());
      if (i == 0) {
        ct0_rot_sum = t0_rot_;
      } else {
        auto tsum = make_treg_from(t0_rot_);
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum, t0_rot_, ct0_rot_sum));
        tsum.set_bignode_idx(bn->index());
        ct0_rot_sum = tsum;
      }

      std::shared_ptr<BigNode> bn0 = nullptr;
      // if(num_partitions > 1){
      auto t0 = t0_rot;
      Polynomial b0;
      auto pl_idx = get_pl_idx();
      if (current_partition_size > 1) {
        std::shared_ptr<BigNode> bn_intt = create_bignode();
        t0 = make_treg_from(t0_rot);
        // t0.set_limbtype(LimbType::Spl);
        t0.set_bignode_idx(bn_intt->index());
        bn_intt->push_instruction(
            std::make_shared<UnOpInstruction>(OpCode::Int, t0, t0_rot));
        std::shared_ptr<BigNode> bn_dist = create_bignode();
        auto t0_move = make_treg_from(t0);
        t0_move.set_limbtype(LimbType::Spl);
        t0_move.set_bignode_idx(bn_dist->index());
        bn_dist->push_instruction(
            std::make_shared<DistRecvMovInstruction>(t0_move, t0));
        t0 = t0_move;
        bn0 = bn_dist;
        auto pl_idx = get_pl_idx();
        bn0->set_ks_pl_no(1);

        b0 = make_bcor_from(t0);
        b0.set_limbtype(LimbType::PaE);
        b0.set_extension_size(extension_size);
        bn0->push_instruction(std::make_shared<PlInstruction1>(
            b0, t0, false /*No inverse NTT required */));
        bn0->set_split_up(split);
        bn0->set_dont_split_modular(true);
        bn0->set_ks_pl_idx(pl_idx);
        b0.set_bignode_idx(bn0->index());
      } else {
        bn0 = create_bignode();
        bn0->set_ks_pl_no(1);

        b0 = make_bcor_from(t0);
        b0.set_limbtype(LimbType::PaE);
        b0.set_extension_size(extension_size);
        bn0->push_instruction(std::make_shared<PlInstruction1>(b0, t0));
        bn0->set_split_up(split);
        bn0->set_dont_split_modular(true);
        bn0->set_ks_pl_idx(pl_idx);
        b0.set_bignode_idx(bn0->index());
      }

      auto t1 = make_treg_from(t0);
      t1.set_limbtype(LimbType::Par);
      auto t2 = make_treg_from(t0);
      t2.set_limbtype(LimbType::Ext);
      t2.set_extension_size(extension_size);

      std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn0);
      bn1->set_ks_pl_no(2);
      bn1->push_instruction(
          std::make_shared<PlInstruction8>(t1, t2, b0, t0_rot));
      bn1->set_split_up(split);
      bn1->set_dont_split_modular(true);
      bn1->set_ks_pl_head_idx(bn0);
      bn1->set_ks_pl_idx(pl_idx);
      t1.set_bignode_idx(bn1->index());
      t2.set_bignode_idx(bn1->index());

      bn0->set_next_bignode_in_group(bn1);

      std::pair<Polynomial, Polynomial> t3;
      t3.first = make_treg_from(t1);
      t3.first.set_limbtype(LimbType::Par);
      t3.second = make_treg_from(t2);
      t3.second.set_limbtype(LimbType::Ext);
      t3.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t4;
      t4.first = make_treg_from(t1);
      t4.first.set_limbtype(LimbType::Par);
      t4.second = make_treg_from(t2);
      t4.second.set_limbtype(LimbType::Ext);
      t4.second.set_extension_size(extension_size);

      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.first, t1, evk0.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.second, t2, evk0.second));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.first, t1, evk1.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.second, t2, evk1.second));

      t3.first.set_bignode_idx(bn1->index());
      t3.second.set_bignode_idx(bn1->index());
      t4.first.set_bignode_idx(bn1->index());
      t4.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t5;
      t5.first = make_treg_from(t1);
      t5.first.set_limbtype(LimbType::Par);
      t5.second = make_treg_from(t2);
      t5.second.set_limbtype(LimbType::Ext);
      t5.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t6;
      t6.first = make_treg_from(t1);
      t6.first.set_limbtype(LimbType::Par);
      t6.second = make_treg_from(t2);
      t6.second.set_limbtype(LimbType::Ext);
      t6.second.set_extension_size(extension_size);

      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

      t5.first.set_bignode_idx(bn1->index());
      t5.second.set_bignode_idx(bn1->index());
      t6.first.set_bignode_idx(bn1->index());
      t6.second.set_bignode_idx(bn1->index());

      if (!k_sum_set) {
        k0_sum = t5;
        k1_sum = t6;
        k_sum_set = true;
      } else {
        // accumulate the sums

        std::pair<Polynomial, Polynomial> t7;
        t7.first = make_treg_from(t1);
        t7.first.set_limbtype(LimbType::Par);
        t7.second = make_treg_from(t2);
        t7.second.set_limbtype(LimbType::Ext);
        t7.second.set_extension_size(extension_size);

        std::pair<Polynomial, Polynomial> t8;
        t8.first = make_treg_from(t1);
        t8.first.set_limbtype(LimbType::Par);
        t8.second = make_treg_from(t2);
        t8.second.set_limbtype(LimbType::Ext);
        t8.second.set_extension_size(extension_size);
        auto bn2 = create_bignode();

        bn2->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t7.first, t5.first, k0_sum.first));
        bn2->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t7.second, t5.second, k0_sum.second));
        bn2->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t8.first, t6.first, k1_sum.first));
        bn2->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t8.second, t6.second, k1_sum.second));

        t7.first.set_bignode_idx(bn2->index());
        t7.second.set_bignode_idx(bn2->index());
        t8.first.set_bignode_idx(bn2->index());
        t8.second.set_bignode_idx(bn2->index());

        k0_sum = t7;
        k1_sum = t8;
      }
    }

    std::pair<Polynomial, Polynomial> t7;
    t7.first = k0_sum.first;

    std::pair<Polynomial, Polynomial> t8;
    t8.first = k1_sum.first;

    t7.second = k0_sum.second;
    t8.second = k1_sum.second;

    auto bn = create_bignode();
    auto b1 = make_bcor_from(t7.first);
    b1.set_limbtype(LimbType::Par);
    bn->push_instruction(std::make_shared<PlInstruction1>(b1, t7.second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_head_idx(bn);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(t8.first);
    b2.set_limbtype(LimbType::Par);
    bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t8.second));
    b2.set_bignode_idx(bn2->index());

    Polynomial t11 = make_treg_from(t7.first);
    Polynomial t13 = make_treg_from(t11);
    bn2->push_instruction(std::make_shared<PlInstruction7>(t11, t7.first, b1));
    t11.set_bignode_idx(bn2->index());
    bn2->push_instruction(
        std::make_shared<BinOpInstruction>(OpCode::Add, t13, t11, ct0_rot_sum));
    t13.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn);
    bn3->set_ks_pl_no(3);

    Polynomial t12 = make_treg_from(t8.first);
    bn3->push_instruction(std::make_shared<PlInstruction7>(t12, t8.first, b2));
    t12.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);

    Polynomial t14;
    if (ct1_zero_rot_sum.has_value()) {
      t14 = make_treg_from(t12);
      bn3->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Add, t14, ct1_zero_rot_sum.value(), t12));
      t14.set_bignode_idx(bn3->index());
    } else {
      t14 = t12;
    }

    PolyPair output;
    output.first = t13;
    output.second = t14;
    return output;
  }

  PolyPair rotate_accumulate_internal_basic_parallel(
      const std::vector<Ciphertext> &ciphertexts,
      const std::vector<int32_t> rotation_indices, const SplitType &split__,
      const uint32_t extension_size__, const uint32_t dnum_glo__) {

    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    using LimbType = Term::LimbType;

    auto key_switch_type = KeySwitchType::BasicParallel;
    auto level = ciphertexts[0].ct1.level();
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);
    assert(dnum_glo == 1);

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;
    // for (auto ii = 0; ii < num_partitions; ii++) {
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto count = 0;
      std::set<uint16_t> s;
      for (auto l = ii; l < level; l += dnum_loc) {
        s.insert(l);
        count++;
        if (count == extension_size) {
          split.push_back(s);
          s.clear();
          count = 0;
        }
      }
      if (!s.empty()) {
        split.push_back(s);
        s.clear();
        count = 0;
      }
    }

    Polynomial ct0_rot_sum;
    std::optional<Polynomial> ct1_zero_rot_sum;
    bool k_sum_set = false;
    std::pair<Polynomial, Polynomial> k0_sum;
    std::pair<Polynomial, Polynomial> k1_sum;

    for (size_t i = 0; i < ciphertexts.size(); i++) {
      auto rot_idx = rotation_indices.at(i);
      // auto t0 = ciphertexts[i].ct1;
      // auto inp = ciphertexts[i].ct1;

      if (rot_idx == 0) {
        if (!ct1_zero_rot_sum.has_value()) {
          ct1_zero_rot_sum = ciphertexts[i].ct1;
        } else {
          auto tsum = make_treg_from(ciphertexts[i].ct1);
          auto bn = create_bignode();
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum, ciphertexts[i].ct1, ct1_zero_rot_sum.value()));
          tsum.set_bignode_idx(bn->index());
          ct1_zero_rot_sum = tsum;
        }
        if (i == 0) {
          ct0_rot_sum = ciphertexts[i].ct0;
        } else {
          auto tsum = make_treg_from(ciphertexts[i].ct0);
          auto bn = create_bignode();
          bn->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum, ciphertexts[i].ct0, ct0_rot_sum));
          tsum.set_bignode_idx(bn->index());
          ct0_rot_sum = tsum;
        }
        continue;
      }

      assert(rot_idx != 0);

      auto evalkeys = get_evalkey(ciphertexts[0].ct1, EvalKeyType::Rotation,
                                  rot_idx, key_switch_type, extension_size);
      auto evk0 = evalkeys.first;
      auto evk1 = evalkeys.second;

      auto bn = create_bignode();
      auto t0_rot = make_treg_from(ciphertexts[i].ct1);
      bn->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t0_rot, ciphertexts[i].ct1, rot_idx));
      t0_rot.set_bignode_idx(bn->index());

      bn = create_bignode();
      auto t0_rot_ = make_treg_from(ciphertexts[i].ct0);
      bn->push_instruction(std::make_shared<UnOpInstruction>(
          OpCode::Rot, t0_rot_, ciphertexts[i].ct0, rot_idx));
      t0_rot_.set_bignode_idx(bn->index());
      if (i == 0) {
        ct0_rot_sum = t0_rot_;
      } else {
        auto tsum = make_treg_from(t0_rot_);
        bn->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, tsum, t0_rot_, ct0_rot_sum));
        tsum.set_bignode_idx(bn->index());
        ct0_rot_sum = tsum;
      }

      std::shared_ptr<BigNode> bn0 = nullptr;
      // if(num_partitions > 1){
      auto t0 = t0_rot;
      if (current_partition_size > 1) {
        std::shared_ptr<BigNode> bn_dist = create_bignode();
        auto t0_move = make_treg_from(t0_rot);
        t0_move.set_limbtype(LimbType::Spl);
        t0_move.set_bignode_idx(bn_dist->index());
        bn_dist->push_instruction(
            std::make_shared<DistRecvMovInstruction>(t0_move, t0_rot));
        t0 = t0_move;
        bn0 = bn_dist;
      } else {
        bn0 = create_bignode();
      }

      // XXX: The different parts of the evalkeys should be different bignodes
      // while reading
      auto pl_idx = get_pl_idx();
      // std::shared_ptr<BigNode> bn0 = create_bignode();
      bn0->set_ks_pl_no(1);

      auto b0 = make_bcor_from(t0);
      b0.set_limbtype(LimbType::PaE_Split);
      b0.set_extension_size(extension_size);
      bn0->push_instruction(std::make_shared<PlInstruction1>(b0, t0));
      bn0->set_split_up(split);
      bn0->set_dont_split_modular(true);
      bn0->set_ks_pl_idx(pl_idx);
      b0.set_bignode_idx(bn0->index());

      auto t1 = make_treg_from(t0);
      t1.set_limbtype(LimbType::Par);
      auto t2 = make_treg_from(t0);
      t2.set_limbtype(LimbType::Ext_Split);
      t2.set_extension_size(extension_size);

      std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn0);
      bn1->set_ks_pl_no(2);
      bn1->push_instruction(std::make_shared<PlInstruction8>(t1, t2, b0, t0));
      bn1->set_split_up(split);
      bn1->set_dont_split_modular(true);
      bn1->set_ks_pl_head_idx(bn0);
      bn1->set_ks_pl_idx(pl_idx);
      t1.set_bignode_idx(bn1->index());
      t2.set_bignode_idx(bn1->index());

      bn0->set_next_bignode_in_group(bn1);

      std::pair<Polynomial, Polynomial> t3;
      t3.first = make_treg_from(t1);
      t3.first.set_limbtype(LimbType::Par);
      t3.second = make_treg_from(t2);
      t3.second.set_limbtype(LimbType::Ext_Split);
      t3.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t4;
      t4.first = make_treg_from(t1);
      t4.first.set_limbtype(LimbType::Par);
      t4.second = make_treg_from(t2);
      t4.second.set_limbtype(LimbType::Ext_Split);
      t4.second.set_extension_size(extension_size);

      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.first, t1, evk0.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t3.second, t2, evk0.second));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.first, t1, evk1.first));
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Mul, t4.second, t2, evk1.second));

      t3.first.set_bignode_idx(bn1->index());
      t3.second.set_bignode_idx(bn1->index());
      t4.first.set_bignode_idx(bn1->index());
      t4.second.set_bignode_idx(bn1->index());

      std::pair<Polynomial, Polynomial> t5;
      t5.first = make_treg_from(t1);
      t5.first.set_limbtype(LimbType::Par);
      t5.second = make_treg_from(t2);
      t5.second.set_limbtype(LimbType::Ext_Split);
      t5.second.set_extension_size(extension_size);

      std::pair<Polynomial, Polynomial> t6;
      t6.first = make_treg_from(t1);
      t6.first.set_limbtype(LimbType::Par);
      t6.second = make_treg_from(t2);
      t6.second.set_limbtype(LimbType::Ext_Split);
      t6.second.set_extension_size(extension_size);

      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
      bn1->push_instruction(
          std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

      t5.first.set_bignode_idx(bn1->index());
      t5.second.set_bignode_idx(bn1->index());
      t6.first.set_bignode_idx(bn1->index());
      t6.second.set_bignode_idx(bn1->index());

      if (!k_sum_set) {
        k0_sum = t5;
        k1_sum = t6;
        k_sum_set = true;
      } else {
        // accumulate the sums

        std::pair<Polynomial, Polynomial> t7;
        t7.first = make_treg_from(t1);
        t7.first.set_limbtype(LimbType::Par);
        t7.second = make_treg_from(t2);
        t7.second.set_limbtype(LimbType::Ext_Split);
        t7.second.set_extension_size(extension_size);

        std::pair<Polynomial, Polynomial> t8;
        t8.first = make_treg_from(t1);
        t8.first.set_limbtype(LimbType::Par);
        t8.second = make_treg_from(t2);
        t8.second.set_limbtype(LimbType::Ext_Split);
        t8.second.set_extension_size(extension_size);
        auto bn2 = create_bignode();

        bn2->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t7.first, t5.first, k0_sum.first));
        bn2->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t7.second, t5.second, k0_sum.second));
        bn2->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t8.first, t6.first, k1_sum.first));
        bn2->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t8.second, t6.second, k1_sum.second));

        t7.first.set_bignode_idx(bn2->index());
        t7.second.set_bignode_idx(bn2->index());
        t8.first.set_bignode_idx(bn2->index());
        t8.second.set_bignode_idx(bn2->index());

        k0_sum = t7;
        k1_sum = t8;
      }
    }

    std::pair<Polynomial, Polynomial> t7;
    t7.first = k0_sum.first;

    std::pair<Polynomial, Polynomial> t8;
    t8.first = k1_sum.first;

    if (current_partition_size > 1) {

      t7.second = make_treg_from(k0_sum.second);
      t7.second.set_limbtype(LimbType::Ext);
      t7.second.set_extension_size(extension_size);

      t8.second = make_treg_from(k1_sum.second);
      t8.second.set_limbtype(LimbType::Ext);
      t8.second.set_extension_size(extension_size);

      std::shared_ptr<BigNode> bn_dist = create_bignode();
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t7.second, k0_sum.second));
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t8.second, k1_sum.second));

      t7.second.set_bignode_idx(bn_dist->index());
      t8.second.set_bignode_idx(bn_dist->index());

    } else {
      t7.second = k0_sum.second;
      t8.second = k1_sum.second;
    }

    auto bn = create_bignode();
    auto b1 = make_bcor_from(t7.first);
    b1.set_limbtype(LimbType::Par);
    bn->push_instruction(std::make_shared<PlInstruction1>(b1, t7.second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_head_idx(bn);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(t8.first);
    b2.set_limbtype(LimbType::Par);
    bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t8.second));
    b2.set_bignode_idx(bn2->index());

    Polynomial t11 = make_treg_from(t7.first);
    Polynomial t13 = make_treg_from(t11);
    bn2->push_instruction(std::make_shared<PlInstruction7>(t11, t7.first, b1));
    t11.set_bignode_idx(bn2->index());
    bn2->push_instruction(
        std::make_shared<BinOpInstruction>(OpCode::Add, t13, t11, ct0_rot_sum));
    t13.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn);
    bn3->set_ks_pl_no(3);

    Polynomial t12 = make_treg_from(t8.first);
    bn3->push_instruction(std::make_shared<PlInstruction7>(t12, t8.first, b2));
    t12.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);

    Polynomial t14;
    if (ct1_zero_rot_sum.has_value()) {
      t14 = make_treg_from(t12);
      bn3->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::Add, t14, ct1_zero_rot_sum.value(), t12));
      t14.set_bignode_idx(bn3->index());
    } else {
      t14 = t12;
    }

    PolyPair output;
    output.first = t13;
    output.second = t14;
    return output;
  }

  PolyPair rotate_accumulate_internal(
      const std::vector<Ciphertext> &ciphertexts,
      const std::vector<int32_t> rotation_indices, const SplitType &split,
      const uint32_t extension_size, const uint32_t dnum_glo) {

    using LimbType = Term::LimbType;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    std::shared_ptr<BigNode> bn;
    std::optional<Polynomial>
        zero_rot_idx_sum; /* Sum of terms that are not rotated*/

    Polynomial ct0_sum;
    PolyPair output;
    std::vector<Polynomial> rot1_vec;

    for (int k = 0; k < 2; k++) {

      std::optional<Polynomial> zero_rot_idx_ct1_sum;
      std::optional<PolyPair> k_sum;

      for (int i = 0; i < rotation_indices.size(); i++) {

        auto rot_idx = rotation_indices[i];
        auto evalkeys =
            get_evalkey(ciphertexts[0].ct1, EvalKeyType::Rotation, rot_idx,
                        KeySwitchType::Aggregation, extension_size);
        bn = create_bignode();
        Polynomial rot0;
        Polynomial rot1;
        if (k == 0) {
          if (rot_idx == 0) {
            rot0 = ciphertexts[i].ct0;
            rot1 = ciphertexts[i].ct1;
          } else {
            rot0 = make_treg_from(ciphertexts[i].ct0);
            rot1 = make_treg_from(ciphertexts[i].ct1);
            bn->push_instruction(std::make_shared<UnOpInstruction>(
                OpCode::Rot, rot0, ciphertexts[i].ct0, rot_idx));
            bn->push_instruction(std::make_shared<UnOpInstruction>(
                OpCode::Rot, rot1, ciphertexts[i].ct1, rot_idx));
            rot0.set_bignode_idx(bn->index());
            rot1.set_bignode_idx(bn->index());
          }
          rot1_vec.push_back(rot1);
          if (i == 0) {
            ct0_sum = rot0;
          } else {
            auto tsum = make_treg_from(rot0);
            bn->push_instruction(std::make_shared<BinOpInstruction>(
                OpCode::Add, tsum, rot0, ct0_sum));
            tsum.set_bignode_idx(bn->index());
            ct0_sum = tsum;
          }
        }
        rot1 = rot1_vec[i];

        if (rot_idx == 0) {
          if (k == 1) {
            if (!zero_rot_idx_ct1_sum.has_value()) {
              zero_rot_idx_ct1_sum = rot1;
            } else {
              auto tsum = make_treg_from(rot1);
              bn->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Add, tsum, rot1, zero_rot_idx_ct1_sum.value()));
              tsum.set_bignode_idx(bn->index());
              zero_rot_idx_ct1_sum = tsum;
            }
          }
          continue;
        }

        auto pl_idx = get_pl_idx();
        auto b0 = make_bcor_from(rot1);
        b0.set_limbtype(LimbType::ExC);
        b0.set_extension_size(extension_size);

        bn = create_bignode();
        bn->set_ks_pl_no(1);
        bn->push_instruction(std::make_shared<PlInstruction1>(b0, rot1));
        bn->set_split_up(split);
        bn->set_ks_pl_idx(pl_idx);
        b0.set_bignode_idx(bn->index());

        std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn);

        auto t1 = make_treg_from(rot1);
        t1.set_limbtype(LimbType::Usp);
        auto t2 = make_treg_from(rot1);
        t2.set_limbtype(LimbType::Ext);
        t2.set_extension_size(extension_size);

        bn1->push_instruction(
            std::make_shared<PlInstruction8>(t1, t2, b0, rot1));
        bn1->set_split_up(split);
        bn1->set_ks_pl_head_idx(bn);
        bn1->set_ks_pl_idx(pl_idx);
        t1.set_bignode_idx(bn1->index());
        t2.set_bignode_idx(bn1->index());

        bn->set_next_bignode_in_group(bn1);

        std::pair<Polynomial, Polynomial> t3;
        t3.first = make_treg_from(t1);
        t3.first.set_limbtype(LimbType::Usp);
        t3.second = make_treg_from(t2);
        t3.second.set_limbtype(LimbType::Ext);
        t3.second.set_extension_size(extension_size);

        PolyPair evk;
        if (k == 0) {
          evk = evalkeys.first;
        } else {
          evk = evalkeys.second;
        }
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Mul, t3.first, t1, evk.first));
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Mul, t3.second, t2, evk.second));

        t3.first.set_bignode_idx(bn1->index());
        t3.second.set_bignode_idx(bn1->index());

        std::pair<Polynomial, Polynomial> t5;
        t5.first = make_treg_from(t1);
        t5.first.set_limbtype(LimbType::Usp);
        t5.second = make_treg_from(t2);
        t5.second.set_limbtype(LimbType::Ext);
        t5.second.set_extension_size(extension_size);

        bn1->push_instruction(
            std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
        bn1->push_instruction(
            std::make_shared<JoinLocalInstruction>(t5.second, t3.second));

        t5.first.set_bignode_idx(bn1->index());
        t5.second.set_bignode_idx(bn1->index());

        bn1 = create_bignode();

        if (!k_sum.has_value()) {
          k_sum = t5;
        } else {
          auto &k_sum_val = k_sum.value();
          std::pair<Polynomial, Polynomial> tsum;
          tsum.first = make_treg_from(t5.first);
          tsum.first.set_limbtype(LimbType::Usp);
          tsum.second = make_treg_from(t5.second);
          tsum.second.set_limbtype(LimbType::Ext);
          tsum.second.set_extension_size(extension_size);
          bn1->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum.first, t5.first, k_sum_val.first));
          bn1->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum.second, t5.second, k_sum_val.second));
          tsum.first.set_bignode_idx(bn1->index());
          tsum.second.set_bignode_idx(bn1->index());
          k_sum = tsum;
        }
      }

      assert(k_sum.has_value());

      bn = create_bignode();
      auto b1 = make_bcor_from(k_sum.value().first);
      b1.set_limbtype(LimbType::Usp);
      bn->push_instruction(
          std::make_shared<PlInstruction1>(b1, k_sum.value().second));
      b1.set_bignode_idx(bn->index());

      bn->set_ks_pl_no(1);

      auto bn2 = create_bignode_in_group(bn);
      bn2->set_ks_pl_no(2);

      Polynomial t11 = make_treg_from(k_sum.value().first);
      bn2->push_instruction(
          std::make_shared<PlInstruction7>(t11, k_sum.value().first, b1));
      t11.set_bignode_idx(bn2->index());

      bn->set_next_bignode_in_group(bn2);

      if (dnum_glo != 1) {
        t11.set_limbtype(LimbType::Usp);
        t11.set_join_reqd(true);
      }

      if (k == 0) {
        auto bn4 = create_bignode();
        Polynomial t13 = make_treg_from(t11);
        bn4->push_instruction(
            std::make_shared<BinOpInstruction>(OpCode::Add, t13, ct0_sum, t11));
        t13.set_bignode_idx(bn4->index());
        output.first = t13;
      } else {
        Polynomial t14 = make_treg_from(t11);
        if (zero_rot_idx_ct1_sum.has_value()) {
          auto bn5 = create_bignode();
          bn5->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, t14, zero_rot_idx_ct1_sum.value(), t11));
          t14.set_bignode_idx(bn5->index());
        } else {
          t14 = t11;
        }
        output.second = t14;
      }
    }

    return output;
  }

  PolyPair bsgs_giantstep_accumulate_keyswitch_iterations_2(
      const std::tuple<std::vector<Polynomial>, std::vector<Polynomial>>
          &babysteps,
      const std::vector<Plaintext> &plaintexts,
      const std::vector<int32_t> giantstep_rotation_indices,
      const SplitType &split, const uint32_t extension_size,
      const uint32_t dnum_glo) {

    using LimbType = Term::LimbType;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    std::shared_ptr<BigNode> bn;
    std::optional<Polynomial>
        zero_rot_idx_sum; /* Sum of terms that are not rotated*/

    std::vector<Polynomial> babystep_rotations_ct0 = std::get<0>(babysteps);
    std::vector<Polynomial> babystep_rotations_ct1 = std::get<1>(babysteps);

    Polynomial giantstep_ct0_sum;

    size_t num_babysteps = babystep_rotations_ct0.size();
    PolyPair output;
    std::vector<Polynomial> rot1_vec;

    for (int k = 0; k < 2; k++) {

      std::optional<Polynomial> giantstep_zero_rot_idx_ct1_sum;
      std::optional<PolyPair> giantstep_k_sum;

      for (int i = 0; i < giantstep_rotation_indices.size(); i++) {

        Polynomial babystep_accumulated_ct0;
        Polynomial babystep_accumulated_ct1;
        auto rot_idx = giantstep_rotation_indices[i];
        auto evalkeys =
            get_evalkey(babystep_rotations_ct1[0], EvalKeyType::Rotation,
                        rot_idx, KeySwitchType::Aggregation, extension_size);
        bn = create_bignode();
        Polynomial rot0;
        Polynomial rot1;
        if (k == 0) {
          for (int j = 0; j < num_babysteps; j++) {
            auto &plaintext = plaintexts[j + i * num_babysteps];
            auto t0 = make_treg_from(babystep_rotations_ct0[j]);
            auto t1 = make_treg_from(babystep_rotations_ct1[j]);
            bn->push_instruction(std::make_shared<BinOpInstruction>(
                OpCode::MuP, t0, babystep_rotations_ct0[j], plaintext.pt));
            bn->push_instruction(std::make_shared<BinOpInstruction>(
                OpCode::MuP, t1, babystep_rotations_ct1[j], plaintext.pt));
            t0.set_bignode_idx(bn->index());
            t1.set_bignode_idx(bn->index());
            if (j == 0) {
              babystep_accumulated_ct0 = t0;
              babystep_accumulated_ct1 = t1;
            } else {
              auto t2 = make_treg_from(t0);
              auto t3 = make_treg_from(t1);
              bn->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Add, t2, t0, babystep_accumulated_ct0));
              bn->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Add, t3, t1, babystep_accumulated_ct1));
              t2.set_bignode_idx(bn->index());
              t3.set_bignode_idx(bn->index());
              babystep_accumulated_ct0 = t2;
              babystep_accumulated_ct1 = t3;
            }
          }

          if (rot_idx == 0) {
            rot0 = babystep_accumulated_ct0;
            rot1 = babystep_accumulated_ct1;
          } else {
            rot0 = make_treg_from(babystep_accumulated_ct0);
            rot1 = make_treg_from(babystep_accumulated_ct1);
            bn->push_instruction(std::make_shared<UnOpInstruction>(
                OpCode::Rot, rot0, babystep_accumulated_ct0, rot_idx));
            bn->push_instruction(std::make_shared<UnOpInstruction>(
                OpCode::Rot, rot1, babystep_accumulated_ct1, rot_idx));
            rot0.set_bignode_idx(bn->index());
            rot1.set_bignode_idx(bn->index());
          }
          rot1_vec.push_back(rot1);
        }
        rot1 = rot1_vec[i];

        if (k == 0) {
          if (i == 0) {
            giantstep_ct0_sum = rot0;
          } else {
            auto tsum = make_treg_from(rot0);
            bn->push_instruction(std::make_shared<BinOpInstruction>(
                OpCode::Add, tsum, rot0, giantstep_ct0_sum));
            tsum.set_bignode_idx(bn->index());
            giantstep_ct0_sum = tsum;
          }
        }

        if (rot_idx == 0) {
          if (k == 1) {
            if (!giantstep_zero_rot_idx_ct1_sum.has_value()) {
              giantstep_zero_rot_idx_ct1_sum = rot1;
            } else {
              auto tsum = make_treg_from(rot1);
              bn->push_instruction(std::make_shared<BinOpInstruction>(
                  OpCode::Add, tsum, rot1,
                  giantstep_zero_rot_idx_ct1_sum.value()));
              tsum.set_bignode_idx(bn->index());
              giantstep_zero_rot_idx_ct1_sum = tsum;
            }
          }
          continue;
        }

        auto pl_idx = get_pl_idx();
        auto b0 = make_bcor_from(rot1);
        b0.set_limbtype(LimbType::ExC);
        b0.set_extension_size(extension_size);

        bn = create_bignode();
        bn->set_ks_pl_no(1);
        bn->push_instruction(std::make_shared<PlInstruction1>(b0, rot1));
        bn->set_split_up(split);
        bn->set_ks_pl_idx(pl_idx);
        b0.set_bignode_idx(bn->index());

        std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn);

        auto t1 = make_treg_from(rot1);
        t1.set_limbtype(LimbType::Usp);
        auto t2 = make_treg_from(rot1);
        t2.set_limbtype(LimbType::Ext);
        t2.set_extension_size(extension_size);

        bn1->push_instruction(
            std::make_shared<PlInstruction8>(t1, t2, b0, rot1));
        bn1->set_split_up(split);
        bn1->set_ks_pl_head_idx(bn);
        bn1->set_ks_pl_idx(pl_idx);
        t1.set_bignode_idx(bn1->index());
        t2.set_bignode_idx(bn1->index());

        bn->set_next_bignode_in_group(bn1);

        std::pair<Polynomial, Polynomial> t3;
        t3.first = make_treg_from(t1);
        t3.first.set_limbtype(LimbType::Usp);
        t3.second = make_treg_from(t2);
        t3.second.set_limbtype(LimbType::Ext);
        t3.second.set_extension_size(extension_size);

        PolyPair evk;
        if (k == 0) {
          evk = evalkeys.first;
        } else {
          evk = evalkeys.second;
        }
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Mul, t3.first, t1, evk.first));
        bn1->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Mul, t3.second, t2, evk.second));

        t3.first.set_bignode_idx(bn1->index());
        t3.second.set_bignode_idx(bn1->index());

        std::pair<Polynomial, Polynomial> t5;
        t5.first = make_treg_from(t1);
        t5.first.set_limbtype(LimbType::Usp);
        t5.second = make_treg_from(t2);
        t5.second.set_limbtype(LimbType::Ext);
        t5.second.set_extension_size(extension_size);

        bn1->push_instruction(
            std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
        bn1->push_instruction(
            std::make_shared<JoinLocalInstruction>(t5.second, t3.second));

        t5.first.set_bignode_idx(bn1->index());
        t5.second.set_bignode_idx(bn1->index());

        bn1 = create_bignode();

        if (!giantstep_k_sum.has_value()) {
          giantstep_k_sum = t5;
        } else {
          auto &k_sum_val = giantstep_k_sum.value();
          std::pair<Polynomial, Polynomial> tsum;
          tsum.first = make_treg_from(t5.first);
          tsum.first.set_limbtype(LimbType::Usp);
          tsum.second = make_treg_from(t5.second);
          tsum.second.set_limbtype(LimbType::Ext);
          tsum.second.set_extension_size(extension_size);
          bn1->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum.first, t5.first, k_sum_val.first));
          bn1->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, tsum.second, t5.second, k_sum_val.second));
          tsum.first.set_bignode_idx(bn1->index());
          tsum.second.set_bignode_idx(bn1->index());
          giantstep_k_sum = tsum;
        }
      }

      assert(giantstep_k_sum.has_value());

      bn = create_bignode();
      auto b1 = make_bcor_from(giantstep_k_sum.value().first);
      b1.set_limbtype(LimbType::Usp);
      bn->push_instruction(
          std::make_shared<PlInstruction1>(b1, giantstep_k_sum.value().second));
      b1.set_bignode_idx(bn->index());

      bn->set_ks_pl_no(1);

      auto bn2 = create_bignode_in_group(bn);
      bn2->set_ks_pl_no(2);

      Polynomial t11 = make_treg_from(giantstep_k_sum.value().first);
      bn2->push_instruction(std::make_shared<PlInstruction7>(
          t11, giantstep_k_sum.value().first, b1));
      t11.set_bignode_idx(bn2->index());

      bn->set_next_bignode_in_group(bn2);

      if (dnum_glo != 1) {
        t11.set_limbtype(LimbType::Usp);
        t11.set_join_reqd(true);
      }

      if (k == 0) {
        auto bn4 = create_bignode();
        Polynomial t13 = make_treg_from(t11);
        bn4->push_instruction(std::make_shared<BinOpInstruction>(
            OpCode::Add, t13, giantstep_ct0_sum, t11));
        t13.set_bignode_idx(bn4->index());
        output.first = t13;
      } else {
        Polynomial t14 = make_treg_from(t11);
        if (giantstep_zero_rot_idx_ct1_sum.has_value()) {
          auto bn5 = create_bignode();
          bn5->push_instruction(std::make_shared<BinOpInstruction>(
              OpCode::Add, t14, giantstep_zero_rot_idx_ct1_sum.value(), t11));
          t14.set_bignode_idx(bn5->index());
        } else {
          t14 = t11;
        }
        output.second = t14;
      }
    }

    return output;
  }

  void
  hoisted_input_broadcast_rotations(CiphertextVector &output,
                                    const Frontend::Term::Ptr &args,
                                    std::vector<int32_t> rotation_indices) {
    assert(isCipher(args));
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args));
    auto &output_vec = output.vec;
    output_vec.resize(rotation_indices.size());
    for (int s = 0; s < output_vec.size(); s++) {
      output_vec[s].level = input1.level;
    }

    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    std::tuple<std::vector<Polynomial>, std::vector<Polynomial>> rotations;
    if (current_partition_size == 1) {
      rotations = hoisted_basic_parallel_keyswitching_internal(
          input1, rotation_indices);
    } else {
      if (USE_BASIC_PARALLEL_KEYSWITCHING) {
        rotations = hoisted_basic_parallel_keyswitching_internal(
            input1, rotation_indices);
      } else {
        rotations = hoisted_input_broadcast_keyswitching_internal(
            input1, rotation_indices);
      }
    }

    auto &rotations_ct0 = std::get<0>(rotations);
    auto &rotations_ct1 = std::get<1>(rotations);

    assert(rotations_ct0.size() == rotation_indices.size());
    assert(rotations_ct1.size() == rotation_indices.size());

    for (int i = 0; i < rotation_indices.size(); i++) {
      output_vec[i].ct0 = rotations_ct0[i];
      output_vec[i].ct1 = rotations_ct1[i];
    }
  }

  void rotate_accumulate(Ciphertext &output,
                         const std::vector<Frontend::Term::Ptr> &args,
                         std::vector<int32_t> rotation_indices) {
    assert(args.size() == rotation_indices.size());
    std::vector<Ciphertext> ciphertext_inputs;
    for (int i = 0; i < args.size(); i++) {
      assert(isCipher(args[i]));
      Ciphertext &ciphertext_input = std::get<Ciphertext>(Objects.at(args[i]));
      ciphertext_inputs.push_back(ciphertext_input);
    }
    auto level = ciphertext_inputs[0].level;
    for (int i = 0; i < args.size(); i++) {
      assert(level == ciphertext_inputs[i].level);
    }

    output.level = level;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    auto key_switch_type = KeySwitchType::Aggregation;
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);

    using LimbType = Term::LimbType;

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;

    auto count = 0;
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto bound = ii < (level % dnum_loc) ? split_size + 1 : split_size;
      std::set<uint16_t> s;
      for (auto i = 0; i < bound; i++) {
        s.insert(s.end(), count);
        count++;
      }
      split.push_back(s);
    }

    PolyPair rotate_accumulate;
    if (current_partition_size == 1) {
      rotate_accumulate = rotate_accumulate_internal(
          ciphertext_inputs, rotation_indices, split, extension_size, dnum_glo);
    } else {
      if (USE_BASIC_PARALLEL_KEYSWITCHING) {
        rotate_accumulate = rotate_accumulate_internal_basic_parallel(
            ciphertext_inputs, rotation_indices, split, extension_size,
            dnum_glo);
      } else {
        // rotate_accumulate =
        // rotate_accumulate_internal_input_broadcast(ciphertext_inputs,rotation_indices,split,extension_size,dnum_glo);
        rotate_accumulate =
            rotate_accumulate_internal(ciphertext_inputs, rotation_indices,
                                       split, extension_size, dnum_glo);
      }
    }

    output.ct0 = rotate_accumulate.first;
    output.ct1 = rotate_accumulate.second;
  }

  void
  bsgs_multiply_accumulate(Ciphertext &output,
                           const std::vector<Frontend::Term::Ptr> &args,
                           std::vector<int32_t> babystep_rotation_indices,
                           std::vector<int32_t> giantstep_rotation_indices) {
    assert(args.size() == babystep_rotation_indices.size() *
                                  giantstep_rotation_indices.size() +
                              1);
    assert(isCipher(args[0]));
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args[0]));
    std::vector<Plaintext> plaintext_inputs;
    for (int i = 1; i < args.size(); i++) {
      assert(isPlain(args[i]));
      Plaintext &plaintext_input = std::get<Plaintext>(Objects.at(args[i]));
      plaintext_inputs.push_back(plaintext_input);
    }
    output.level = input1.level;
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    auto level = input1.level;

    auto key_switch_type = KeySwitchType::Aggregation;
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);

    using LimbType = Term::LimbType;

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;

    auto count = 0;
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto bound = ii < (level % dnum_loc) ? split_size + 1 : split_size;
      std::set<uint16_t> s;
      for (auto i = 0; i < bound; i++) {
        s.insert(s.end(), count);
        count++;
      }
      split.push_back(s);
    }

    std::tuple<std::vector<Polynomial>, std::vector<Polynomial>>
        keyswitch_babystep;
    if (USE_BASIC_PARALLEL_KEYSWITCHING) {
      keyswitch_babystep = hoisted_basic_parallel_keyswitching_internal(
          input1, babystep_rotation_indices);
    } else {
      keyswitch_babystep = hoisted_input_broadcast_keyswitching_internal(
          input1, babystep_rotation_indices);
    }

    PolyPair keyswitch_giantstep;
    if (current_partition_size == 1) {
      keyswitch_giantstep = bsgs_giantstep_accumulate_keyswitch_iterations(
          keyswitch_babystep, plaintext_inputs, giantstep_rotation_indices,
          split, extension_size, dnum_glo);
    } else {
      keyswitch_giantstep = bsgs_giantstep_accumulate_keyswitch_iterations_2(
          keyswitch_babystep, plaintext_inputs, giantstep_rotation_indices,
          split, extension_size, dnum_glo);
    }

    output.ct0 = keyswitch_giantstep.first;
    output.ct1 = keyswitch_giantstep.second;
  }

  void rotate_internal(Ciphertext &output, Ciphertext &input1,
                       int32_t rot_idx) {
    output.level = input1.level;
    assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;
    if (rot_idx == 0) {
      output.ct0 = input1.ct0;
      output.ct1 = input1.ct1;
      return;
    }

    auto t1 = make_treg_from(input1.ct1);
    std::shared_ptr<BigNode> bn1 = create_bignode();
    bn1->push_instruction(std::make_shared<UnOpInstruction>(
        OpCode::Rot, t1, input1.ct1, rot_idx));
    t1.set_bignode_idx(bn1->index());

    auto t2 = make_treg_from(input1.ct0);
    output.ct1 = make_treg_from(input1.ct1);
    keyswitch(t1, t2, output.ct1, EvalKeyType::Rotation, rot_idx);

    auto t0 = make_treg_from(input1.ct0);
    output.ct0 = make_treg_from(input1.ct0);
    std::shared_ptr<BigNode> bn0 = create_bignode();
    bn0->push_instruction(std::make_shared<UnOpInstruction>(
        OpCode::Rot, t0, input1.ct0, rot_idx));
    t0.set_bignode_idx(bn0->index());
    bn0->push_instruction(
        std::make_shared<BinOpInstruction>(OpCode::Add, output.ct0, t0, t2));
    output.ct0.set_bignode_idx(bn0->index());
  }

  void conjugate(Ciphertext &output, const Frontend::Term::Ptr &args1) {

    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    auto t1 = make_treg_from(input1.ct1);
    std::shared_ptr<BigNode> bn1 = create_bignode();
    bn1->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Con, t1, input1.ct1));
    t1.set_bignode_idx(bn1->index());

    auto t2 = make_treg_from(input1.ct0);
    output.ct1 = make_treg_from(input1.ct1);
    keyswitch(t1, t2, output.ct1, EvalKeyType::Conjugation);

    auto t0 = make_treg_from(input1.ct0);
    output.ct0 = make_treg_from(input1.ct0);
    std::shared_ptr<BigNode> bn0 = create_bignode();
    bn0->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Con, t0, input1.ct0));
    t0.set_bignode_idx(bn0->index());
    bn0->push_instruction(
        std::make_shared<BinOpInstruction>(OpCode::Add, output.ct0, t0, t2));
    output.ct0.set_bignode_idx(bn0->index());
  }

  void rotate(Ciphertext &output, const Frontend::Term::Ptr &args1,
              int32_t rot_idx) {
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    rotate_internal(output, input1, rot_idx);
  }

  Polynomial do_rescale(Polynomial &input) {

    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;
    using LimbType = Term::LimbType;

    LimbIndexType last_limbshare = input.level() - 1;

    auto t_lastlimb = make_new_term_share_from(&input);
    t_lastlimb.set_shares(std::set<LimbIndexType>{last_limbshare});

    auto t_remaining_limbs = make_new_term_share_from(&input);
    auto remaining_limbs = t_remaining_limbs.shares();
    remaining_limbs.erase(last_limbshare);
    t_remaining_limbs.set_shares(remaining_limbs);

    auto t_lastlimb_intt = make_treg_from(input);
    t_lastlimb_intt.set_shares(std::set<LimbIndexType>{last_limbshare});
    // t1_lastlimb_intt.set_limbtype(LimbType::Las);

    std::shared_ptr<BigNode> bn_intt = create_bignode();
    bn_intt->push_instruction(std::make_shared<UnOpInstruction>(
        OpCode::Int, t_lastlimb_intt, t_lastlimb));
    t_lastlimb_intt.set_bignode_idx(bn_intt->index());

    std::shared_ptr<BigNode> bn_dist = create_bignode();
    if (num_partitions > 1) {
      bn_dist->push_instruction(
          std::make_shared<DistInstruction>(t_lastlimb_intt));
    }

    auto t_rescale = make_rescale_treg_from(input);
    std::shared_ptr<BigNode> bn = create_bignode();
    bn->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::SuD, t_rescale, t_remaining_limbs, t_lastlimb_intt));
    t_rescale.set_bignode_idx(bn->index());

    if (num_partitions > 1) {
      bn_dist->add_child(bn);
    }

    return t_rescale;
  }

  void rescale(Ciphertext &output, const Frontend::Term::Ptr &args1) {

    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    assert(!input1.ct2.has_value());
    output.level = input1.level - 1;
    // assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;
    using LimbType = Term::LimbType;

    LimbIndexType last_limbshare = input1.level - 1;

    auto t1_lastlimb = make_new_term_share_from(&input1.ct1);
    t1_lastlimb.set_shares(std::set<LimbIndexType>{last_limbshare});

    auto t1_remaining_limbs = make_new_term_share_from(&input1.ct1);
    auto remaining_limbs_1 = t1_remaining_limbs.shares();
    remaining_limbs_1.erase(last_limbshare);
    t1_remaining_limbs.set_shares(remaining_limbs_1);

    auto t1_lastlimb_intt = make_treg_from(input1.ct1);
    t1_lastlimb_intt.set_shares(std::set<LimbIndexType>{last_limbshare});
    // t1_lastlimb_intt.set_limbtype(LimbType::Las);

    std::shared_ptr<BigNode> bn1_intt = create_bignode();
    bn1_intt->push_instruction(std::make_shared<UnOpInstruction>(
        OpCode::Int, t1_lastlimb_intt, t1_lastlimb));
    t1_lastlimb_intt.set_bignode_idx(bn1_intt->index());

    output.ct1 = make_rescale_treg_from(input1.ct1);

    if (num_partitions > 1) {
      std::shared_ptr<BigNode> bn1_dist = create_bignode();
      bn1_dist->push_instruction(
          std::make_shared<DistInstruction>(t1_lastlimb_intt));
      std::shared_ptr<BigNode> bn1 = create_bignode();
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::SuD, output.ct1, t1_remaining_limbs, t1_lastlimb_intt));
      output.ct1.set_bignode_idx(bn1->index());
      bn1_dist->add_child(bn1);
    } else {
      std::shared_ptr<BigNode> bn1 = create_bignode();
      bn1->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::SuD, output.ct1, t1_remaining_limbs, t1_lastlimb_intt));
      output.ct1.set_bignode_idx(bn1->index());
    }

    auto t0_lastlimb = make_new_term_share_from(&input1.ct0);
    t0_lastlimb.set_shares(std::set<LimbIndexType>{last_limbshare});

    auto t0_remaining_limbs = make_new_term_share_from(&input1.ct0);
    auto remaining_limbs_0 = t0_remaining_limbs.shares();
    remaining_limbs_0.erase(last_limbshare);
    t0_remaining_limbs.set_shares(remaining_limbs_0);

    auto t0_lastlimb_intt = make_treg_from(input1.ct0);
    t0_lastlimb_intt.set_shares(std::set<LimbIndexType>{last_limbshare});
    // t0_lastlimb_intt.set_limbtype(LimbType::Las);

    std::shared_ptr<BigNode> bn0_intt = create_bignode();
    bn0_intt->push_instruction(std::make_shared<UnOpInstruction>(
        OpCode::Int, t0_lastlimb_intt, t0_lastlimb));
    t0_lastlimb_intt.set_bignode_idx(bn0_intt->index());

    output.ct0 = make_rescale_treg_from(input1.ct0);

    if (num_partitions > 1) {
      std::shared_ptr<BigNode> bn0_dist = create_bignode();
      bn0_dist->push_instruction(
          std::make_shared<DistInstruction>(t0_lastlimb_intt));
      std::shared_ptr<BigNode> bn0 = create_bignode();
      bn0->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::SuD, output.ct0, t0_remaining_limbs, t0_lastlimb_intt));
      output.ct0.set_bignode_idx(bn0->index());
      bn0_dist->add_child(bn0);
    } else {
      std::shared_ptr<BigNode> bn0 = create_bignode();
      bn0->push_instruction(std::make_shared<BinOpInstruction>(
          OpCode::SuD, output.ct0, t0_remaining_limbs, t0_lastlimb_intt));
      output.ct0.set_bignode_idx(bn0->index());
    }
  }

  void mod_switch_plaintext(Plaintext &output,
                            const Frontend::Term::Ptr &args1) {

    Plaintext &input1 = std::get<Plaintext>(Objects.at(args1));
    output.level = input1.level - 1;
    // assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    output.pt = make_modswitch_treg_from(input1.pt);
  }

  void mod_switch(Ciphertext &output, const Frontend::Term::Ptr &args1) {

    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level - 1;
    // assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    output.ct1 = make_modswitch_treg_from(input1.ct1);
    output.ct0 = make_modswitch_treg_from(input1.ct0);
    if (input1.ct2.has_value()) {
      output.ct2 = make_modswitch_treg_from(input1.ct2.value());
    }
  }

  void bootstrap_mod_raise(Ciphertext &output, const Frontend::Term::Ptr &args1,
                           const uint16_t raise_to_level,
                           bool use_ephemeral_key = false) {

    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    assert(!input1.ct2.has_value());
    output.level = raise_to_level;
    // assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    using LimbType = Term::LimbType;

    // TODO: Set the limbtypes....

    std::shared_ptr<BigNode> bn0_intt = create_bignode();
    auto t0 = make_treg_from(input1.ct0);
    auto t2 = make_treg_from(input1.ct0);
    bn0_intt->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Int, t0, input1.ct0));
    t0.set_bignode_idx(bn0_intt->index());

    if (num_partitions > 1) {
      std::shared_ptr<BigNode> bn_dist = create_bignode();
      auto t0_move = make_treg_from(t0);
      t0_move.set_limbtype(LimbType::Usp);
      t0_move.set_bignode_idx(bn_dist->index());
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t0_move, t0));
      t0 = t0_move;
      // bn_dist->add_child(bn0_intt);
      bn0_intt = create_bignode();
      bn_dist->add_child(bn0_intt);
    }

    bn0_intt->push_instruction(std::make_shared<ResolveInstruction>(t2, t0));
    t2.set_bignode_idx(bn0_intt->index());

    auto raised_level = raise_to_level + 1;

    std::shared_ptr<BigNode> bn0_mod = create_bignode();
    auto t4 = make_treg(raise_to_level);
    auto t6 = make_treg_from(t4);
    bn0_mod->push_instruction(std::make_shared<ModInstruction>(t4, t2));
    t4.set_bignode_idx(bn0_mod->index());
    bn0_mod->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Ntt, t6, t4));
    t6.set_bignode_idx(bn0_mod->index());

    std::shared_ptr<BigNode> bn1_intt = create_bignode();
    auto t1 = make_treg_from(input1.ct1);
    auto t3 = make_treg_from(input1.ct1);
    bn1_intt->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Int, t1, input1.ct1));
    t1.set_bignode_idx(bn1_intt->index());

    if (num_partitions > 1) {
      std::shared_ptr<BigNode> bn_dist = create_bignode();
      auto t1_move = make_treg_from(t1);
      t1_move.set_limbtype(LimbType::Usp);
      t1_move.set_bignode_idx(bn_dist->index());
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t1_move, t1));
      t1 = t1_move;
      // bn_dist->add_child(bn0_intt);
      bn1_intt = create_bignode();
      bn_dist->add_child(bn1_intt);
    }

    bn1_intt->push_instruction(std::make_shared<ResolveInstruction>(t3, t1));
    t3.set_bignode_idx(bn1_intt->index());

    auto extension_size = input1.level; // + 1;

    std::shared_ptr<BigNode> bn1_mod = create_bignode();
    PolyPair t5, t7;
    t5.first = make_treg(raised_level);
    // t5.first.set_limbtype(LimbType::Usp);
    t5.second = make_treg(raised_level);
    t5.second.set_limbtype(LimbType::Ext);
    t5.second.set_extension_size(extension_size);

    t7.first = make_treg_from(t5.first);
    // t7.first.set_limbtype(LimbType::Usp);
    t7.second = make_treg_from(t5.second);
    t7.second.set_limbtype(LimbType::Ext);
    t7.second.set_extension_size(extension_size);

    bn1_mod->push_instruction(std::make_shared<ModInstruction>(t5.first, t3));
    bn1_mod->push_instruction(std::make_shared<ModInstruction>(t5.second, t3));
    t5.first.set_bignode_idx(bn1_mod->index());
    t5.second.set_bignode_idx(bn1_mod->index());
    bn1_mod->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Ntt, t7.first, t5.first));
    bn1_mod->push_instruction(
        std::make_shared<UnOpInstruction>(OpCode::Ntt, t7.second, t5.second));
    t7.first.set_bignode_idx(bn1_mod->index());
    t7.second.set_bignode_idx(bn1_mod->index());

    auto level = input1.level;
    auto key_switch_type = KeySwitchType::Broadcast;
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);

    std::pair<PolyPair, PolyPair> evalkeys;
    if (use_ephemeral_key) {
      evalkeys = get_evalkey(input1.ct1, EvalKeyType::Bootstrap2,
                             raise_to_level, key_switch_type, extension_size);
    } else {
      evalkeys = get_evalkey(input1.ct1, EvalKeyType::Bootstrap, raise_to_level,
                             key_switch_type, extension_size);
    }

    auto split_size = raised_level / dnum_loc;

    SplitType split;

    auto count = 0;
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto bound = ii < (level % dnum_loc) ? split_size + 1 : split_size;
      std::set<uint16_t> s;
      for (auto i = 0; i < bound; i++) {
        s.insert(s.end(), count);
        count++;
      }
      split.push_back(s);
    }

    auto evk0 = evalkeys.first;
    auto evk1 = evalkeys.second;

    std::shared_ptr<BigNode> bn_ks = create_bignode();
    bn_ks->set_split_up(split);

    std::pair<Polynomial, Polynomial> t8;
    t8.first = make_treg_from(t7.first);
    // t8.first.set_limbtype(LimbType::Usp);
    t8.second = make_treg_from(t7.second);
    t8.second.set_limbtype(LimbType::Ext);
    t8.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t9;
    t9.first = make_treg_from(t7.first);
    // t9.first.set_limbtype(LimbType::Usp);
    t9.second = make_treg_from(t7.second);
    t9.second.set_limbtype(LimbType::Ext);
    t9.second.set_extension_size(extension_size);

    bn_ks->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t8.first, t7.first, evk0.first));
    bn_ks->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t8.second, t7.second, evk0.second));
    bn_ks->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t9.first, t7.first, evk1.first));
    bn_ks->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t9.second, t7.second, evk1.second));

    t8.first.set_bignode_idx(bn_ks->index());
    t8.second.set_bignode_idx(bn_ks->index());
    t9.first.set_bignode_idx(bn_ks->index());
    t9.second.set_bignode_idx(bn_ks->index());

    std::pair<Polynomial, Polynomial> t10;
    t10.first = make_treg_from(t8.first);
    // t10.first.set_limbtype(LimbType::Usp);
    t10.second = make_treg_from(t8.second);
    t10.second.set_limbtype(LimbType::Ext);
    t10.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t11;
    t11.first = make_treg_from(t9.first);
    // t11.first.set_limbtype(LimbType::Usp);
    t11.second = make_treg_from(t9.second);
    t11.second.set_limbtype(LimbType::Ext);
    t11.second.set_extension_size(extension_size);

    bn_ks->push_instruction(
        std::make_shared<JoinLocalInstruction>(t10.first, t8.first));
    bn_ks->push_instruction(
        std::make_shared<JoinLocalInstruction>(t10.second, t8.second));
    bn_ks->push_instruction(
        std::make_shared<JoinLocalInstruction>(t11.first, t9.first));
    bn_ks->push_instruction(
        std::make_shared<JoinLocalInstruction>(t11.second, t9.second));

    t10.first.set_bignode_idx(bn_ks->index());
    t10.second.set_bignode_idx(bn_ks->index());
    t11.first.set_bignode_idx(bn_ks->index());
    t11.second.set_bignode_idx(bn_ks->index());

    auto bn1 = create_bignode();
    auto b1 = make_bcor_from(t10.first);
    // b1.set_limbtype(LimbType::Usp);
    bn1->push_instruction(std::make_shared<PlInstruction1>(b1, t10.second));
    b1.set_bignode_idx(bn1->index());

    bn1->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn1);
    bn2->set_ks_pl_head_idx(bn1);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(t11.first);
    // b2.set_limbtype(LimbType::Usp);
    bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t11.second));
    b2.set_bignode_idx(bn2->index());

    // Polynomial t11 = make_treg_from(t5.first);
    Polynomial t12 = make_treg_from(t10.first);
    bn2->push_instruction(std::make_shared<PlInstruction7>(t12, t10.first, b1));
    t12.set_bignode_idx(bn2->index());

    bn1->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn1);
    bn3->set_ks_pl_no(3);

    Polynomial t13 = make_treg_from(t11.first);
    bn3->push_instruction(std::make_shared<PlInstruction7>(t13, t11.first, b2));
    t13.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);

    output.ct1 = do_rescale(t13);

    Polynomial t14 = do_rescale(t12);

    auto bn = create_bignode();
    output.ct0 = make_treg_from(t14);
    output.ct0.set_bignode_idx(bn->index());
    bn->push_instruction(
        std::make_shared<BinOpInstruction>(OpCode::Add, output.ct0, t14, t6));
  }

  void process_output(Frontend::Term::Ptr &args1, const std::string &name) {
    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    assert(!input1.ct2.has_value());
    input1.ct0.set_output(true);
    input1.ct1.set_output(true);

    input1.ct0.set_symbol(name + ":c0");
    input1.ct1.set_symbol(name + ":c1");
  }

  void relinearize(Ciphertext &output, const Frontend::Term::Ptr &args1) {

    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    // assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    assert(input1.ct2.has_value());

    auto k0 = make_treg_from(input1.ct0);
    auto k1 = make_treg_from(input1.ct1);
    keyswitch(input1.ct2.value(), k0, k1, EvalKeyType::Relinearization, 0);

    auto bn0 = create_bignode();
    output.ct0 = make_treg_from(input1.ct0);
    bn0->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Add, output.ct0, input1.ct0, k0));
    output.ct0.set_bignode_idx(bn0->index());

    auto bn1 = create_bignode();
    output.ct1 = make_treg_from(input1.ct1);
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Add, output.ct1, input1.ct1, k1));
    output.ct1.set_bignode_idx(bn1->index());
  }

  void to_ephemeral(Ciphertext &output, const Frontend::Term::Ptr &args1) {

    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    auto t2 = make_treg_from(input1.ct0);
    output.ct1 = make_treg_from(input1.ct1);
    keyswitch2(input1.ct1, t2, output.ct1, EvalKeyType::Ephemeral);

    output.ct0 = make_treg_from(input1.ct0);
    std::shared_ptr<BigNode> bn0 = create_bignode();
    bn0->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Add, output.ct0, input1.ct0, t2));
    output.ct0.set_bignode_idx(bn0->index());
  }

  void relinearize2(Ciphertext &output, const Frontend::Term::Ptr &args1) {

    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    // assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    assert(input1.ct2.has_value());

    auto k0 = make_treg_from(input1.ct0);
    auto k1 = make_treg_from(input1.ct1);
    keyswitch2(input1.ct2.value(), k0, k1, EvalKeyType::Relinearization, 0);

    auto bn0 = create_bignode();
    output.ct0 = make_treg_from(input1.ct0);
    bn0->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Add, output.ct0, input1.ct0, k0));
    output.ct0.set_bignode_idx(bn0->index());

    auto bn1 = create_bignode();
    output.ct1 = make_treg_from(input1.ct1);
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Add, output.ct1, input1.ct1, k1));
    output.ct1.set_bignode_idx(bn1->index());
  }

  void keyswitch_basic_parallel(Polynomial &inp, Polynomial &out0,
                                Polynomial &out1, EvalKeyType evk_type,
                                int32_t rot_idx = 0) {
    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    using LimbType = Term::LimbType;

    auto key_switch_type = KeySwitchType::BasicParallel;
    auto level = inp.level();
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);
    assert(dnum_glo == 1);

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    auto evalkeys =
        get_evalkey(inp, evk_type, rot_idx, key_switch_type, extension_size);

    SplitType split;
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto count = 0;
      std::set<uint16_t> s;
      for (auto l = ii; l < level; l += dnum_loc) {
        s.insert(l);
        count++;
        if (count == extension_size) {
          split.push_back(s);
          s.clear();
          count = 0;
        }
      }
      if (!s.empty()) {
        split.push_back(s);
        s.clear();
        count = 0;
      }
    }

    auto evk0 = evalkeys.first;
    auto evk1 = evalkeys.second;

    auto t0 = inp;

    std::shared_ptr<BigNode> bn0 = nullptr;
    if (current_partition_size > 1) {
      std::shared_ptr<BigNode> bn_dist = create_bignode();
      auto t0_move = make_treg_from(t0);
      t0_move.set_limbtype(LimbType::Spl);
      t0_move.set_bignode_idx(bn_dist->index());
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t0_move, t0));
      t0 = t0_move;
      bn0 = bn_dist;
    } else {
      bn0 = create_bignode();
    }

    // XXX: The different parts of the evalkeys should be different bignodes
    // while reading
    auto pl_idx = get_pl_idx();
    bn0->set_ks_pl_no(1);

    auto b0 = make_bcor_from(t0);
    b0.set_limbtype(LimbType::PaE_Split);
    b0.set_extension_size(extension_size);
    bn0->push_instruction(std::make_shared<PlInstruction1>(b0, t0));
    bn0->set_split_up(split);
    bn0->set_dont_split_modular(true);
    bn0->set_ks_pl_idx(pl_idx);
    b0.set_bignode_idx(bn0->index());

    auto t1 = make_treg_from(inp);
    t1.set_limbtype(LimbType::Par);
    auto t2 = make_treg_from(inp);
    t2.set_limbtype(LimbType::Ext_Split);
    t2.set_extension_size(extension_size);

    std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn0);
    bn1->set_ks_pl_no(2);
    bn1->push_instruction(std::make_shared<PlInstruction8>(t1, t2, b0, t0));
    bn1->set_split_up(split);
    bn1->set_dont_split_modular(true);
    bn1->set_ks_pl_head_idx(bn0);
    bn1->set_ks_pl_idx(pl_idx);
    t1.set_bignode_idx(bn1->index());
    t2.set_bignode_idx(bn1->index());

    bn0->set_next_bignode_in_group(bn1);

    std::pair<Polynomial, Polynomial> t3;
    t3.first = make_treg_from(t1);
    t3.first.set_limbtype(LimbType::Par);
    t3.second = make_treg_from(t2);
    t3.second.set_limbtype(LimbType::Ext_Split);
    t3.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t4;
    t4.first = make_treg_from(t1);
    t4.first.set_limbtype(LimbType::Par);
    t4.second = make_treg_from(t2);
    t4.second.set_limbtype(LimbType::Ext_Split);
    t4.second.set_extension_size(extension_size);

    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t3.first, t1, evk0.first));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t3.second, t2, evk0.second));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t4.first, t1, evk1.first));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t4.second, t2, evk1.second));

    t3.first.set_bignode_idx(bn1->index());
    t3.second.set_bignode_idx(bn1->index());
    t4.first.set_bignode_idx(bn1->index());
    t4.second.set_bignode_idx(bn1->index());

    std::pair<Polynomial, Polynomial> t5;
    t5.first = make_treg_from(t1);
    t5.first.set_limbtype(LimbType::Par);
    t5.second = make_treg_from(t2);
    t5.second.set_limbtype(LimbType::Ext_Split);
    t5.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t6;
    t6.first = make_treg_from(t1);
    t6.first.set_limbtype(LimbType::Par);
    t6.second = make_treg_from(t2);
    t6.second.set_limbtype(LimbType::Ext_Split);
    t6.second.set_extension_size(extension_size);

    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

    t5.first.set_bignode_idx(bn1->index());
    t5.second.set_bignode_idx(bn1->index());
    t6.first.set_bignode_idx(bn1->index());
    t6.second.set_bignode_idx(bn1->index());

    std::pair<Polynomial, Polynomial> t7;
    t7.first = t5.first;

    std::pair<Polynomial, Polynomial> t8;
    t8.first = t6.first;

    if (current_partition_size > 1) {

      t7.second = make_treg_from(t5.second);
      t7.second.set_limbtype(LimbType::Ext);
      t7.second.set_extension_size(extension_size);

      t8.second = make_treg_from(t6.second);
      t8.second.set_limbtype(LimbType::Ext);
      t8.second.set_extension_size(extension_size);

      std::shared_ptr<BigNode> bn_dist = create_bignode();
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t7.second, t5.second));
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t8.second, t6.second));

      t7.second.set_bignode_idx(bn_dist->index());
      t8.second.set_bignode_idx(bn_dist->index());

    } else {
      t7.second = t5.second;
      t8.second = t6.second;
    }

    auto bn = create_bignode();
    auto b1 = make_bcor_from(t7.first);
    b1.set_limbtype(LimbType::Par);
    bn->push_instruction(std::make_shared<PlInstruction1>(b1, t7.second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_head_idx(bn);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(t8.first);
    b2.set_limbtype(LimbType::Par);
    bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t8.second));
    b2.set_bignode_idx(bn2->index());

    bn2->push_instruction(std::make_shared<PlInstruction7>(out0, t7.first, b1));
    out0.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn);
    bn3->set_ks_pl_no(3);

    bn3->push_instruction(std::make_shared<PlInstruction7>(out1, t8.first, b2));
    out1.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);
  }

  void keyswitch(Polynomial &inp, Polynomial &out0, Polynomial &out1,
                 EvalKeyType evk_type, int32_t rot_idx = 0) {
    if (USE_BASIC_PARALLEL_KEYSWITCHING) {
      return keyswitch_basic_parallel(inp, out0, out1, evk_type, rot_idx);
    } else {
      return keyswitch_input_broadcast(inp, out0, out1, evk_type, rot_idx);
      // return keyswitch_output_aggregation(inp,out0,out1,evk_type,rot_idx);
    }
  }

  void keyswitch_input_broadcast(Polynomial &inp, Polynomial &out0,
                                 Polynomial &out1, EvalKeyType evk_type,
                                 int32_t rot_idx = 0) {
    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    using LimbType = Term::LimbType;

    auto key_switch_type = KeySwitchType::Broadcast;
    auto level = inp.level();
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);
    assert(dnum_glo == 1);

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    auto evalkeys =
        get_evalkey(inp, evk_type, rot_idx, key_switch_type, extension_size);

    SplitType split;
    // for (auto ii = 0; ii < num_partitions; ii++) {
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto count = 0;
      std::set<uint16_t> s;
      for (auto l = ii; l < level; l += dnum_loc) {
        s.insert(l);
        count++;
        if (count == extension_size) {
          split.push_back(s);
          s.clear();
          count = 0;
        }
      }
      if (!s.empty()) {
        split.push_back(s);
        s.clear();
        count = 0;
      }
    }

    auto evk0 = evalkeys.first;
    auto evk1 = evalkeys.second;

    auto t0 = inp;
    Polynomial b0;

    std::shared_ptr<BigNode> bn0 = nullptr;
    auto pl_idx = get_pl_idx();
    if (current_partition_size > 1) {
      std::shared_ptr<BigNode> bn_intt = create_bignode();
      t0 = make_treg_from(inp);
      // t0.set_limbtype(LimbType::Spl);
      t0.set_bignode_idx(bn_intt->index());
      bn_intt->push_instruction(
          std::make_shared<UnOpInstruction>(OpCode::Int, t0, inp));
      std::shared_ptr<BigNode> bn_dist = create_bignode();
      auto t0_move = make_treg_from(t0);
      t0_move.set_limbtype(LimbType::Spl);
      t0_move.set_bignode_idx(bn_dist->index());
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t0_move, t0));
      t0 = t0_move;
      bn0 = bn_dist;
      auto pl_idx = get_pl_idx();
      bn0->set_ks_pl_no(1);

      b0 = make_bcor_from(t0);
      b0.set_limbtype(LimbType::PaE);
      b0.set_extension_size(extension_size);
      bn0->push_instruction(std::make_shared<PlInstruction1>(
          b0, t0, false /*No inverse NTT required */));
      bn0->set_split_up(split);
      bn0->set_dont_split_modular(true);
      bn0->set_ks_pl_idx(pl_idx);
      b0.set_bignode_idx(bn0->index());
    } else {
      bn0 = create_bignode();
      bn0->set_ks_pl_no(1);

      b0 = make_bcor_from(t0);
      b0.set_limbtype(LimbType::PaE);
      b0.set_extension_size(extension_size);
      bn0->push_instruction(std::make_shared<PlInstruction1>(b0, t0));
      bn0->set_split_up(split);
      bn0->set_dont_split_modular(true);
      bn0->set_ks_pl_idx(pl_idx);
      b0.set_bignode_idx(bn0->index());
    }

    auto t1 = make_treg_from(inp);
    t1.set_limbtype(LimbType::Par);
    auto t2 = make_treg_from(inp);
    t2.set_limbtype(LimbType::Ext);
    t2.set_extension_size(extension_size);

    std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn0);
    bn1->set_ks_pl_no(2);
    bn1->push_instruction(std::make_shared<PlInstruction8>(t1, t2, b0, inp));
    bn1->set_split_up(split);
    bn1->set_dont_split_modular(true);
    bn1->set_ks_pl_head_idx(bn0);
    bn1->set_ks_pl_idx(pl_idx);
    t1.set_bignode_idx(bn1->index());
    t2.set_bignode_idx(bn1->index());

    bn0->set_next_bignode_in_group(bn1);

    std::pair<Polynomial, Polynomial> t3;
    t3.first = make_treg_from(t1);
    t3.first.set_limbtype(LimbType::Par);
    t3.second = make_treg_from(t2);
    t3.second.set_limbtype(LimbType::Ext);
    t3.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t4;
    t4.first = make_treg_from(t1);
    t4.first.set_limbtype(LimbType::Par);
    t4.second = make_treg_from(t2);
    t4.second.set_limbtype(LimbType::Ext);
    t4.second.set_extension_size(extension_size);

    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t3.first, t1, evk0.first));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t3.second, t2, evk0.second));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t4.first, t1, evk1.first));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t4.second, t2, evk1.second));

    t3.first.set_bignode_idx(bn1->index());
    t3.second.set_bignode_idx(bn1->index());
    t4.first.set_bignode_idx(bn1->index());
    t4.second.set_bignode_idx(bn1->index());

    std::pair<Polynomial, Polynomial> t5;
    t5.first = make_treg_from(t1);
    t5.first.set_limbtype(LimbType::Par);
    t5.second = make_treg_from(t2);
    t5.second.set_limbtype(LimbType::Ext);
    t5.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t6;
    t6.first = make_treg_from(t1);
    t6.first.set_limbtype(LimbType::Par);
    t6.second = make_treg_from(t2);
    t6.second.set_limbtype(LimbType::Ext);
    t6.second.set_extension_size(extension_size);

    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

    t5.first.set_bignode_idx(bn1->index());
    t5.second.set_bignode_idx(bn1->index());
    t6.first.set_bignode_idx(bn1->index());
    t6.second.set_bignode_idx(bn1->index());

    auto bn = create_bignode();
    // auto bn = bn1;
    auto b1 = make_bcor_from(t5.first);
    b1.set_limbtype(LimbType::Par);
    bn->push_instruction(std::make_shared<PlInstruction1>(b1, t5.second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_head_idx(bn);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(t6.first);
    b2.set_limbtype(LimbType::Par);
    bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t6.second));
    b2.set_bignode_idx(bn2->index());

    bn2->push_instruction(std::make_shared<PlInstruction7>(out0, t5.first, b1));
    out0.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn);
    bn3->set_ks_pl_no(3);

    bn3->push_instruction(std::make_shared<PlInstruction7>(out1, t6.first, b2));
    out1.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);
  }

  // keyswitch2 uses a dnum_loc of levels. This avoids the errors associated
  // with fast base conversion and results in more precision. However, this
  // comes at the cost of higher computation costs.
  void keyswitch2(Polynomial &inp, Polynomial &out0, Polynomial &out1,
                  EvalKeyType evk_type, int32_t rot_idx = 0) {
    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    using LimbType = Term::LimbType;

    auto key_switch_type = KeySwitchType::Broadcast;
    auto level = inp.level();
    uint16_t dnum_glo = 1;
    uint16_t dnum_loc = level;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);
    assert(dnum_glo == 1);

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;
    auto evalkeys =
        get_evalkey(inp, evk_type, rot_idx, key_switch_type, extension_size);

    SplitType split;
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto count = 0;
      std::set<uint16_t> s;
      for (auto l = ii; l < level; l += dnum_loc) {
        s.insert(l);
        count++;
        if (count == extension_size) {
          split.push_back(s);
          s.clear();
          count = 0;
        }
      }
      if (!s.empty()) {
        split.push_back(s);
        s.clear();
        count = 0;
      }
    }
    auto evk0 = evalkeys.first;
    auto evk1 = evalkeys.second;

    auto t0 = inp;

    std::shared_ptr<BigNode> bn0 = nullptr;
    if (current_partition_size > 1) {
      std::shared_ptr<BigNode> bn_dist = create_bignode();
      auto t0_move = make_treg_from(t0);
      t0_move.set_limbtype(LimbType::Spl);
      t0_move.set_bignode_idx(bn_dist->index());
      bn_dist->push_instruction(
          std::make_shared<DistRecvMovInstruction>(t0_move, t0));
      t0 = t0_move;
      bn0 = bn_dist;
    } else {
      bn0 = create_bignode();
    }

    // XXX: The different parts of the evalkeys should be different bignodes
    // while reading
    auto pl_idx = get_pl_idx();
    // std::shared_ptr<BigNode> bn0 = create_bignode();
    bn0->set_ks_pl_no(1);

    auto b0 = make_bcor_from(t0);
    b0.set_limbtype(LimbType::PaE);
    b0.set_extension_size(extension_size);
    bn0->push_instruction(std::make_shared<PlInstruction1>(b0, t0));
    bn0->set_split_up(split);
    bn0->set_dont_split_modular(true);
    bn0->set_ks_pl_idx(pl_idx);
    b0.set_bignode_idx(bn0->index());

    auto t1 = make_treg_from(inp);
    t1.set_limbtype(LimbType::Par);
    auto t2 = make_treg_from(inp);
    t2.set_limbtype(LimbType::Ext);
    t2.set_extension_size(extension_size);

    std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn0);
    bn1->set_ks_pl_no(2);
    bn1->push_instruction(std::make_shared<PlInstruction8>(t1, t2, b0, t0));
    bn1->set_split_up(split);
    bn1->set_dont_split_modular(true);
    bn1->set_ks_pl_head_idx(bn0);
    bn1->set_ks_pl_idx(pl_idx);
    t1.set_bignode_idx(bn1->index());
    t2.set_bignode_idx(bn1->index());

    bn0->set_next_bignode_in_group(bn1);

    std::pair<Polynomial, Polynomial> t3;
    t3.first = make_treg_from(t1);
    t3.first.set_limbtype(LimbType::Par);
    t3.second = make_treg_from(t2);
    t3.second.set_limbtype(LimbType::Ext);
    t3.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t4;
    t4.first = make_treg_from(t1);
    t4.first.set_limbtype(LimbType::Par);
    t4.second = make_treg_from(t2);
    t4.second.set_limbtype(LimbType::Ext);
    t4.second.set_extension_size(extension_size);

    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t3.first, t1, evk0.first));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t3.second, t2, evk0.second));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t4.first, t1, evk1.first));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t4.second, t2, evk1.second));

    t3.first.set_bignode_idx(bn1->index());
    t3.second.set_bignode_idx(bn1->index());
    t4.first.set_bignode_idx(bn1->index());
    t4.second.set_bignode_idx(bn1->index());

    std::pair<Polynomial, Polynomial> t5;
    t5.first = make_treg_from(t1);
    t5.first.set_limbtype(LimbType::Par);
    t5.second = make_treg_from(t2);
    t5.second.set_limbtype(LimbType::Ext);
    t5.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t6;
    t6.first = make_treg_from(t1);
    t6.first.set_limbtype(LimbType::Par);
    t6.second = make_treg_from(t2);
    t6.second.set_limbtype(LimbType::Ext);
    t6.second.set_extension_size(extension_size);

    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

    t5.first.set_bignode_idx(bn1->index());
    t5.second.set_bignode_idx(bn1->index());
    t6.first.set_bignode_idx(bn1->index());
    t6.second.set_bignode_idx(bn1->index());

    auto bn = create_bignode();
    // auto bn = bn1;
    auto b1 = make_bcor_from(t5.first);
    b1.set_limbtype(LimbType::Par);
    bn->push_instruction(std::make_shared<PlInstruction1>(b1, t5.second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_head_idx(bn);
    bn2->set_ks_pl_no(2);

    auto b2 = make_bcor_from(t6.first);
    b2.set_limbtype(LimbType::Par);
    bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t6.second));
    b2.set_bignode_idx(bn2->index());

    // Polynomial t11 = make_treg_from(t5.first);
    bn2->push_instruction(std::make_shared<PlInstruction7>(out0, t5.first, b1));
    out0.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn2);
    bn3->set_ks_pl_head_idx(bn);
    bn3->set_ks_pl_no(3);

    // Polynomial t12 = make_treg_from(t6.first);
    bn3->push_instruction(std::make_shared<PlInstruction7>(out1, t6.first, b2));
    out1.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);
  }

  void keyswitch_output_aggregation(Polynomial &inp, Polynomial &out0,
                                    Polynomial &out1, EvalKeyType evk_type,
                                    int32_t rot_idx = 0) {
    using OpCode = PolynomialInstruction::OpCode;
    using PolynomialInstruction = PolynomialInstruction;
    using LimbType = Term::LimbType;

    auto level = inp.level();
    auto key_switch_type = KeySwitchType::Aggregation;
    auto dnums = get_keyswitch_dnum(level, key_switch_type);
    auto dnum_glo = dnums.first;
    auto dnum_loc = dnums.second;
    assert(dnum_loc > 0);
    assert(dnum_glo > 0);

    using LimbType = Term::LimbType;

    auto split_size = level / dnum_loc;
    auto extension_size = (level % dnum_loc) ? split_size + 1 : split_size;
    extension_size = (extension_size % dnum_glo) == 0
                         ? extension_size / dnum_glo
                         : (extension_size / dnum_glo) + 1;

    SplitType split;

    auto count = 0;
    for (auto ii = 0; ii < dnum_loc; ii++) {
      auto bound = ii < (level % dnum_loc) ? split_size + 1 : split_size;
      std::set<uint16_t> s;
      for (auto i = 0; i < bound; i++) {
        s.insert(s.end(), count);
        count++;
      }
      split.push_back(s);
    }

    auto evalkeys =
        get_evalkey(inp, evk_type, rot_idx, key_switch_type, extension_size);

    auto pl_idx = get_pl_idx();
    auto b0 = make_bcor_from(inp);
    b0.set_limbtype(LimbType::ExC);
    b0.set_extension_size(extension_size);

    auto bn = create_bignode();
    bn->set_ks_pl_no(1);
    bn->push_instruction(std::make_shared<PlInstruction1>(b0, inp));
    bn->set_split_up(split);
    bn->set_ks_pl_idx(pl_idx);
    b0.set_bignode_idx(bn->index());

    std::shared_ptr<BigNode> bn1 = create_bignode_in_group(bn);

    auto t1 = make_treg_from(inp);
    t1.set_limbtype(LimbType::Usp);
    auto t2 = make_treg_from(inp);
    t2.set_limbtype(LimbType::Ext);
    t2.set_extension_size(extension_size);

    bn1->push_instruction(std::make_shared<PlInstruction8>(t1, t2, b0, inp));
    bn1->set_split_up(split);
    bn1->set_ks_pl_head_idx(bn);
    bn1->set_ks_pl_idx(pl_idx);
    t1.set_bignode_idx(bn1->index());
    t2.set_bignode_idx(bn1->index());

    bn->set_next_bignode_in_group(bn1);

    std::pair<Polynomial, Polynomial> t3;
    t3.first = make_treg_from(t1);
    t3.first.set_limbtype(LimbType::Usp);
    t3.second = make_treg_from(t2);
    t3.second.set_limbtype(LimbType::Ext);
    t3.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t4;
    t4.first = make_treg_from(t1);
    t4.first.set_limbtype(LimbType::Usp);
    t4.second = make_treg_from(t2);
    t4.second.set_limbtype(LimbType::Ext);
    t4.second.set_extension_size(extension_size);

    auto evk0 = evalkeys.first;
    auto evk1 = evalkeys.second;
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t3.first, t1, evk0.first));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t3.second, t2, evk0.second));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t4.first, t1, evk1.first));
    bn1->push_instruction(std::make_shared<BinOpInstruction>(
        OpCode::Mul, t4.second, t2, evk1.second));

    t3.first.set_bignode_idx(bn1->index());
    t3.second.set_bignode_idx(bn1->index());
    t4.first.set_bignode_idx(bn1->index());
    t4.second.set_bignode_idx(bn1->index());

    std::pair<Polynomial, Polynomial> t5;
    t5.first = make_treg_from(t1);
    t5.first.set_limbtype(LimbType::Usp);
    t5.second = make_treg_from(t2);
    t5.second.set_limbtype(LimbType::Ext);
    t5.second.set_extension_size(extension_size);

    std::pair<Polynomial, Polynomial> t6;
    t6.first = make_treg_from(t1);
    t6.first.set_limbtype(LimbType::Usp);
    t6.second = make_treg_from(t2);
    t6.second.set_limbtype(LimbType::Ext);
    t6.second.set_extension_size(extension_size);

    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t5.first, t3.first));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t5.second, t3.second));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t6.first, t4.first));
    bn1->push_instruction(
        std::make_shared<JoinLocalInstruction>(t6.second, t4.second));

    t5.first.set_bignode_idx(bn1->index());
    t5.second.set_bignode_idx(bn1->index());
    t6.first.set_bignode_idx(bn1->index());
    t6.second.set_bignode_idx(bn1->index());

    bn = create_bignode();
    auto b1 = make_bcor_from(t5.first);
    b1.set_limbtype(LimbType::Usp);
    bn->push_instruction(std::make_shared<PlInstruction1>(b1, t5.second));
    b1.set_bignode_idx(bn->index());

    bn->set_ks_pl_no(1);

    auto bn2 = create_bignode_in_group(bn);
    bn2->set_ks_pl_no(2);

    // Polynomial t11 = make_treg_from(t5.first);
    Polynomial &t11 = out0;
    bn2->push_instruction(std::make_shared<PlInstruction7>(t11, t5.first, b1));
    t11.set_bignode_idx(bn2->index());

    auto b2 = make_bcor_from(t6.first);
    b2.set_limbtype(LimbType::Usp);
    bn2->push_instruction(std::make_shared<PlInstruction1>(b2, t6.second));
    b2.set_bignode_idx(bn2->index());

    bn->set_next_bignode_in_group(bn2);

    auto bn3 = create_bignode_in_group(bn);
    bn3->set_ks_pl_no(3);

    Polynomial &t12 = out1;
    // Polynomial t12 = make_treg_from(t6.first);
    bn3->push_instruction(std::make_shared<PlInstruction7>(t12, t6.first, b2));
    t12.set_bignode_idx(bn3->index());

    bn2->set_next_bignode_in_group(bn3);

    if (dnum_glo != 1) {
      t11.set_limbtype(LimbType::Usp);
      t11.set_join_reqd(true);
      t12.set_limbtype(LimbType::Usp);
      t12.set_join_reqd(true);
    }

    return;
  }

  void output(Ciphertext &output) {

    output.ct0.term()->set_output(true);
    output.ct1.term()->set_output(true);
  }
  template <typename T> T &initValue(const Frontend::Term::Ptr &term) {
    return std::get<T>(Objects[term] = T{});
  }

  void partition(uint8_t partition_size, uint8_t partition_id) {
    current_partition_size = partition_size;
    current_partition_id = partition_id;
  }

  void receive(Ciphertext &output, const Frontend::Term::Ptr &args1,
               const std::uint8_t dest_partition_size,
               const std::uint8_t dest_partition_id) {

    Ciphertext &input1 = std::get<Ciphertext>(Objects.at(args1));
    output.level = input1.level;
    assert(!input1.ct2.has_value());
    using PolynomialInstruction = PolynomialInstruction;
    using OpCode = PolynomialInstruction::OpCode;

    auto bn0 = BigNodes[input1.ct0.bignode_index()];
    auto bn1 = BigNodes[input1.ct1.bignode_index()];

    auto src_partition_size = bn0->partition_size();
    auto src_partition_id = bn0->partition_id();

    assert(src_partition_size == bn1->partition_size());
    assert(src_partition_id == bn1->partition_id());

    if (src_partition_size > dest_partition_size) {
      ;
    } else if (dest_partition_size > src_partition_size) {
      bn0 = create_bignode(dest_partition_size, dest_partition_id);
      bn1 = create_bignode(dest_partition_size, dest_partition_id);
    }

    output.ct1 = make_treg_from(input1.ct1);
    auto receive_instruction1 =
        std::make_shared<ReceiveInstruction>(output.ct1, input1.ct1);
    // receive_instruction1->set_dest_partition_size(current_partition_size);
    // receive_instruction1->set_dest_partition_id(current_partition_id);
    receive_instruction1->set_dest_partition_size(dest_partition_size);
    receive_instruction1->set_dest_partition_id(dest_partition_id);
    receive_instruction1->set_src_partition_size(src_partition_size);
    receive_instruction1->set_src_partition_id(src_partition_id);
    bn1->push_instruction(receive_instruction1);
    output.ct1.set_bignode_idx(bn1->index());

    output.ct0 = make_treg_from(input1.ct0);
    auto receive_instruction0 =
        std::make_shared<ReceiveInstruction>(output.ct0, input1.ct0);
    // receive_instruction0->set_dest_partition_size(current_partition_size);
    // receive_instruction0->set_dest_partition_id(current_partition_id);
    receive_instruction0->set_dest_partition_size(dest_partition_size);
    receive_instruction0->set_dest_partition_id(dest_partition_id);
    receive_instruction0->set_src_partition_size(src_partition_size);
    receive_instruction0->set_src_partition_id(src_partition_id);
    bn0->push_instruction(receive_instruction0);
    output.ct0.set_bignode_idx(bn0->index());
  }

  void process_input(const Frontend::Term::Ptr &term) {
    switch (term->get<TypeAttribute>()) {
    case Type::Cipher: {
      auto &output = initValue<Ciphertext>(term);
      // uint16_t limbs = levels - term->getLevel(); // change to max levels
      uint16_t limbs = term->getLevel(); // change to max levels
      output.ct0 = make_input(limbs);
      output.ct1 = make_input(limbs);
      output.level = limbs;
      auto &name = term->get<NameAttribute>();
      output.ct0.set_symbol(name + ":c0");
      output.ct1.set_symbol(name + ":c1");
    } break;
    case Type::Plain: {
      auto &output = initValue<Plaintext>(term);
      // uint16_t level = levels - term->getLevel(); // change to max levels
      uint16_t level = term->getLevel(); // change to max levels
      auto &name = term->get<NameAttribute>();
      if (term->get<IsScalarAttribute>() == true) {
        output.pt = make_scalar(level);
        output.pt.set_symbol(name + ":s");
      } else {
        output.pt = make_plaintext(level);
        output.pt.set_symbol(name + ":p");
      }
    } break;
    default:
      throw std::runtime_error("Invalid Type");
    }
    return;
  }

  void enable_only_if_atleast_one_ciphertext_operand(
      const Frontend::Term::Ptr &term) {
    auto args = term->getOperands();
    bool all_args_plain = true;
    for (auto &arg : args) {
      if (isCipher(arg)) {
        all_args_plain = false;
        break;
      }
    }
    if (all_args_plain) {
      throw std::runtime_error(
          "All operands cannot be plaintext for operation: " +
          getOpName(term->getOp()));
    }
  }

  void enable_only_if_all_ciphertext_operand(const Frontend::Term::Ptr &term) {
    auto args = term->getOperands();
    bool all_args_cipher = true;
    for (auto &arg : args) {
      if (!isCipher(arg)) {
        all_args_cipher = false;
        break;
      }
    }
    if (!all_args_cipher) {
      throw std::runtime_error(
          "All operands must be ciphertexts for operation: " +
          getOpName(term->getOp()));
    }
  }

  void process_unop(const Frontend::Term::Ptr &term) {
    auto args = term->getOperands();
    assert(args.size() == 1);
    if (!isCipher(args[0])) {
      throw std::runtime_error("Operand must be a ciphertext for operation: " +
                               getOpName(term->getOp()));
    }
    assert(isCipher(args[0]));
    auto &output = initValue<Ciphertext>(term);

    switch (term->getOp()) {
    case Op::Negate: {
      negate(output, args[0]);
    } break;
    case Op::RotateLeftConst: {
      rotate(output, args[0], term->get<RotationAttribute>());
    } break;
    case Op::RotateRightConst: {
      rotate(output, args[0], -(term->get<RotationAttribute>()));
    } break;
    case Op::Conjugate: {
      conjugate(output, args[0]);
    } break;
    case Op::Rescale: {
      rescale(output, args[0]);
    } break;
    case Op::ToEphemeral: {
      to_ephemeral(output, args[0]);
    } break;
    case Op::Relinearize: {
      relinearize(output, args[0]);
    } break;
    case Op::Relinearize2: {
      relinearize2(output, args[0]);
    } break;
    case Op::BootstrapModRaise: {
      uint16_t raise_to_level =
          levels - term->get<ModRaiseLevelAttribute>(); // change to max levels
      bootstrap_mod_raise(output, args[0], raise_to_level, true);
    } break;
    case Op::Receive: {
      receive(output, args[0], term->getPartitionSize(),
              term->getPartitionId());
    } break;
    case Op::Output: {
      process_output(args[0], term->get<NameAttribute>());
    } break;
    default:
      throw std::runtime_error("Invalid UnOp " + getOpName(term->getOp()));
    }
  }

  void process_binop(const Frontend::Term::Ptr &term) {

    auto args = term->getOperands();
    assert(args.size() == 2);
    enable_only_if_atleast_one_ciphertext_operand(term);
    assert(isCipher(args[0]) || isPlain(args[0]));
    assert(isCipher(args[1]) || isPlain(args[1]));
    auto &output = initValue<Ciphertext>(term);

    switch (term->getOp()) {
    case Op::Sub: {
      sub(output, args[0], args[1], false);
    } break;
    case Op::Add: {
      add(output, args[0], args[1]);
    } break;
    case Op::Mul: {
      mul(output, args[0], args[1]);
    } break;
    default:
      throw std::runtime_error("Invalid BinOp " + getOpName(term->getOp()));
    }
  }

  void reset_globals() {
    merge_count = 0;
    block_size = 1000;
    BigNodes.clear();
    Groups.clear();
    Chains.clear();
    io_limbs.clear();
  }

public:
  CinnamonCompiler(Program &g, const uint32_t levels,
                   const uint8_t num_partitions, const uint64_t num_vregs,
                   const std::string &output_prefix, const bool use_cinnamon_keyswitching)
      : program(g), Objects(g), levels(levels), num_partitions(num_partitions),
        current_partition_size(num_partitions), current_partition_id(0), USE_BASIC_PARALLEL_KEYSWITCHING(!use_cinnamon_keyswitching), 
        num_vregs(num_vregs), output_prefix(output_prefix) {
    create_bignode();
    reset_globals();
  }

  void operator()(const Frontend::Term::Ptr &term) {
    auto args = term->getOperands();
    switch (term->getOp()) {
    case Op::Nop:
      // Do nothing
      break;
    case Op::Input:
      process_input(term);
      break;
    case Op::Negate:
    case Op::RotateLeftConst:
    case Op::RotateRightConst:
    case Op::Conjugate:
    case Op::Rescale:
    case Op::ToEphemeral:
    case Op::Relinearize:
    case Op::Relinearize2:
    case Op::BootstrapModRaise:
    case Op::Receive:
    case Op::Output:
      process_unop(term);
      break;
    case Op::Sub:
    case Op::Add:
    case Op::Mul:
      process_binop(term);
      break;
    case Op::ModSwitch: {
      assert(args.size() == 1);
      if (isCipher(args[0])) { // works on cipher
        auto &output = initValue<Ciphertext>(term);
        mod_switch(output, args[0]);
      } else if (isPlain(args[0])) {
        auto &output = initValue<Plaintext>(term);
        mod_switch_plaintext(output, args[0]);
      } else {
        assert(0);
      }
    } break;

    case Op::RotAcc:
      if (isPlain(args[0])) {
        // Unimplimented for now
        throw std::runtime_error("Unhandled op " + getOpName(term->getOp()));
      } else { // works on cipher, no plaintext support
        auto &rotation_indices = term->get<MultiRotationAttribute>();
        assert(args.size() == rotation_indices.size());
        for (auto &arg : args) {
          assert(isCipher(arg));
        }
        auto &output = initValue<Ciphertext>(term);
        rotate_accumulate(output, args, rotation_indices);
      }
      break;
    case Op::RotMulAcc:
      assert(args.size() > 1);
      if (isPlain(args[0])) {
        // Unimplimented for now
        throw std::runtime_error("Unhandled op " + getOpName(term->getOp()));
      } else { // works on cipher, no plaintext support
        auto &rotation_indices = term->get<RotMulAccRotationAttribute>();
        assert(args.size() == rotation_indices.size() + 1);
        assert(isCipher(args[0]));
        auto &output = initValue<Ciphertext>(term);
        rotate_multiply_accumulate(output, args, rotation_indices);
      }
      break;
    case Op::MulRotAcc:
      assert(args.size() > 1);
      if (isPlain(args[0])) {
        // Unimplimented for now
        throw std::runtime_error("Unhandled op " + getOpName(term->getOp()));
      } else { // works on cipher, no plaintext support
        auto &rotation_indices = term->get<RotMulAccRotationAttribute>();
        assert(args.size() == rotation_indices.size() + 1);
        assert(isCipher(args[0]));
        auto &output = initValue<Ciphertext>(term);
        multiply_rotate_accumulate(output, args, rotation_indices);
      }
      break;
    case Op::BsgsMulAcc:
      assert(args.size() > 1);
      if (isPlain(args[0])) {
        // Unimplimented for now
        throw std::runtime_error("Unhandled op " + getOpName(term->getOp()));
      } else { // works on cipher, no plaintext support
        auto &babystep_rotation_indices =
            term->get<BsgsMulAccBabyStepAttribute>();
        auto &giantstep_rotation_indices =
            term->get<BsgsMulAccGiantStepAttribute>();
        assert(args.size() == babystep_rotation_indices.size() *
                                      giantstep_rotation_indices.size() +
                                  1);
        assert(isCipher(args[0]));
        auto &output = initValue<Ciphertext>(term);
        bsgs_multiply_accumulate(output, args, babystep_rotation_indices,
                                 giantstep_rotation_indices);
      }
      break;
    case Op::TermInCiphertextVec:
      assert(args.size() == 1);
      if (isPlain(args[0])) {
        // Unimplimented for now
        throw std::runtime_error("Unhandled op " + getOpName(term->getOp()));
      } else { // works on cipher, no plaintext support
        auto &extract_idx = term->get<TermIdxInVec>();
        assert(isCiphertextVector(args[0]));
        auto &output = initValue<Ciphertext>(term);
        CiphertextVector &ct_vec =
            std::get<CiphertextVector>(Objects.at(args[0]));
        output = ct_vec.vec.at(extract_idx);
      }
      break;
    case Op::HoistInpBroadcast:
      assert(args.size() == 1);
      if (isPlain(args[0])) {
        // Unimplimented for now
        throw std::runtime_error("Unhandled op " + getOpName(term->getOp()));
      } else { // works on cipher, no plaintext support
        auto &output = initValue<CiphertextVector>(term);
        auto rotataion_indices = term->get<MultiRotationAttribute>();
        hoisted_input_broadcast_rotations(output, args[0], rotataion_indices);
      }
      break;

    case Op::Partition: {
      partition(term->get<PartitionSizeAttribute>(),
                term->get<PartitionIdAttribute>());
    } break;
    default:
      throw std::runtime_error("Unhandled op : " + getOpName(term->getOp()));
    }
  }

  struct pairComparer {
    bool operator()(const std::pair<uint64_t, IndexType> &lhs,
                    const std::pair<uint64_t, IndexType> &rhs) {
      if (lhs.first >= rhs.first) {
        return true;
      } else {
        return false;
      }
    }
  };

  std::vector<ChainIndexType>
  expand_instructions_limbwise(std::shared_ptr<BigNode> &bn) {
    std::set<LimbIndexType> limbs;
    auto &instrs = bn->instructions();
    for (auto &instr : instrs) {
      if (instr->opcode() == PolynomialInstruction::OpCode::Syn) {
        auto syn_instr =
            std::dynamic_pointer_cast<SyncGlobalInstruction>(instr);
        assert(syn_instr != nullptr);
        auto &sync_limbs = syn_instr->sync_limbs();
        limbs.insert(sync_limbs.begin(), sync_limbs.end());
        continue;
      }
      for (auto &src : instr->srcs()) {
        auto &src_limbs = src->limbs();
        limbs.insert(src_limbs.begin(), src_limbs.end());
      }
      for (auto &dst : instr->dests()) {
        if (dst->is_bcor()) {
          continue;
        }
        auto &dst_limbs = dst->limbs();
        limbs.insert(dst_limbs.begin(), dst_limbs.end());
      }
    }

    std::vector<ChainIndexType> chains;
    for (auto it = limbs.rbegin(); it != limbs.rend(); it++) {
      auto chain = create_chain();
      for (auto &instr : instrs) {
        auto limb_instr = instr->get_limb_instruction(*it);
        if (limb_instr != nullptr) {
          chain->push_instruction(limb_instr);
        }
      }
      chains.push_back(chain->index());
    }

    return chains;
  }
  void print_graph_simple2() {

    std::ofstream graph_file("graph_simple.dot");
    assert(graph_file.is_open());
    uint16_t MAX_LIMBS = 64;
    graph_file << "Digraph G {\n";
    std::map<TermIndexType, std::vector<uint64_t>> created_at;
    std::map<TermIndexType, uint64_t> created_at_instr;
    std::unordered_set<BigNodeIndexType> visited;
    for (auto i = 0; i < BigNodes.size(); i++) {

      using OpCode = PolynomialInstruction::OpCode;

      auto idx = BigNodes[i]->index();
      if (visited.find(idx) != visited.end()) {
        continue;
      }
      visited.insert(idx);
      auto &bn = BigNodes[idx];
      graph_file << "\tsubgraph cluster_BN" << idx << "{\n ";
      graph_file << "\t\tb" << idx << "[label=\"BN" << idx << ":("
                 << (uint32_t)bn->partition_size() << ":"
                 << (uint32_t)bn->partition_id() << ")"
                 << "\"]\n";

      uint64_t instructions = 0;
      auto instructions_copy = instructions;
      auto instrs = bn->instructions();
      for (auto &instr : instrs) {
        instructions++;

        auto dests = instr->dests();
        for (auto &dst : dests) {
          auto dst_idx = dst->term_idx();
          if (created_at.find(dst_idx) == created_at.end()) {
            created_at[dst_idx] = std::vector<uint64_t>(MAX_LIMBS, 0);
          }
          std::vector<uint64_t> &vec = created_at[dst_idx];
          for (auto &share : dst->shares()) {
            vec[share] = instructions;
          }
        }
        graph_file << "\t\tt" << instructions << "[label=\"" << instr->ppOp()
                   << "\"]\n";
      }
      if (!bn->split_up().empty()) {
        graph_file << "\t\tt" << idx << "_splitU [label=\"Split Up\"]\n";
      }

      graph_file << "\t}\n";
    }
    graph_file << "}";
    graph_file << std::flush;
  }

  void print_graph_simple(std::vector<BigNodeIndexType> &top_sort) {

    uint16_t MAX_LIMBS = 64;
    uint64_t instructions = 0;
    std::cout << "Digraph G {\n";
    std::map<TermIndexType, std::vector<uint64_t>> created_at;
    std::map<TermIndexType, uint64_t> created_at_instr;
    for (auto i = 0; i < top_sort.size(); i++) {

      using OpCode = PolynomialInstruction::OpCode;

      auto idx = top_sort[i];
      std::cout << "\tsubgraph cluster_BN" << idx << "{\n ";
      std::cout << "\t\tb" << idx << "[label=\"BN" << idx << "\"]\n";

      auto instructions_copy = instructions;
      auto &bn = BigNodes[idx];
      auto instrs = bn->instructions();
      for (auto &instr : instrs) {
        instructions++;

        auto dests = instr->dests();
        for (auto &dst : dests) {
          auto dst_idx = dst->term_idx();
          if (created_at.find(dst_idx) == created_at.end()) {
            created_at[dst_idx] = std::vector<uint64_t>(MAX_LIMBS, 0);
          }
          std::vector<uint64_t> &vec = created_at[dst_idx];
          for (auto &limb : dst->limbs()) {
            vec[limb] = instructions;
          }
        }
        std::cout << "\t\tt" << instructions << "[label=\"" << instr->ppOp()
                  << "\"]\n";
      }
      if (!bn->split_up().empty()) {
        std::cout << "\t\tt" << idx << "_splitU [label=\"Split Up\"]\n";
      }

      std::cout << "\t}\n";
    }
  }

  void print_graph(std::vector<BigNodeIndexType> &top_sort,
                   std::string outfile_name) {

    std::ofstream graph_file(outfile_name);
    assert(graph_file.is_open());
    bool kill = false;
    uint16_t MAX_LIMBS = 64;
    uint64_t instructions = 0;
    graph_file << "Digraph G {\n";
    std::map<TermIndexType, std::vector<uint64_t>> created_at;
    std::map<TermIndexType, uint64_t> created_at_instr;
    for (auto i = 0; i < top_sort.size(); i++) {

      using OpCode = PolynomialInstruction::OpCode;

      auto idx = top_sort[i];
      auto &bn = BigNodes[idx];
      graph_file << "\tsubgraph cluster_BN" << idx << "{\n ";
      graph_file << "\t\tb" << idx << "[label=\"BN" << idx << ":("
                 << (uint32_t)bn->partition_size() << ":"
                 << (uint32_t)bn->partition_id() << ")"
                 << "\"]\n";

      auto instructions_copy = instructions;
      auto instrs = bn->instructions();
      for (auto &instr : instrs) {
        instructions++;
        auto dests = instr->dests();
        for (auto &dst : dests) {
          auto dst_idx = dst->term_idx();
          if (created_at.find(dst_idx) == created_at.end()) {
            created_at[dst_idx] = std::vector<uint64_t>(MAX_LIMBS, 0);
          }
          std::vector<uint64_t> &vec = created_at[dst_idx];
          for (auto &limb : dst->limbs()) {
            vec[limb] = instructions;
          }
        }
        graph_file << "\t\tt" << instructions << "[label=\"" << instr->ppOp()
                   << "\"]\n";
      }
      if (!bn->split_up().empty()) {
        graph_file << "\t\tt" << idx << "_splitU [label=\"Split Up\"]\n";
      }

      graph_file << "\t}\n";

      for (auto &instr : instrs) {
        instructions_copy++;
        auto srcs = instr->srcs();
        auto cnt = 0;
        for (auto &src : srcs) {
          if (src->is_input()) {
            continue;
          }
          if (instr->opcode() == OpCode::Rec && src->num_shares() == 0) {
            continue;
          }
          if (created_at.find(src->term_idx()) == created_at.end()) {
            graph_file << "ERR: " << instr->ppOp() << "\n";
            graph_file << std::flush;
            kill = true;
            goto fail;
            continue;
          }
          auto &vec = created_at[src->term_idx()];
          std::set<uint64_t> deps;
          for (auto &limb : src->limbs()) {
            deps.insert(vec[limb]);
          }
          for (auto &dep : deps) {
            if (dep == 0 && src->is_input()) {
              continue;
            }
            if (dep == 0 && instr->opcode() == OpCode::Drm) {
              continue;
            }
            if (dep == 0 && instr->opcode() == OpCode::Rec) {
              continue;
            }
            if (dep == 0 && instr->opcode() == OpCode::Pip8 &&
                !src->is_bcor()) {
              continue;
            }
            if (dep == 0) {
              graph_file << "ERR: " << instr->ppOp() << "\n";
              kill = true;
              goto fail;
              continue;
            }
            assert(dep != 0);
            graph_file << "\tt" << dep << " -> t" << instructions_copy << " [";
            graph_file << "label=\"" << cnt;
            graph_file << "\" ";
            if (src->is_bcor()) {
              graph_file << "color=blue ";
            }
            graph_file << "]\n";
          }
          cnt++;
        }
      }
    }

    for (auto i = 1; i < top_sort.size(); i++) {
      graph_file << "\tb" << top_sort[i - 1] << " -> b" << top_sort[i] << " [";
      graph_file << "color=red";
      graph_file << "]\n";
    }

    graph_file << "}\n";
    graph_file.close();

  fail:
    if (kill) {
      graph_file << std::flush;
      assert(0);
    }
  }

  ~CinnamonCompiler() {}

  void print_chains(uint32_t partition) {
    std::ofstream chain_file("chain_instructions" + std::to_string(partition) +
                             ".txt");
    assert(chain_file.is_open());
    uint64_t chain_instructions = 0;
    for (auto i = 0; i < top_sort_chains[partition].size(); i++) {
      auto idx = top_sort_chains[partition][i];
      auto &ch = Chains[idx];
      chain_file << "ChIdx: " << idx << "\n";
      chain_file << "Inputs: " << ch->ppInputs() << "\n";
      for (auto &instr : ch->instructions()) {
        chain_instructions++;
        chain_file << instr->ppOp() << "\n";
      }
      chain_file << "Outputs: " << ch->ppOutputs() << "\n";
      chain_file << "===============================\n";
    }
    chain_file << "Chain instructions: " << chain_instructions << "\n";
    chain_file.close();
  }

  // Topologically Sort the instructions in a bignode
  void topologically_sort_instructions(
      std::vector<CinnamonCompiler::InstructionSharedPtr> &instructions,
      std::unordered_set<TermIndexType> &created_terms) {
    // Vector to store instructions in the topologically sorted order
    std::vector<CinnamonCompiler::InstructionSharedPtr> instructions_topsort;
    auto instruction_count = instructions.size();
    size_t instructions_topsort_size_old;
    while (instructions_topsort.size() < instruction_count) {
      instructions_topsort_size_old = instructions_topsort.size();
      for (auto &instruction : instructions) {
        if (instruction == nullptr) {
          continue;
        }
        bool all_dependencies_created = true;
        const std::vector<Polynomial *> &srcs = instruction->srcs();
        for (auto &src : srcs) {
          if (created_terms.find(src->term_idx()) == created_terms.end()) {
            all_dependencies_created = false;
            break;
          }
        }
        if (all_dependencies_created) {
          const std::vector<Polynomial *> &dests = instruction->dests();
          for (auto &dest : dests) {
            created_terms.insert(dest->term_idx());
          }
          instructions_topsort.push_back(instruction);
          instruction.reset();
        }
      }
      if (instructions_topsort.size() == instructions_topsort_size_old) {
        throw std::runtime_error(
            "ERROR: Infinte Loop Found While Topologically Sorting "
            "Instructions in the BigNode. This indicates that something is "
            "wrond and that there is a cyclical dependency in the "
            "instructions. Something is terribly wrong.");
      }
    }
    instructions = instructions_topsort;
  }

  void mark_terms_for_global_aggregation(
      std::vector<CinnamonCompiler::InstructionSharedPtr> &instructions,
      BigNodeIndexType bn_index) {
    for (auto &instruction : instructions) {
      const std::vector<Polynomial *> &srcs = instruction->srcs();
      PolynomialInstruction::OpCode opcode = instruction->opcode();
      for (auto &src : srcs) {
        if (src->join_reqd()) {
          if (opcode > PolynomialInstruction::OpCode::Joi) {
            assert(src->bignode_index() != bn_index);
            src->set_join_here(true);
          }
        }
      }
    }
  }

  // This is a simpler version of the topological sort. Since the BigNodes are
  // created in program order, it is enough to just iterate over the BigNodes in
  // order
  void topologically_sort_bignodes2() {
    std::unordered_set<TermIndexType> created_terms;
    for (size_t i = 0; i < BigNodes.size(); i++) {
      auto &bn = BigNodes[i];
      auto bn_index = bn->index();
      std::vector<CinnamonCompiler::InstructionSharedPtr> &instructions =
          bn->instructions();
      topologically_sort_instructions(instructions, created_terms);
      mark_terms_for_global_aggregation(instructions, bn_index);
      top_sort.push_back(bn_index);
    }
  }

  void topologically_sort_bignodes() {

    std::vector<bool> visited(Groups.size(), false);

    // Number of Bignodes in a block
    std::deque<BigNodeIndexType> bignodes_indegree0;

    std::unordered_set<TermIndexType> created_terms;

    // auto block_size = block_size;
    for (auto i = 0; i < Groups.size(); i += block_size) {
      // Iterate over the bignodes, inserting the bignodes with
      // no parents in the bignodes_indegree0 queue. These will used to
      // topologically sort the bignodes.
      for (auto j = 0; j < block_size; j++) {
        if (i + j >= Groups.size()) {
          // All groups are processed
          break;
        }
        const std::shared_ptr<Group> &group = Groups[i + j];
        GroupIndexType group_index = group->group_index();

        if (visited.at(group_index) == true) {
          continue;
        }

        visited[group_index] = true;

        group->refresh_parents();
        group->refresh_children();

        std::shared_ptr<BigNode> bn = BigNodes[group->bn_index()];

        // If this bignode isn't the first bignode in the group,
        // Get the first bignode in the group
        if (bn->ks_pl_head_idx() != 0) {
          bn = BigNodes[bn->ks_pl_head_idx()];
        }

        const std::set<GroupIndexType> &parents = bn->parents_group();
        if (parents.empty()) {
          auto ks_pl_no = bn->ks_pl_no();
          if (ks_pl_no == 0 || ks_pl_no == 1) {
            bignodes_indegree0.push_back(bn->index());
          } else {
            assert(0);
          }
        }
      }

      // Topologicaly Sort the BigNodes,
      // And Instructions inside the bignodes
      while (!bignodes_indegree0.empty()) {
        // There should be atleast one bignode with indegree 0
        auto &top = bignodes_indegree0.front();
        auto &bn = BigNodes[top];
        auto bn_index = bn->index();

        std::vector<CinnamonCompiler::InstructionSharedPtr> &instructions =
            bn->instructions();

        topologically_sort_instructions(instructions, created_terms);
        mark_terms_for_global_aggregation(instructions, bn_index);

        auto &children = bn->children_group();
        top_sort.push_back(bn_index);
        bignodes_indegree0.pop_front();

        // If this is not the last bignode in the group, insert the next bignode
        // in the group at the front of the queue and continue
        BigNodeIndexType next_bignode_in_group_index =
            bn->next_bignode_in_group_index();
        if (next_bignode_in_group_index != 0) {
          bignodes_indegree0.push_front(next_bignode_in_group_index);
          continue;
        }

        // If the current bignode is the last bignode in the group, we are done
        // processing the group Erase the group as a parent of its children.
        // Insert children with no parents in the queue for processing.
        // (Topologically sorted order of bignodes)
        const GroupIndexType group_index =
            Groups[bn->group_index()]->group_index();
        for (auto &child : children) {
          Groups[child]->delete_parent_noref(group_index);
          if (Groups[child]->parents().empty()) {
            auto next_node = BigNodes[Groups[child]->bn_index()];
            if (next_node->ks_pl_head_idx() != 0) {
              next_node = BigNodes[next_node->ks_pl_head_idx()];
            }
            bignodes_indegree0.push_front(next_node->index());
          }
        }
      }
    }

    // Ensure that every bignode has been accounted for in the top sort
    std::unordered_set<BigNodeIndexType> unique_bn_indices;
    for (auto &bn : BigNodes) {
      unique_bn_indices.insert(bn->index());
    }

    // TODO: We are only checking size.
    // But we need to actually check that all the values are present
    assert(top_sort.size() == unique_bn_indices.size());
  }

  bool get_print_graph() {
    if (const char *env_p = std::getenv("CINNAMON_PRINT_GRAPH")) {
      auto env_str = std::string(env_p);
      if (env_str == "true") {
        return true;
      } else {
        return false;
      }
    }
    return false;
  }

  void split_keyswitch_pipeline_bignodes() {
    decltype(top_sort) top_sort_new;
    for (auto i = 0; i < top_sort.size(); i++) {

      auto idx = top_sort[i];
      auto bn = BigNodes[idx];
      if (bn->split_up().size() == 0 &&
          bn->next_bignode_in_group_index() == 0) {
        top_sort_new.push_back(idx);
        continue;
      }

      std::vector<std::shared_ptr<BigNode>> to_split;

      auto bn_it = bn;
      auto split_size = bn_it->split_up().size();
      int split_bn_count = 0;
      // while(bn_it->next_ks_bn_idx() != 0){
      while (true) {
        assert(bn_it->index() ==
               BigNodes[top_sort[i + split_bn_count]]->index());
        assert(bn_it->split_up().size() == split_size);
        to_split.push_back(bn_it);
        split_bn_count++;
        if (bn_it->next_bignode_in_group_index() == 0) {
          break;
        }
        bn_it = BigNodes[bn_it->next_bignode_in_group_index()];
      }

      if (split_size == 0) {
        for (auto &b : to_split) {
          top_sort_new.push_back(b->index());
        }
        i += to_split.size() -
             1; // -1 because the i++ at the end of the loop will be executed
        continue;
      }

      std::vector<std::vector<BigNodeIndexType>> splits;

      for (auto &b : to_split) {
        auto bn_split = split_bignode(b);
        splits.push_back(bn_split);
      }

      for (auto i = 0; i <= split_size; i++) {
        for (auto &bn_split : splits) {
          if (i < bn_split.size()) {
            top_sort_new.push_back(bn_split[i]);
          }
        }
      }

      i += to_split.size() - 1;
    }
    top_sort = std::move(top_sort_new);
  }

  void set_instruction_sync_ids() {

    if (num_partitions <= 1) {
      return;
    }
    uint64_t sync_id = 1;
    for (auto i = 0; i < top_sort.size(); i++) {
      using OpCode = PolynomialInstruction::OpCode;
      auto idx = top_sort[i];
      auto bn = BigNodes[idx];
      for (auto &instr : bn->instructions()) {
        auto opcode = instr->opcode();
        if (opcode == OpCode::Joi) {
          auto instr_ = std::dynamic_pointer_cast<JoinGlobalInstruction>(instr);
          assert(instr_ != nullptr);
          instr_->set_sync_id(sync_id++);
          instr_->set_sync_size(bn->partition_size());
        } else if (opcode == OpCode::Dis) {
          auto instr_ = std::dynamic_pointer_cast<DistInstruction>(instr);
          assert(instr_ != nullptr);
          instr_->set_sync_id(sync_id++);
          instr_->set_sync_size(bn->partition_size());
        } else if (opcode == OpCode::Drm) {
          auto instr_ =
              std::dynamic_pointer_cast<DistRecvMovInstruction>(instr);
          assert(instr_ != nullptr);
          instr_->set_sync_id(sync_id++);
          instr_->set_sync_size(bn->partition_size());
        } else if (opcode == OpCode::Rec) {
          auto instr_ = std::dynamic_pointer_cast<ReceiveInstruction>(instr);
          assert(instr_ != nullptr);
          instr_->set_sync_id(sync_id++);
        }
      }
    }
  }

  void create_partitions() {
    if (num_partitions == 1) {
      top_sort_partitions.push_back(std::move(top_sort));
      return;
    }

    for (auto i = 0; i < num_partitions; i++) {
      std::vector<BigNodeIndexType> vec;
      vec.reserve(top_sort.size());
      top_sort_partitions.push_back(std::move(vec));
    }
    for (auto i = 0; i < top_sort.size(); i++) {
      auto idx = top_sort[i];
      auto bn = BigNodes[idx];
      auto mod_split = split_bignode_modular(bn);
      for (auto j = 0; j < num_partitions; j++) {
        if (!BigNodes[mod_split[j]]->empty()) {
          top_sort_partitions[j].push_back(mod_split[j]);
        }
      }
      bn.reset();
    }
    top_sort.clear();
  }

  void allocate_registers(uint32_t partition) {
    auto i = partition;
    RegisterAllocator registerAllocator(num_vregs);
    for (auto j = 0; j < top_sort_chains[i].size(); j++) {

      using OpCode = PolynomialInstruction::OpCode;
      auto idx = top_sort_chains[i][j];
      auto &ch = Chains[idx];
      auto &instrs = ch->instructions();

      for (auto it = instrs.begin(); it != instrs.end(); it++) {
        registerAllocator.process_instruction(*it);
      }
    }
    registerAllocator.finish(output_prefix + "instructions", i);
    registerAllocator.write_program_inputs(output_prefix + "inputs", i, terms);
  }

  void write_program_inputs() {
    std::stringstream ciphertext_stream, plaintext_stream, scalar_stream,
        output_stream, evalkey_stream;

    for (auto it = io_limbs.begin(); it != io_limbs.end(); it++) {
      std::shared_ptr<Term> term = terms[it->first];
      std::stringstream s;
      s << term->name() << " | " << term->symbol() << " | [";
      int count = 0;
      for (const LimbIndexType limb : it->second) {
        if (count != 0) {
          s << ",";
        }
        s << limb;
        count++;
      }
      s << "]\n";
      if (term->is_input()) {
        if (term->is_eval_key()) {
          evalkey_stream << s.str();
        } else if (term->is_plaintext()) {
          if (term->is_scalar()) {
            scalar_stream << s.str();
          } else {
            plaintext_stream << s.str();
          }
        } else {
          ciphertext_stream << s.str();
        }
      } else if (term->is_output()) {
        output_stream << s.str();
      } else {
        assert(0);
      }
    }
    std::ofstream program_inputs_file(output_prefix + "program_inputs");
    assert(program_inputs_file.is_open());
    program_inputs_file << "Ciphertext Stream:\n"
                        << ciphertext_stream.str() << ";\n";
    program_inputs_file << "Plaintext Stream:\n"
                        << plaintext_stream.str() << ";\n";
    program_inputs_file << "Scalar Stream:\n" << scalar_stream.str() << ";\n";
    program_inputs_file << "Output Stream:\n" << output_stream.str() << ";\n";
    program_inputs_file << "Evalkey Stream:\n" << evalkey_stream.str() << ";\n";
  }

  void insert_join_global_instructions() {
    decltype(top_sort) top_sort_new;
    for (auto i = 0; i < top_sort.size(); i++) {
      auto idx = top_sort[i];
      top_sort_new.push_back(idx);
      auto bn = BigNodes[idx];
      auto &instrs = bn->instructions();
      for (auto &instr : instrs) {
        auto dests = instr->dests();
        for (auto &dst : dests) {
          if (dst->join_here()) {
            auto dst_after_join = *dst;
            *dst = make_copy_treg_from(dst);

            // Rewrite all uses of this polynomial after join with the temporary
            // copy in this bignode so that we don't end up depending on a value
            // in the future
            for (auto &instr_ : bn->instructions()) {
              for (auto &src : instr_->srcs()) {
                if (src->term_idx() == dst_after_join.term_idx()) {
                  *src = *dst;
                }
              }
            }

            auto join_bn =
                create_bignode(bn->partition_size(), bn->partition_id());
            join_bn->push_join_instruction(
                std::make_shared<JoinGlobalInstruction>(dst_after_join, *dst));
            auto join_bn_idx = join_bn->index();
            top_sort_new.push_back(join_bn_idx);
            dst_after_join.set_bignode_idx(join_bn_idx);
            dst_after_join.set_limbtype(Term::LimbType::Spl);
            dst_after_join.set_join_reqd(false);
            dst_after_join.set_join_here(false);

            auto next_bn_in_group_idx = bn->next_bignode_in_group_index();
            if (next_bn_in_group_idx != 0) {
              bn->set_next_bignode_in_group(join_bn);
              join_bn->set_next_bignode_in_group(
                  BigNodes[next_bn_in_group_idx]);
            }
          }
        }
      }
    }
    top_sort = std::move(top_sort_new);
  }

  void
  iterate_bignodes_inorder(std::function<void(std::shared_ptr<BigNode>)> f) {
    for (auto i = 0; i < num_partitions; i++) {
      for (auto j = 0; j < top_sort_partitions[i].size(); j++) {
        auto idx = top_sort_partitions[i][j];
        auto &bn = BigNodes[idx];
        f(bn);
      }
    }
  };

  void iterate_instructions_all_partitions(
      std::function<void(std::uint32_t partition, std::shared_ptr<BigNode> &,
                         std::shared_ptr<PolynomialInstruction> &)>
          f) {
    for (auto i = 0; i < num_partitions; i++) {
      for (auto j = 0; j < top_sort_partitions[i].size(); j++) {
        auto idx = top_sort_partitions[i][j];
        auto &bn = BigNodes[idx];
        auto &instrs = bn->instructions();
        for (auto &instr : instrs) {
          f(i, bn, instr);
        }
      }
    }
  };

  void iterate_instructions_partition(
      uint32_t partition,
      std::function<void(std::shared_ptr<BigNode> &,
                         std::shared_ptr<PolynomialInstruction> &)>
          f) {
    if (partition >= num_partitions) {
      return;
    }
    for (auto j = 0; j < top_sort_partitions[partition].size(); j++) {
      auto idx = top_sort_partitions[partition][j];
      auto &bn = BigNodes[idx];
      auto &instrs = bn->instructions();
      for (auto &instr : instrs) {
        f(bn, instr);
      }
    }
  }

  void iterate_instruction_in_bignode(
      std::shared_ptr<BigNode> &bn,
      std::function<void(std::shared_ptr<PolynomialInstruction>)> f) {
    auto &instrs = bn->instructions();
    for (auto &instr : instrs) {
      f(instr);
    }
  };

  void set_instruction_limbs(uint32_t partition) {
    iterate_instructions_partition(
        partition, [&partition](std::shared_ptr<BigNode> &bn,
                                std::shared_ptr<PolynomialInstruction> &instr) {
          instr->set_limbs(partition, bn->partition_size());
        });
  }

  void do_next_use_analysis(uint32_t partition) {
    auto i = partition;
    uint64_t chain_instructions = 0;
    for (auto j = 0; j < top_sort_chains[i].size(); j++) {

      using OpCode = PolynomialInstruction::OpCode;
      auto idx = top_sort_chains[i][j];
      auto &ch = Chains[idx];
      auto &instrs = ch->instructions();

      for (auto it = instrs.begin(); it != instrs.end();) {
        auto &instr = *it;
        if (instr->opcode() == OpCode::Inp) {
          it = instrs.erase(it);
          continue;
        }

        const std::vector<Limb *> &srcs = instr->srcs();
        for (auto jt = srcs.rbegin(); jt != srcs.rend(); jt++) {
          Limb *src = *jt;
          src->set_last_use(chain_instructions);
        }
        const std::vector<Limb *> &dests = instr->dests();
        for (auto jt = dests.rbegin(); jt != dests.rend(); jt++) {
          Limb *dst = *jt;
          dst->set_last_use(chain_instructions);
        }
        it++;
        chain_instructions++;
      }
    }

    uint64_t reverse_chain_instructions = chain_instructions;
    LimbMap<uint64_t> next_use;
    PolynomialMap<uint64_t> polynomial_next_use;
    for (int j = top_sort_chains[i].size() - 1; j >= 0; j--) {
      auto idx = top_sort_chains[i][j];
      auto &ch = Chains[idx];
      auto &instrs = ch->instructions();
      for (auto it = instrs.rbegin(); it != instrs.rend(); it++) {
        reverse_chain_instructions--;
        auto &instr = *it;

        // Use reverse iterators because we are processing instructions
        // in reverse order
        const std::vector<Limb *> &srcs = instr->srcs();
        for (auto it = srcs.rbegin(); it != srcs.rend(); it++) {
          Limb *src = *it;
          auto &key = *src;
          if (next_use.find(key) == next_use.end()) {
            src->set_dead(true);
          } else {
            src->set_next_use(next_use[key]);
          }
          next_use[key] = reverse_chain_instructions;
        }
        const std::vector<Limb *> &dests = instr->dests();
        for (auto it = dests.rbegin(); it != dests.rend(); it++) {
          Limb *dst = *it;
          auto &key = *dst;
          if (next_use.find(key) == next_use.end()) {
            if (!dst->is_output()) {
              dst->set_dead(true);
            } else {
              dst->set_next_use(-1);
            }
            continue;
          }
          dst->set_next_use(next_use.at(key));
          next_use.erase(key);
        }

        // If has polynomial srcs, polynomial dsts, create those too...
        const std::vector<Polynomial *> &polynomial_srcs =
            instr->polynomial_srcs();
        for (auto it = polynomial_srcs.rbegin(); it != polynomial_srcs.rend();
             it++) {
          Polynomial *src = *it;
          auto &key = *src;
          if (polynomial_next_use.find(key) == polynomial_next_use.end()) {
            src->set_dead(true);
          } else {
            src->set_next_use(polynomial_next_use[key]);
          }
          polynomial_next_use[key] = reverse_chain_instructions;
        }
        const std::vector<Polynomial *> &polynomial_dests =
            instr->polynomial_dests();
        for (auto it = polynomial_dests.rbegin(); it != polynomial_dests.rend();
             it++) {
          Polynomial *dst = *it;
          auto &key = *dst;
          if (polynomial_next_use.find(key) == polynomial_next_use.end()) {
            if (!dst->is_output()) {
              dst->set_dead(true);
            } else {
              dst->set_next_use(-1);
            }
            continue;
          }
          dst->set_next_use(polynomial_next_use.at(key));
          polynomial_next_use[key] = reverse_chain_instructions;
          // polynomial_next_use.erase(key);
        }
      }
    }
  }

  void create_limb_instructions(size_t partition) {
    std::vector<ChainIndexType> vec;
    top_sort_chains.push_back(std::move(vec));
    auto i = partition;
    for (auto j = 0; j < top_sort_partitions[i].size(); j++) {
      using OpCode = PolynomialInstruction::OpCode;
      auto idx = top_sort_partitions[i][j];
      auto &bn = BigNodes[idx];
      auto chains = expand_instructions_limbwise(bn);
      for (auto &c : chains) {
        top_sort_chains[i].push_back(c);
      }
    }
    top_sort_partitions[i].clear();
  }

  void verify_instructions_and_convert_terms_to_termshares(uint32_t partition) {
    uint16_t MAX_LIMBS = 64;
    auto i = partition;

    std::map<TermIndexType, std::vector<TermShareIndexType>> ts_created_at;
    std::map<TermShareIndexType, TermIndexType> ts_to_term;
    std::map<std::string, TermIndexType> evk_to_term;
    std::map<std::string, TermIndexType> evk_ext_to_term;
    std::map<std::string, TermIndexType> evk_par_to_term;
    auto convert_termshare_to_term_src = [&](Polynomial *src) {
      if (src->num_shares() == 0) {
        return;
      }
      auto &vec = ts_created_at.at(src->term_idx());
      std::set<TermShareIndexType> deps;
      for (auto &limb : src->shares()) {
        deps.insert(vec[limb]);
      }
      assert(src->shares().size() == 0 || deps.size() != 0);

      if (src->is_eval_key() && src->limbtype() != Term::LimbType::Spl) {
        std::stringstream symbolSS;
        symbolSS << src->symbol() << ":[";
        for (auto &share : src->shares()) {
          symbolSS << share << ",";
        }
        symbolSS << "]";
        auto symbol = symbolSS.str();

        if (src->limbtype() == Term::LimbType::Usp) {
          auto it = evk_to_term.find(symbol);
          if (it == evk_to_term.end()) {
            *src = make_copy_treg_from(src);
            evk_to_term[symbol] = src->term_idx();
          } else {
            *src = make_treg_with_term(src, it->second);
          }
        } else if (src->limbtype() == Term::LimbType::Ext) {
          auto it = evk_ext_to_term.find(symbol);
          if (it == evk_ext_to_term.end()) {
            *src = make_copy_treg_from(src);
            evk_ext_to_term[symbol] = src->term_idx();
          } else {
            *src = make_treg_with_term(src, it->second);
          }
        } else if (src->limbtype() == Term::LimbType::Par) {
          auto it = evk_par_to_term.find(symbol);
          if (it == evk_par_to_term.end()) {
            *src = make_copy_treg_from(src);
            evk_par_to_term[symbol] = src->term_idx();
          } else {
            *src = make_treg_with_term(src, it->second);
          }
        } else if (src->limbtype() == Term::LimbType::Ext_Split) {
          auto it = evk_par_to_term.find(symbol);
          if (it == evk_par_to_term.end()) {
            *src = make_copy_treg_from(src);
            evk_par_to_term[symbol] = src->term_idx();
          } else {
            *src = make_treg_with_term(src, it->second);
          }
        } else {
          assert(0);
        }
      } else if (deps.size() > 1) {
        assert(src->limbtype() == Term::LimbType::Spl ||
               src->limbtype() == Term::LimbType::Ext_Split);
      } else if (deps.size() == 1) {
        assert(deps.size() == 1);

        // TODO: Add a predicate here
        if (src->limbtype() != Term::LimbType::Spl &&
            src->limbtype() != Term::Ext_Split) {
          if (src->is_input()) {
            *src = make_copy_treg_from(src);
          } else {
            *src = make_treg_with_term(src, ts_to_term[*deps.begin()]);
          }
        }
      } else {
        assert(deps.size() == 0);
      }
    };

    auto convert_termshare_to_term_dest = [&](Polynomial *dst) {
      auto dst_idx = dst->term_idx();
      // TODO: Add a predicate here
      if (dst->limbtype() != Term::LimbType::Spl &&
          dst->limbtype() != Term::LimbType::Ext_Split) {
        auto old_ts_idx = dst->term_share_idx();
        *dst = make_copy_treg_from(dst);
        ts_to_term[old_ts_idx] = dst->term_idx();
      }
    };

    iterate_instructions_partition(
        i, [&](std::shared_ptr<BigNode> &bn,
               std::shared_ptr<PolynomialInstruction> &instr) {
          if (!instr->verify_operands()) {
            throw std::runtime_error(
                "Instruction Verification Failed for BN: " +
                std::to_string(bn->index()) + " instruction: " + instr->ppOp());
          }
          auto dests = instr->dests();
          for (auto &dst : dests) {
            auto dst_idx = dst->term_idx();
            if (ts_created_at.find(dst_idx) == ts_created_at.end()) {
              ts_created_at[dst_idx] =
                  std::vector<TermShareIndexType>(MAX_LIMBS, 0);
            }
            std::vector<uint64_t> &vec = ts_created_at.at(dst_idx);
            for (auto &share : dst->shares()) {
              vec[share] = dst->term_share_idx();
            }
          }
          auto srcs = instr->srcs();
          for (auto &src : srcs) {
            if (src->num_shares() == 0) {
              continue;
            }
            auto &vec = ts_created_at.at(src->term_idx());
            std::set<TermShareIndexType> deps;
            for (auto &limb : src->shares()) {
              deps.insert(vec[limb]);
            }
            assert(src->shares().size() == 0 || deps.size() != 0);
            if (deps.size() > 1) {
              assert(src->limbtype() == Term::LimbType::Spl ||
                     src->is_eval_key() ||
                     src->limbtype() == Term::LimbType::Ext_Split);
            }
          }

          for (auto &dst : dests) {
            convert_termshare_to_term_dest(dst);
          }
          for (auto &src : srcs) {
            convert_termshare_to_term_src(src);
          }
        });
  }

  void set_temporary_values(uint32_t partition) {
    std::map<TermIndexType, BigNodeIndexType> created_at_bn;
    auto handle_dests = [&](BigNodeIndexType bn_index,
                            std::vector<Polynomial *> &&dests) {
      for (auto &dst : dests) {
        auto dst_idx = dst->term_idx();
        created_at_bn[dst_idx] = bn_index;
        if (dst->is_output()) {
          dst->set_temp(false);
        }
      }
    };
    auto handle_srcs = [&](BigNodeIndexType bn_index,
                           std::vector<Polynomial *> &&srcs) {
      for (auto &src : srcs) {
        auto src_idx = src->term_idx();
        if (bn_index != created_at_bn[src_idx]) {
          src->set_temp(false);
        }
      }
    };
    iterate_instructions_partition(
        partition, [&](std::shared_ptr<BigNode> &bn,
                       std::shared_ptr<PolynomialInstruction> &instr) {
          auto bn_index = bn->index();
          handle_dests(bn_index, instr->dests());
          handle_srcs(bn_index, instr->srcs());
        });
  }

  void compile() {
    std::cout << "Compilation Starting" << std::endl;
    uint64_t instructions = 0;

    bool PRINT_GRAPH = get_print_graph();

    if (PRINT_GRAPH) {
      print_graph_simple2();
    }
    // topologically_sort_bignodes();
    topologically_sort_bignodes2();

    insert_join_global_instructions();

    split_keyswitch_pipeline_bignodes();

    set_instruction_sync_ids();

    if (PRINT_GRAPH) {
      print_graph(top_sort, "graph.dot");
    }

    create_partitions();

    for (auto i = 0; i < num_partitions; i++) {
      set_instruction_limbs(i);
      if (PRINT_GRAPH) {
        std::string file_name =
            "graph_split_before" + std::to_string(i) + ".dot";
        print_graph(top_sort_partitions[i], file_name);
      }
      verify_instructions_and_convert_terms_to_termshares(i);
      set_temporary_values(i);
      if (PRINT_GRAPH) {
        std::string file_name =
            "graph_split_before" + std::to_string(i) + ".dot";
        print_graph(top_sort_partitions[i], file_name);
      }
      create_limb_instructions(i);
      do_next_use_analysis(i);
      if (PRINT_GRAPH) {
        print_chains(i);
      }
      allocate_registers(i);
    }
    top_sort_partitions.clear();
    BigNodes.clear();
    write_program_inputs();
    std::cout << "Compilation Finished" << std::endl;
  }

  void free(const Frontend::Term::Ptr &term) {}
};
} // namespace Backend
} // namespace Cinnamon
// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "cinnamon/backend/polynomial_instruction.h"

namespace Cinnamon {
namespace Backend {

uint64_t merge_count = 0;
uint64_t block_size = 1000;

using BigNodeIndexType = uint64_t;
using GroupIndexType = uint64_t;
class Group;
std::vector<std::shared_ptr<Group>> Groups;
class Group {
  GroupIndexType index_;
  std::set<GroupIndexType> children_;
  std::set<GroupIndexType> parents_;
  BigNodeIndexType bn_index_;
  std::set<GroupIndexType> equivalents_;

  uint64_t merge_count_parents_ = 0;
  uint64_t merge_count_children_ = 0;

public:
  Group(GroupIndexType index, BigNodeIndexType bn_index)
      : index_(index), bn_index_(bn_index) {
    equivalents_.insert(index_);
    merge_count_children_ = merge_count;
    merge_count_parents_ = merge_count;
  };
  decltype(index_) group_index() { return index_; }

  auto bn_index() { return bn_index_; }
  void add_parent(std::shared_ptr<Group> &parent) {

    if (Groups[parent->index_]->index_ == Groups[index_]->index_) {
      return;
    }

    decltype(parents_) new_parents(parent->parents_);
    new_parents.insert(parent->index_);
    parents_.insert(new_parents.begin(), new_parents.end());

    // refresh_children();
    for (auto &child : children_) {
      Groups[child]->parents_.insert(new_parents.begin(), new_parents.end());
    }
  }

  void add_child(std::shared_ptr<Group> &child) {

    if (Groups[child->index_]->index_ == Groups[index_]->index_) {
      return;
    }

    decltype(children_) new_children(child->children_);
    new_children.insert(child->index_);
    children_.insert(new_children.begin(), new_children.end());

    // refresh_parents();
    for (auto &parent : parents_) {
      Groups[parent]->children_.insert(new_children.begin(),
                                       new_children.end());
    }
    child->add_parent(Groups[index_]);
  }

  void refresh_children() {
    if (merge_count_children_ == merge_count) {
      return;
    }
    decltype(children_) children_refresh;
    for (auto &child : children_) {
      children_refresh.insert(Groups[child]->index_);
    }
    children_ = children_refresh;
    merge_count_children_ = merge_count;
  }

  void refresh_parents() {
    if (merge_count_parents_ == merge_count) {
      return;
    }
    decltype(parents_) parents_refresh;
    for (auto &parent : parents_) {
      parents_refresh.insert(Groups[parent]->index_);
    }
    parents_ = std::move(parents_refresh);
    merge_count_parents_ = merge_count;
  }

  auto &children() { return children_; }
  auto &parents() { return parents_; }
  auto &equivalents() { return equivalents_; }

  void delete_parent_noref(GroupIndexType idx) { parents_.erase(idx); }
};

using SplitType = std::vector<std::set<uint16_t>>;
class BigNode;
std::vector<std::shared_ptr<BigNode>> BigNodes;
class BigNode {

  BigNodeIndexType index_;
  std::vector<std::shared_ptr<PolynomialInstruction>> instructions_;
  GroupIndexType group_idx_;
  uint8_t ks_pl_no_;
  BigNodeIndexType
      next_bignode_in_group_index_; // Index of the Next Bignode in the group,
                                    // if any otherwise 0
  BigNodeIndexType ks_pl_head_idx_;
  BigNodeIndexType ks_pl_tail_idx_;
  uint64_t ks_pl_idx_;

  bool dont_split_modular_;
  SplitType split_up_;
  SplitType split_down_;

  std::set<TermIndexType> inputs_;
  std::set<TermIndexType> outputs_;

  std::set<BigNodeIndexType> children_;
  std::set<BigNodeIndexType> parents_;
  std::set<BigNodeIndexType> equivalents_;

  uint64_t merge_count_parents_ = 0;
  uint64_t merge_count_children_ = 0;

  uint8_t partition_size_ = 0;
  uint8_t partition_id_ = 0;

  void add_parent(std::shared_ptr<BigNode> parent) {
    Groups[group_idx_]->add_parent(Groups[parent->group_idx_]);
    BigNodes[index_]->do_add_parent(BigNodes[parent->index_]);
  }

  void do_add_parent(std::shared_ptr<BigNode> &parent) {

    if (BigNodes[parent->index_]->index_ == BigNodes[index_]->index_) {
      return;
    }

    decltype(parents_) new_parents(parent->parents_);
    new_parents.insert(parent->index_);
    parents_.insert(new_parents.begin(), new_parents.end());

    refresh_children();
    for (auto &child : children_) {
      BigNodes[child]->parents_.insert(new_parents.begin(), new_parents.end());
    }
  }

  void do_add_child(std::shared_ptr<BigNode> &child) {

    if (BigNodes[child->index_]->index_ == BigNodes[index_]->index_) {
      return;
    }

    decltype(children_) new_children(child->children_);
    new_children.insert(child->index_);
    children_.insert(new_children.begin(), new_children.end());

    // refresh_parents();
    for (auto &parent : parents_) {
      BigNodes[parent]->children_.insert(new_children.begin(),
                                         new_children.end());
    }
    child->add_parent(BigNodes[index_]);
  }

  void refresh_children() {
    if (merge_count_children_ == merge_count) {
      return;
    }
    decltype(children_) children_refresh;
    for (auto &child : children_) {
      children_refresh.insert(BigNodes[child]->index_);
    }
    children_ = std::move(children_refresh);
    merge_count_children_ = merge_count;
  }

  void refresh_parents() {
    if (merge_count_parents_ == merge_count) {
      return;
    }
    decltype(parents_) parents_refresh;
    for (auto &parent : parents_) {
      parents_refresh.insert(BigNodes[parent]->index_);
    }
    parents_ = std::move(parents_refresh);
    merge_count_parents_ = merge_count;
  }

public:
  BigNode(uint64_t index, GroupIndexType group_idx, uint8_t partition_size,
          uint8_t partition_id)
      : index_(index), group_idx_(group_idx), ks_pl_no_(0),
        next_bignode_in_group_index_(0), ks_pl_head_idx_(0), ks_pl_tail_idx_(0),
        ks_pl_idx_(0), dont_split_modular_(false),
        partition_size_(partition_size), partition_id_(partition_id) {
    equivalents_.insert(index_);
    merge_count_children_ = merge_count;
    merge_count_parents_ = merge_count;
  };

  decltype(group_idx_) group_index() { return group_idx_; }

  void set_split_up(const SplitType &split) { split_up_ = split; }
  void set_split_down(SplitType &split) { split_down_ = split; }
  void set_dont_split_modular(bool val) { dont_split_modular_ = val; }

  void set_next_bignode_in_group(const std::shared_ptr<BigNode> &next) {
    BigNodes[index_]->next_bignode_in_group_index_ = next->index_;
  }

  void set_ks_pl_head_idx(const std::shared_ptr<BigNode> &head) {
    BigNodes[index_]->ks_pl_head_idx_ = head->index_;
  }

  void set_ks_pl_tail_idx(const std::shared_ptr<BigNode> &tail) {
    BigNodes[index_]->ks_pl_tail_idx_ = tail->index_;
  }

  void set_ks_pl_idx(decltype(ks_pl_idx_) idx) { ks_pl_idx_ = idx; }

  BigNodeIndexType next_bignode_in_group_index() {
    return next_bignode_in_group_index_;
  }

  void set_ks_pl_no(decltype(ks_pl_no_) no) { ks_pl_no_ = no; }

  decltype(ks_pl_no_) ks_pl_no() { return ks_pl_no_; }
  decltype(ks_pl_head_idx_) ks_pl_head_idx() { return ks_pl_head_idx_; }
  decltype(ks_pl_tail_idx_) ks_pl_tail_idx() { return ks_pl_tail_idx_; }

  decltype(ks_pl_idx_) ks_pl_idx() { return ks_pl_idx_; }

  auto partition_size() { return partition_size_; }
  auto partition_id() { return partition_id_; }

  void push_join_instruction(std::shared_ptr<PolynomialInstruction> &&instr) {
    instructions_.push_back(instr);
  }
  void push_instruction(std::shared_ptr<PolynomialInstruction> &&instr) {

    using OpCode = PolynomialInstruction::OpCode;

    instructions_.push_back(instr);

    for (auto &src : instr->srcs()) {
      // If this value comes from a different bignode
      // then add us as a child to it
      auto sidx = src->bignode_index();
      if (sidx != index_) {
        auto bn_src = BigNodes[sidx];
        bn_src->add_child(BigNodes[index_]);
      }
    }
  }

  void delete_parent_noref_group(GroupIndexType idx) {
    Groups[group_idx_]->delete_parent_noref(idx);
  }

  // automatically add
  void add_child(std::shared_ptr<BigNode> child) {
    if (BigNodes[index_]->block_idx() != child->block_idx()) {
      return;
    }
    Groups[group_idx_]->add_child(Groups[child->group_idx_]);
    BigNodes[index_]->do_add_child(BigNodes[child->index_]);
  }

  auto &parents() { return parents_; }

  auto &children() { return children_; }

  auto &equivalents() { return equivalents_; }

  auto &parents_group() { return Groups[group_idx_]->parents(); }

  auto &children_group() { return Groups[group_idx_]->children(); }

  auto &equivalents_group() { return Groups[group_idx_]->equivalents(); }

  void merge_equivalents(std::shared_ptr<BigNode> &other) {
    auto &other_equivalents = other->equivalents_;
    equivalents_.insert(other_equivalents.begin(), other_equivalents.end());
  }

  void refresh_children_group() {
    Groups[group_idx_]->refresh_children();
    return;
  }

  void refresh_parents_group() {
    Groups[group_idx_]->refresh_parents();
    return;
  }

  BigNodeIndexType index() { return index_; }

  const std::vector<std::shared_ptr<PolynomialInstruction>> &
  instructions() const {
    return instructions_;
  }

  std::vector<std::shared_ptr<PolynomialInstruction>> &instructions() {
    return instructions_;
  }

  bool empty() const { return instructions_.empty(); }

  const SplitType &split_up() { return split_up_; }

  const SplitType &split_down() { return split_down_; }

  bool dont_split_modular() const { return dont_split_modular_; }

  void add_input(const TermIndexType &inp) { inputs_.insert(inp); }
  void add_output(const TermIndexType out) { outputs_.insert(out); }

  void set_inputs(const decltype(inputs_) &inp) { inputs_ = inp; }

  void set_outputs(const decltype(outputs_) &out) { outputs_ = out; }

  const decltype(inputs_) &inputs() { return inputs_; }

  const decltype(outputs_) &outputs() { return outputs_; }

  uint64_t block_idx() const {
    return Groups[group_idx_]->group_index() / block_size;
  }
};

} // namespace Backend
} // namespace Cinnamon
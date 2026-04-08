// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include "fmt/core.h"
#include <memory>
#include <sstream>
#include <vector>

#include "cinnamon/backend/terms.h"

namespace Cinnamon {
namespace Backend {

class Limb {
  uint16_t level_;
  uint16_t limb_idx_;
  std::shared_ptr<TermShare> term_share_;
  uint64_t next_use_;
  uint64_t last_use_;
  bool dead_;

public:
  Limb(){};
  Limb(std::shared_ptr<TermShare> &term_share, uint16_t level,
       uint16_t limb_idx)
      : term_share_(term_share), level_(level), limb_idx_(limb_idx),
        next_use_(0), last_use_(0), dead_(false) {}
  Limb(const Polynomial *poly, uint16_t limb_idx)
      : term_share_(poly->term_share()), level_(poly->level()),
        limb_idx_(limb_idx), next_use_(0), dead_(false) {}
  Limb(const Polynomial &poly, uint16_t limb_idx)
      : term_share_(poly.term_share()), level_(poly.level()),
        limb_idx_(limb_idx), next_use_(0), dead_(false) {}

  void set_limb(const uint16_t &limb_idx) { limb_idx_ = limb_idx; }

  void set_dead(bool val) { dead_ = val; }
  void set_as_dead() { set_dead(true); }

  bool is_dead() const { return dead_; }

  void set_next_use(const uint64_t next_use) { next_use_ = next_use; }

  uint64_t next_use() const { return next_use_; }

  void set_last_use(const uint64_t last_use) { last_use_ = last_use; }

  uint64_t last_use() const { return last_use_; }

  uint16_t limb_idx() const { return limb_idx_; }

  uint32_t level() const { return level_; }

  std::shared_ptr<TermShare> term_share() { return term_share_; }

  const std::shared_ptr<TermShare> &term_share() const { return term_share_; }

  TermIndexType term_share_idx() { return term_share_->term_share_idx(); }

  std::shared_ptr<Term> term() { return term_share_->term(); }

  const std::shared_ptr<Term> &term() const { return term_share_->term(); }

  TermIndexType term_idx() const { return term_share_->term_idx(); }

  void set_bignode_idx(uint64_t idx) { term_share_->set_bignode_idx(idx); }

  uint64_t bignode_index() const { return term_share_->bignode_index(); }

  bool is_bcor() const { return term_share_->is_bcor(); }

  bool is_input() const { return term_share_->is_input(); }

  bool is_plaintext() const { return term_share_->is_plaintext(); }

  bool is_scalar() const { return term_share_->is_scalar(); }

  bool is_receive() const { return term_share_->is_receive(); }

  bool is_send() const { return term_share_->is_send(); }

  void set_output(bool val) const { term_share_->set_output(val); }

  bool is_output() const { return term_share_->is_output(); }

  bool is_eval_key() const { return term_share_->is_eval_key(); }

  bool eval_key_poly_num() const { return term_share_->eval_key_poly_num(); }

  void set_temp(bool val) { term_share_->set_temp(val); }

  void set_join_reqd(bool val) { term_share_->set_join_reqd(val); }

  bool join_reqd() const { return term_share_->join_reqd(); }

  void set_join_here(bool val) { term_share_->set_join_here(val); }

  bool join_here() const { return term_share_->join_here(); }

  void set_unmergeable_downward(bool val) {
    term_share_->set_unmergeable_downward(val);
  }

  bool unmergeable_downward() { return term_share_->unmergeable_downward(); }

  void set_unmergeable_upward(bool val) {
    term_share_->set_unmergeable_upward(val);
  }

  bool unmergeable_upward() { return term_share_->unmergeable_upward(); }

  void set_limbtype(Term::LimbType type) { term_share_->set_limbtype(type); }

  Term::LimbType limbtype() const { return term_share_->limbtype(); }

  friend std::ostream &operator<<(std::ostream &s, const Limb &poly);
};

std::ostream &operator<<(std::ostream &s, const Cinnamon::Backend::Limb &limb) {

  std::string reg_name;
  auto term = limb.term();
  s << term->name() << "(" + std::to_string(limb.limb_idx_) << ")";

  if (limb.dead_) {
    s << "[X]";
  }

  return s;
}

struct LimbHash {
  std::size_t operator()(const Limb &limb) const {
    return std::hash<TermIndexType>()(limb.term_idx()) ^
           std::hash<LimbIndexType>()(limb.limb_idx());
  }
};

struct LimbCompare {
  bool operator()(const Limb &lhs, const Limb &rhs) const {
    return lhs.term_idx() == rhs.term_idx() && lhs.limb_idx() == rhs.limb_idx();
  }
};

template <typename T>
using LimbMap = std::unordered_map<Limb, T, LimbHash, LimbCompare>;

using LimbSet = std::unordered_set<Limb, LimbHash, LimbCompare>;
} // namespace Backend
} // namespace Cinnamon

template <> struct fmt::formatter<Cinnamon::Backend::Limb> {

  constexpr auto parse(format_parse_context &ctx)
      -> format_parse_context::iterator {

    // Parse the presentation format and store it in the formatter:
    auto it = ctx.begin(), end = ctx.end();

    // Check if reached the end of the range:
    if (it != end && *it != '}') ctx.on_error("invalid format");

    // Return an iterator past the end of the parsed range:
    return it;
  }

  auto format(const Cinnamon::Backend::Limb &p, format_context &ctx) const
      -> format_context::iterator {
    std::stringstream s;
    s << p;
    return fmt::format_to(ctx.out(), "{}", s.str());
  }
};
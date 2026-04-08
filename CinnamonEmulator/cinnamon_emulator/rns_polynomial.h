// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include "limb.h"

namespace Cinnamon::Emulator {
class RnsPolynomial {

public:
  RnsPolynomial() {}

  void clear() { limb_index_map_.clear(); }

  void write_limb(const std::shared_ptr<Limb> &limb,
                  std::uint64_t rns_base_id) {
    limb_index_map_[rns_base_id] = limb;
  }

  const std::shared_ptr<Limb> &at(std::uint64_t rns_base_id) const {
    return limb_index_map_.at(rns_base_id);
  }

  const auto &limb_index_map() const { return limb_index_map_; }

  auto num_limbs() const { return limb_index_map_.size(); }

  auto empty() const { return limb_index_map_.empty(); }

  auto begin() { return limb_index_map_.begin(); }
  auto begin() const { return limb_index_map_.begin(); }
  auto end() { return limb_index_map_.end(); }
  auto end() const { return limb_index_map_.end(); }

private:
  std::map<std::uint64_t, std::shared_ptr<Limb>> limb_index_map_;
};

using RnsPolynomialPtr = std::shared_ptr<RnsPolynomial>;

} // namespace Cinnamon::Emulator
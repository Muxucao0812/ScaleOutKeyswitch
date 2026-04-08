// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include "context.h"
#include "config.h"
#include "seal/util/ntt.h"

namespace Cinnamon::Emulator {
Context::Context(std::size_t slots, const std::vector<std::uint64_t> &rns_bases)
    : slots_(slots), num_rns_bases_(rns_bases.size()) {

#ifdef CINNAMON_EUMLATOR_DEBUG
  if (slots > MAX_SLOTS) {
    throw std::invalid_argument("slots Must be less than MAX_SLOTS");
  }
  if (slots < MIN_SLOTS) {
    throw std::invalid_argument("slots Must be less than MIN_SLOTS");
  }

  if ((slots & (slots - 1)) != 0) {
    throw std::invalid_argument("slots Must be a power of 2");
  }
#endif

  n_ = slots << 1;

  for (size_t i = 0; i < rns_bases.size(); i++) {
    rns_bases_.push_back(seal::Modulus(rns_bases.at(i)));
  }
  pool_ = seal::MemoryManager::GetPool();
  Util::CreateNTTTables(log2(n_), rns_bases_, ntt_tables_, pool_);
  seal::util::CreateNTTTables(log2(n_), rns_bases_, seal_ntt_tables_, pool_);
  galois_tool_ = seal::util::allocate<Util::GaloisTool>(pool_, log2(n_), pool_);
}
} // namespace Cinnamon::Emulator
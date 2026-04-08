// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <seal/memorymanager.h>
#include <seal/seal.h>
#include <seal/util/mempool.h>
#include <seal/util/rns.h>

#include <vector>

#include "config.h"
#include "galois.h"
#include "ntt.h"

namespace Cinnamon::Emulator {
constexpr std::uint64_t MAX_SLOTS = 32768;
constexpr std::uint64_t MIN_SLOTS = 4;
class Context {
public:
  Context() = delete;

  Context(const Context &copy) = delete;

  Context(Context &&move) = default;

  Context &operator=(Context &&move) = default;

  Context(std::size_t slots, const std::vector<std::uint64_t> &rns_bases);

  inline std::size_t slots() const { return slots_; }

  inline std::size_t n() const { return n_; }

  inline std::size_t num_rns_bases() const { return num_rns_bases_; }

  inline const auto *ntt_tables() const { return ntt_tables_.get(); }

  inline const seal::util::NTTTables *seal_ntt_tables() const {
    return seal_ntt_tables_.get();
  }

  inline const std::vector<seal::Modulus> &rns_bases() const {
    return rns_bases_;
  }

  inline const seal::Modulus &get_rns_modulus(std::size_t rns_base_id) const {
    return rns_bases_.at(rns_base_id);
  }

  inline auto *galois_tool() const noexcept { return galois_tool_.get(); }

  std::uint32_t rns_base_bit_count(std::uint64_t num_bases) const {
    // TODO: Fix this...
    return num_bases * 28;
  }

private:
  std::size_t slots_;
  std::size_t n_;
  std::vector<seal::Modulus> rns_bases_;
  std::uint64_t num_rns_bases_;
  seal::MemoryPoolHandle pool_ = seal::MemoryManager::GetPool();
  seal::util::Pointer<seal::util::NTTTables> seal_ntt_tables_;
  seal::util::Pointer<Util::NTTTables<Limb::Element_t>> ntt_tables_;
  seal::util::Pointer<Util::GaloisTool> galois_tool_;
};
} // namespace Cinnamon::Emulator
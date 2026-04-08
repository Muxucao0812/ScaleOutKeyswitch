// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include "config.h"
#include "util.h"

namespace Cinnamon::Emulator {
namespace Util {
class GaloisTool {
public:
  GaloisTool(int coeff_count_power, seal::MemoryPoolHandle pool)
      : pool_(std::move(pool)) {
    if (!pool_) {
      throw std::invalid_argument("pool is uninitialized");
    }

    initialize(coeff_count_power);
  }

  CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
  void apply_galois_ntt(const T *operand, std::size_t size,
                        std::uint32_t galois_elt, T *result) const {
#ifdef CINNAMON_EUMLATOR_DEBUG
    if (!operand) {
      throw std::invalid_argument("operand");
    }
    if (!result) {
      throw std::invalid_argument("result");
    }
    if (operand == result) {
      throw std::invalid_argument(
          "result cannot point to the same value as operand");
    }
    // Verify coprime conditions.
    if (!(galois_elt & 1) ||
        (galois_elt >= 2 * (uint64_t(1) << coeff_count_power_))) {
      throw std::invalid_argument("Galois element is not valid");
    }
#endif
    generate_table_ntt(galois_elt,
                       permutation_tables_[GetIndexFromElt(galois_elt)]);
    auto table = iter(permutation_tables_[GetIndexFromElt(galois_elt)]);

    // Perform permutation.
    SEAL_ITERATE(iter(table, result), coeff_count_,
                 [&](auto I) { std::get<1>(I) = operand[std::get<0>(I)]; });
  }

  auto get_galois_table(std::uint32_t galois_elt) {
    generate_table_ntt(galois_elt,
                       permutation_tables_[GetIndexFromElt(galois_elt)]);
    return iter(permutation_tables_[GetIndexFromElt(galois_elt)]);
  }

  /**
  Compute the Galois element corresponding to a given rotation step.
  */
  SEAL_NODISCARD std::uint32_t get_elt_from_step(int step) const;

  /**
  Compute the index in the range of 0 to (coeff_count_ - 1) of a given Galois
  element.
  */
  SEAL_NODISCARD static inline std::size_t
  GetIndexFromElt(std::uint32_t galois_elt) {
#ifdef CINNAMON_EUMLATOR_DEBUG
    if (!(galois_elt & 1)) {
      throw std::invalid_argument("galois_elt is not valid");
    }
#endif
    return seal::util::safe_cast<std::size_t>((galois_elt - 1) >> 1);
  }

private:
  GaloisTool(const GaloisTool &copy) = delete;

  GaloisTool(GaloisTool &&source) = delete;

  GaloisTool &operator=(const GaloisTool &assign) = delete;

  GaloisTool &operator=(GaloisTool &&assign) = delete;

  void initialize(int coeff_count_power);

  void generate_table_ntt(std::uint32_t galois_elt,
                          seal::util::Pointer<std::uint32_t> &result) const;

  seal::MemoryPoolHandle pool_;

  int coeff_count_power_ = 0;

  std::size_t coeff_count_ = 0;

  static constexpr std::uint32_t generator_ = 5;

  mutable seal::util::Pointer<seal::util::Pointer<std::uint32_t>>
      permutation_tables_;

  mutable seal::util::ReaderWriterLocker permutation_tables_locker_;
};
} // namespace Util
} // namespace Cinnamon::Emulator

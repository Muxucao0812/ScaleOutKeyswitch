// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include "galois.h"
#include "config.h"

namespace Cinnamon::Emulator {
namespace Util {
// Required for C++14 compliance: static constexpr member variables are not
// necessarily inlined so need to ensure symbol is created.
constexpr uint32_t GaloisTool::generator_;

void GaloisTool::generate_table_ntt(
    uint32_t galois_elt, seal::util::Pointer<uint32_t> &result) const {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (!(galois_elt & 1) ||
      (galois_elt >= 2 * (uint64_t(1) << coeff_count_power_))) {
    throw std::invalid_argument("Galois element is not valid");
  }
#endif
  seal::util::ReaderLock reader_lock(permutation_tables_locker_.acquire_read());
  if (result) {
    return;
  }
  reader_lock.unlock();

  auto temp(seal::util::allocate<uint32_t>(coeff_count_, pool_));
  auto temp_ptr = temp.get();

  uint32_t coeff_count_minus_one =
      seal::util::safe_cast<uint32_t>(coeff_count_) - 1;
  for (size_t i = coeff_count_; i < coeff_count_ << 1; i++) {
    uint32_t reversed = seal::util::reverse_bits<uint32_t>(
        seal::util::safe_cast<uint32_t>(i), coeff_count_power_ + 1);
    uint64_t index_raw =
        (static_cast<uint64_t>(galois_elt) * static_cast<uint64_t>(reversed)) >>
        1;
    index_raw &= static_cast<uint64_t>(coeff_count_minus_one);
    *temp_ptr++ = seal::util::reverse_bits<uint32_t>(
        static_cast<uint32_t>(index_raw), coeff_count_power_);
  }

  seal::util::WriterLock writer_lock(
      permutation_tables_locker_.acquire_write());
  if (result) {
    return;
  }
  result.acquire(std::move(temp));
}

uint32_t GaloisTool::get_elt_from_step(int step) const {
  uint32_t n = seal::util::safe_cast<uint32_t>(coeff_count_);
  uint32_t m32 = seal::util::mul_safe(n, uint32_t(2));
  uint64_t m = static_cast<uint64_t>(m32);

  if (step == 0) {
    return static_cast<uint32_t>(m - 1);
  } else {
    // Extract sign of steps. When steps is positive, the rotation
    // is to the left; when steps is negative, it is to the right.
    bool sign = step < 0;
    uint32_t pos_step = seal::util::safe_cast<uint32_t>(abs(step));

    if (pos_step >= (n >> 1)) {
      throw std::invalid_argument("step count too large");
    }

    pos_step &= m32 - 1;
    if (sign) {
      step = seal::util::safe_cast<int>(n >> 1) -
             seal::util::safe_cast<int>(pos_step);
    } else {
      step = seal::util::safe_cast<int>(pos_step);
    }

    // Construct Galois element for row rotation
    uint64_t gen = static_cast<uint64_t>(generator_);
    uint64_t galois_elt = 1;
    while (step--) {
      galois_elt *= gen;
      galois_elt &= m - 1;
    }
    return static_cast<uint32_t>(galois_elt);
  }
}

void GaloisTool::initialize(int coeff_count_power) {

  coeff_count_power_ = coeff_count_power;
  coeff_count_ = size_t(1) << coeff_count_power_;

  // Capacity for coeff_count_ number of tables
  permutation_tables_ =
      seal::util::allocate<seal::util::Pointer<uint32_t>>(coeff_count_, pool_);
}

} // namespace Util
} // namespace Cinnamon::Emulator

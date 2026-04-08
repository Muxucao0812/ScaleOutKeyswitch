#ifndef CINNAMON_HLS_MONTGOMERY_HPP_
#define CINNAMON_HLS_MONTGOMERY_HPP_

#include <cstdint>

#include "cinnamon_hls/math_utils.hpp"

namespace cinnamon_hls {

inline std::uint64_t montgomery_r(std::uint32_t one_iter_log2,
                                  std::uint32_t num_iterations) {
  const std::uint32_t shift = one_iter_log2 * num_iterations;
  if (shift >= 63) {
    return 0;
  }
  return 1ULL << shift;
}

inline std::uint64_t montgomery_reduce_ntt_friendly(std::uint64_t a,
                                                    std::uint64_t q,
                                                    std::uint32_t one_iter_log2,
                                                    std::uint32_t num_iterations) {
  const std::uint64_t r = montgomery_r(one_iter_log2, num_iterations);
  const std::uint64_t r_mod_q = r % q;
  const std::uint64_t r_inv = mod_inv_u64(r_mod_q, q);
  return mod_mul_u64(a % q, r_inv, q);
}

inline std::uint64_t montgomery_multiply_ntt_friendly(
    std::uint64_t a, std::uint64_t b, std::uint64_t q,
    std::uint32_t one_iter_log2, std::uint32_t num_iterations) {
  const std::uint64_t product = mod_mul_u64(a % q, b % q, q);
  return montgomery_reduce_ntt_friendly(product, q, one_iter_log2,
                                        num_iterations);
}

inline std::uint64_t convert_to_montgomery(std::uint64_t x, std::uint64_t q,
                                           std::uint32_t one_iter_log2,
                                           std::uint32_t num_iterations) {
  const std::uint64_t r = montgomery_r(one_iter_log2, num_iterations);
  return mod_mul_u64(x % q, r % q, q);
}

inline std::uint64_t convert_from_montgomery(std::uint64_t y, std::uint64_t q,
                                             std::uint32_t one_iter_log2,
                                             std::uint32_t num_iterations) {
  return montgomery_reduce_ntt_friendly(y, q, one_iter_log2, num_iterations);
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_MONTGOMERY_HPP_

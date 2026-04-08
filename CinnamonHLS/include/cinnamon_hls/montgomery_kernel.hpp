#ifndef CINNAMON_HLS_MONTGOMERY_KERNEL_HPP_
#define CINNAMON_HLS_MONTGOMERY_KERNEL_HPP_

#include <cstdint>

#include "cinnamon_hls/montgomery.hpp"

namespace cinnamon_hls {

enum MontgomeryKernelOpcode : std::uint32_t {
  kMontgomeryReduce = 0,
  kMontgomeryMultiply = 1,
  kMontgomeryToDomain = 2,
  kMontgomeryFromDomain = 3,
};

inline std::uint64_t montgomery_kernel_apply(std::uint32_t opcode,
                                             std::uint64_t a,
                                             std::uint64_t b,
                                             std::uint64_t q,
                                             std::uint32_t one_iter_log2,
                                             std::uint32_t num_iterations) {
  switch (opcode) {
    case kMontgomeryReduce:
      return montgomery_reduce_ntt_friendly(a, q, one_iter_log2, num_iterations);
    case kMontgomeryMultiply:
      return montgomery_multiply_ntt_friendly(a, b, q, one_iter_log2, num_iterations);
    case kMontgomeryToDomain:
      return convert_to_montgomery(a, q, one_iter_log2, num_iterations);
    case kMontgomeryFromDomain:
      return convert_from_montgomery(a, q, one_iter_log2, num_iterations);
    default:
      return 0;
  }
}

inline void montgomery_core(const std::uint64_t *a,
                            const std::uint64_t *b,
                            std::uint64_t *out,
                            std::uint32_t count,
                            std::uint64_t q,
                            std::uint32_t opcode,
                            std::uint32_t one_iter_log2,
                            std::uint32_t num_iterations) {
  for (std::uint32_t i = 0; i < count; ++i) {
#pragma HLS PIPELINE II = 1
    const std::uint64_t lhs = a ? a[i] : 0ULL;
    const std::uint64_t rhs = b ? b[i] : 0ULL;
    out[i] = montgomery_kernel_apply(opcode, lhs, rhs, q, one_iter_log2,
                                     num_iterations);
  }
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_MONTGOMERY_KERNEL_HPP_

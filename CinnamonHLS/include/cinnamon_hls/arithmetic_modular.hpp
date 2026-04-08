#ifndef CINNAMON_HLS_ARITHMETIC_MODULAR_HPP_
#define CINNAMON_HLS_ARITHMETIC_MODULAR_HPP_

#include <cstdint>

namespace cinnamon_hls {

inline std::uint64_t modular_add(std::uint64_t a, std::uint64_t b,
                                 std::uint64_t mod) {
  const std::uint64_t sum = a + b;
  return (sum >= mod) ? (sum - mod) : sum;
}

inline std::uint64_t modular_sub(std::uint64_t a, std::uint64_t b,
                                 std::uint64_t mod) {
  return (a >= b) ? (a - b) : (a + (mod - b));
}

inline std::uint64_t multiplier_truncated(std::uint64_t a, std::uint64_t b,
                                          unsigned width_bits) {
  __uint128_t product = static_cast<__uint128_t>(a) * b;
  if (width_bits >= 64) {
    return static_cast<std::uint64_t>(product);
  }
  const std::uint64_t mask = (1ULL << width_bits) - 1ULL;
  return static_cast<std::uint64_t>(product) & mask;
}

inline std::uint64_t modular_mul(std::uint64_t a, std::uint64_t b,
                                 std::uint64_t mod, unsigned width_bits) {
  std::uint64_t acc = 0;
  std::uint64_t running = a % mod;
  for (unsigned i = 0; i < width_bits; ++i) {
    if ((b >> i) & 1ULL) {
      acc = modular_add(acc, running, mod);
    }
    running = modular_add(running, running, mod);
  }
  return acc;
}

inline std::uint64_t barrett_reduce(std::uint64_t a, std::uint64_t mod,
                                    std::uint64_t m, std::uint64_t k) {
  const std::uint64_t q = (a * m) >> k;
  std::uint64_t r = a - q * mod;
  if (r >= mod) {
    r -= mod;
  }
  return r;
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_ARITHMETIC_MODULAR_HPP_

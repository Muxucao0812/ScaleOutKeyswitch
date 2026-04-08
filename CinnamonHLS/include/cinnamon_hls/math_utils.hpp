#ifndef CINNAMON_HLS_MATH_UTILS_HPP_
#define CINNAMON_HLS_MATH_UTILS_HPP_

#include <cstdint>

namespace cinnamon_hls {

inline std::uint64_t mod_mul_u64(std::uint64_t a, std::uint64_t b,
                                 std::uint64_t mod) {
  if (mod == 0) {
    return 0;
  }
  __uint128_t product = static_cast<__uint128_t>(a) * b;
  return static_cast<std::uint64_t>(product % mod);
}

inline std::uint64_t mod_pow_u64(std::uint64_t base, std::uint64_t exp,
                                 std::uint64_t mod) {
  if (mod == 0) {
    return 0;
  }
  std::uint64_t result = 1 % mod;
  base %= mod;
  while (exp != 0) {
    if (exp & 1ULL) {
      result = mod_mul_u64(result, base, mod);
    }
    base = mod_mul_u64(base, base, mod);
    exp >>= 1U;
  }
  return result;
}

inline std::uint64_t mod_inv_u64(std::uint64_t a, std::uint64_t mod) {
  if (mod == 0) {
    return 0;
  }

  std::int64_t t = 0;
  std::int64_t new_t = 1;
  std::int64_t r = static_cast<std::int64_t>(mod);
  std::int64_t new_r = static_cast<std::int64_t>(a % mod);

  while (new_r != 0) {
    const std::int64_t q = r / new_r;
    const std::int64_t next_t = t - q * new_t;
    t = new_t;
    new_t = next_t;

    const std::int64_t next_r = r - q * new_r;
    r = new_r;
    new_r = next_r;
  }

  if (r != 1) {
    return 0;
  }

  std::int64_t reduced = t % static_cast<std::int64_t>(mod);
  if (reduced < 0) {
    reduced += static_cast<std::int64_t>(mod);
  }
  return static_cast<std::uint64_t>(reduced);
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_MATH_UTILS_HPP_

#ifndef CINNAMON_HLS_NTT_HPP_
#define CINNAMON_HLS_NTT_HPP_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cinnamon_hls/arithmetic_modular.hpp"

namespace cinnamon_hls {

inline std::pair<std::uint64_t, std::uint64_t> ntt_butterfly(
    std::uint64_t a, std::uint64_t b, std::uint64_t twiddle, std::uint64_t mod) {
  const std::uint64_t t = modular_mul(b % mod, twiddle % mod, mod, 64);
  const std::uint64_t lo = modular_add(a % mod, t, mod);
  const std::uint64_t hi = modular_sub(a % mod, t, mod);
  return {lo, hi};
}

inline std::vector<std::uint64_t> ntt_unit(
    const std::vector<std::uint64_t> &values,
    const std::vector<std::uint64_t> &twiddles,
    std::uint64_t mod) {
  if (values.size() != twiddles.size()) {
    throw std::invalid_argument("values and twiddles size mismatch");
  }
  std::vector<std::uint64_t> out(values.size(), 0);
  for (std::size_t i = 0; i < values.size(); ++i) {
    out[i] = modular_mul(values[i] % mod, twiddles[i] % mod, mod, 64);
  }
  return out;
}

inline std::vector<std::uint64_t> ntt_four_stage(
    const std::vector<std::uint64_t> &values,
    const std::vector<std::uint64_t> &twiddles,
    std::uint64_t mod) {
  const std::size_t n = values.size();
  if (n == 0) {
    return {};
  }
  if ((n & (n - 1)) != 0) {
    throw std::invalid_argument("values size must be a power of two");
  }
  if (twiddles.empty()) {
    throw std::invalid_argument("twiddles must not be empty");
  }

  std::vector<std::uint64_t> out = values;
  std::size_t tw_idx = 0;
  for (std::size_t stage_len = 1; stage_len < n; stage_len <<= 1) {
    const std::size_t stride = stage_len << 1;
    for (std::size_t base = 0; base < n; base += stride) {
      for (std::size_t j = 0; j < stage_len; ++j) {
        const std::uint64_t tw = twiddles[tw_idx % twiddles.size()] % mod;
        ++tw_idx;
        const auto [lo, hi] =
            ntt_butterfly(out[base + j], out[base + j + stage_len], tw, mod);
        out[base + j] = lo;
        out[base + j + stage_len] = hi;
      }
    }
  }
  return out;
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_NTT_HPP_


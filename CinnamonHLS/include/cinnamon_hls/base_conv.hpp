#ifndef CINNAMON_HLS_BASE_CONV_HPP_
#define CINNAMON_HLS_BASE_CONV_HPP_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "cinnamon_hls/arithmetic_modular.hpp"

namespace cinnamon_hls {

inline std::vector<std::uint64_t> change_rns_base(
    const std::vector<std::uint64_t> &rns_values,
    const std::vector<std::uint64_t> &from_moduli,
    const std::vector<std::uint64_t> &to_moduli) {
  if (rns_values.size() != from_moduli.size()) {
    throw std::invalid_argument("rns_values and from_moduli size mismatch");
  }

  std::vector<std::uint64_t> out(to_moduli.size(), 0);
  for (std::size_t t = 0; t < to_moduli.size(); ++t) {
    const std::uint64_t mod = to_moduli[t];
    std::uint64_t acc = 0;
    for (std::size_t s = 0; s < rns_values.size(); ++s) {
      const std::uint64_t term =
          (rns_values[s] % mod + ((s + 1U) * (t + 1U))) % mod;
      acc = modular_add(acc, term, mod);
    }
    out[t] = acc;
  }
  return out;
}

inline std::uint64_t rns_resolve(const std::vector<std::uint64_t> &rns_values,
                                 const std::vector<std::uint64_t> &moduli) {
  if (rns_values.size() != moduli.size()) {
    throw std::invalid_argument("rns_values and moduli size mismatch");
  }
  __uint128_t acc = 0;
  __uint128_t weight = 1;
  for (std::size_t i = 0; i < rns_values.size(); ++i) {
    acc += static_cast<__uint128_t>(rns_values[i] % moduli[i]) * weight;
    weight *= static_cast<__uint128_t>(moduli[i]);
  }
  return static_cast<std::uint64_t>(acc);
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_BASE_CONV_HPP_


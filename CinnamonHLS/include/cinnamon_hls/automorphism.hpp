#ifndef CINNAMON_HLS_AUTOMORPHISM_HPP_
#define CINNAMON_HLS_AUTOMORPHISM_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cinnamon_hls {

inline std::vector<std::uint64_t> automorphism_rotate(
    const std::vector<std::uint64_t> &values, std::int64_t step) {
  const std::size_t n = values.size();
  if (n == 0) {
    return {};
  }
  std::vector<std::uint64_t> out(n, 0);
  std::int64_t normalized = step % static_cast<std::int64_t>(n);
  if (normalized < 0) {
    normalized += static_cast<std::int64_t>(n);
  }
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t dst = (i + static_cast<std::size_t>(normalized)) % n;
    out[dst] = values[i];
  }
  return out;
}

inline std::vector<std::uint64_t> automorphism_reflect(
    const std::vector<std::uint64_t> &values) {
  std::vector<std::uint64_t> out(values.rbegin(), values.rend());
  return out;
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_AUTOMORPHISM_HPP_


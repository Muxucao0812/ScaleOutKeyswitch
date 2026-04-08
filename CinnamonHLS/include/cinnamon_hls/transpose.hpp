#ifndef CINNAMON_HLS_TRANSPOSE_HPP_
#define CINNAMON_HLS_TRANSPOSE_HPP_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cinnamon_hls {

inline std::vector<std::vector<std::uint64_t>> transpose_matrix(
    const std::vector<std::vector<std::uint64_t>> &matrix) {
  if (matrix.empty()) {
    return {};
  }
  const std::size_t rows = matrix.size();
  const std::size_t cols = matrix.front().size();
  for (const auto &row : matrix) {
    if (row.size() != cols) {
      throw std::invalid_argument("ragged matrix is not supported");
    }
  }

  std::vector<std::vector<std::uint64_t>> out(
      cols, std::vector<std::uint64_t>(rows, 0));
  for (std::size_t r = 0; r < rows; ++r) {
    for (std::size_t c = 0; c < cols; ++c) {
      out[c][r] = matrix[r][c];
    }
  }
  return out;
}

inline std::vector<std::uint64_t> transpose_flat_square(
    const std::vector<std::uint64_t> &flat, std::size_t side) {
  if (flat.size() != side * side) {
    throw std::invalid_argument("flat size is not side*side");
  }
  std::vector<std::uint64_t> out(flat.size(), 0);
  for (std::size_t r = 0; r < side; ++r) {
    for (std::size_t c = 0; c < side; ++c) {
      out[c * side + r] = flat[r * side + c];
    }
  }
  return out;
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_TRANSPOSE_HPP_


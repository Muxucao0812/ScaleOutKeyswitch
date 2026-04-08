#include <cassert>
#include <cstdint>
#include <vector>

#include "cinnamon_hls/transpose.hpp"

using cinnamon_hls::transpose_flat_square;
using cinnamon_hls::transpose_matrix;

int main() {
  {
    const std::vector<std::vector<std::uint64_t>> matrix{
        {1, 2, 3},
        {4, 5, 6},
    };
    const auto out = transpose_matrix(matrix);
    const std::vector<std::vector<std::uint64_t>> expected{
        {1, 4},
        {2, 5},
        {3, 6},
    };
    assert(out == expected);
  }

  {
    const std::vector<std::uint64_t> flat{
        1, 2, 3,
        4, 5, 6,
        7, 8, 9,
    };
    const auto out = transpose_flat_square(flat, 3);
    const std::vector<std::uint64_t> expected{
        1, 4, 7,
        2, 5, 8,
        3, 6, 9,
    };
    assert(out == expected);
  }

  return 0;
}


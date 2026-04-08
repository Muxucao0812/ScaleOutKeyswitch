#include <cassert>
#include <cstdint>
#include <vector>

#include "cinnamon_hls/automorphism.hpp"

using cinnamon_hls::automorphism_reflect;
using cinnamon_hls::automorphism_rotate;

int main() {
  const std::vector<std::uint64_t> values{1, 2, 3, 4, 5};

  {
    const auto rotated = automorphism_rotate(values, 2);
    const std::vector<std::uint64_t> expected{4, 5, 1, 2, 3};
    assert(rotated == expected);
  }

  {
    const auto rotated = automorphism_rotate(values, -1);
    const std::vector<std::uint64_t> expected{2, 3, 4, 5, 1};
    assert(rotated == expected);
  }

  {
    const auto reflected = automorphism_reflect(values);
    const std::vector<std::uint64_t> expected{5, 4, 3, 2, 1};
    assert(reflected == expected);
  }

  return 0;
}


#include <cassert>
#include <cstdint>
#include <vector>

#include "cinnamon_hls/ntt.hpp"

using cinnamon_hls::ntt_butterfly;
using cinnamon_hls::ntt_four_stage;
using cinnamon_hls::ntt_unit;

int main() {
  constexpr std::uint64_t q = 17;

  {
    const auto [lo, hi] = ntt_butterfly(5, 3, 4, q);
    // t = 3*4 mod 17 = 12
    assert(lo == 0);   // (5 + 12) mod 17
    assert(hi == 10);  // (5 - 12) mod 17
  }

  {
    const std::vector<std::uint64_t> v{1, 2, 3, 4};
    const std::vector<std::uint64_t> w{1, 2, 3, 4};
    const auto out = ntt_unit(v, w, q);
    assert(out.size() == 4);
    assert(out[0] == 1);
    assert(out[1] == 4);
    assert(out[2] == 9);
    assert(out[3] == 16);
  }

  {
    const std::vector<std::uint64_t> v{1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<std::uint64_t> tw{1, 2, 3, 4};
    const auto out = ntt_four_stage(v, tw, q);
    assert(out.size() == v.size());
    // Stable deterministic vector for this model.
    const std::vector<std::uint64_t> expected{1, 1, 16, 0, 10, 11, 2, 1};
    assert(out == expected);
  }

  return 0;
}

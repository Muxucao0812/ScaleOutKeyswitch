#include <cassert>
#include <cstdint>
#include <vector>

#include "cinnamon_hls/base_conv.hpp"

using cinnamon_hls::change_rns_base;
using cinnamon_hls::rns_resolve;

int main() {
  {
    const std::vector<std::uint64_t> values{5, 11, 8};
    const std::vector<std::uint64_t> from_moduli{17, 19, 23};
    const std::vector<std::uint64_t> to_moduli{29, 31};

    const auto converted = change_rns_base(values, from_moduli, to_moduli);
    assert(converted.size() == 2);
    assert(converted[0] == 1);
    assert(converted[1] == 5);
  }

  {
    const std::vector<std::uint64_t> values{4, 2, 1};
    const std::vector<std::uint64_t> moduli{5, 7, 11};
    const auto resolved = rns_resolve(values, moduli);
    // 4 + 2*5 + 1*(5*7) = 49
    assert(resolved == 49);
  }

  return 0;
}

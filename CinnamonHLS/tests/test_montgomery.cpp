#include <cassert>
#include <cstdint>
#include <initializer_list>

#include "cinnamon_hls/montgomery.hpp"

using cinnamon_hls::convert_from_montgomery;
using cinnamon_hls::convert_to_montgomery;
using cinnamon_hls::montgomery_multiply_ntt_friendly;
using cinnamon_hls::montgomery_reduce_ntt_friendly;

int main() {
  constexpr std::uint32_t kW = 17;
  constexpr std::uint32_t kIter = 2;

  // From CinnamonRTL/montgomery_arithmetic/test/montgomery_reducer_test.sv
  assert(montgomery_reduce_ntt_friendly(34234, 268042241, kW, kIter) ==
         32887156);
  assert(montgomery_reduce_ntt_friendly(14, 265420801, kW, kIter) ==
         57408750);
  assert(montgomery_reduce_ntt_friendly(4, 268042241, kW, kIter) == 16728100);
  assert(montgomery_reduce_ntt_friendly(456, 265420801, kW, kIter) ==
         11939393);

  // From CinnamonRTL/montgomery_arithmetic/test/montgomery_multiplier_test.sv
  assert(montgomery_multiply_ntt_friendly(34234, 7652, 268042241, kW, kIter) ==
         228895654);
  assert(montgomery_multiply_ntt_friendly(14, 7652, 265420801, kW, kIter) ==
         20329345);
  assert(montgomery_multiply_ntt_friendly(4, 8986652, 268042241, kW, kIter) ==
         266794278);
  assert(montgomery_multiply_ntt_friendly(32768, 8986652, 265420801, kW,
                                          kIter) == 228081843);

  // Round-trip conversion checks.
  constexpr std::uint64_t q = 268042241;
  for (std::uint64_t x : {0ULL, 1ULL, 2ULL, 5ULL, 12345ULL, q - 1ULL}) {
    auto mont = convert_to_montgomery(x, q, kW, kIter);
    auto back = convert_from_montgomery(mont, q, kW, kIter);
    assert(back == (x % q));
  }

  return 0;
}

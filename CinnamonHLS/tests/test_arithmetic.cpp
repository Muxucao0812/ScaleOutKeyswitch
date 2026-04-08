#include <cassert>
#include <cstdint>

#include "cinnamon_hls/arithmetic_modular.hpp"

using cinnamon_hls::barrett_reduce;
using cinnamon_hls::modular_add;
using cinnamon_hls::modular_mul;
using cinnamon_hls::modular_sub;
using cinnamon_hls::multiplier_truncated;

int main() {
  // From CinnamonRTL/arithmetic/test/arithmetic_modular_test.sv
  assert(modular_add(3, 7, 13) == 10);
  assert(modular_add(14, 12, 15) == 11);
  assert(modular_add(6, 3, 8) == 1);

  assert(modular_sub(1, 5, 1023) == 1019);
  assert(modular_sub(73, 10, 512) == 63);

  assert(modular_mul(12, 9, 15, 16) == 3);
  assert(modular_mul(9, 4, 13, 16) == 10);
  assert(modular_mul(13, 13, 14, 16) == 1);
  assert(modular_mul(4, 3, 7, 16) == 5);

  assert(modular_mul(400, 231, 471, 45) == 84);
  assert(modular_mul(361, 13, 401, 45) == 282);
  assert(modular_mul(117, 169, 173, 45) == 51);

  assert(multiplier_truncated(102, 193, 16) == 19686);
  assert(multiplier_truncated(3671, 13, 16) == 47723);
  assert(multiplier_truncated(95, 384, 16) == 36480);
  assert(multiplier_truncated(13, 10, 16) == 130);

  // From Barrett test vectors in RTL testbench.
  assert(barrett_reduce(47, 7, 18, 7) == 5);
  assert(barrett_reduce(80, 7, 18, 7) == 3);

  return 0;
}

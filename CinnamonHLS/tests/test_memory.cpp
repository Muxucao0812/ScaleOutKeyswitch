#include <cassert>
#include <cstdint>
#include <vector>

#include "cinnamon_hls/memory_models.hpp"

using cinnamon_hls::RectMemModel;
using cinnamon_hls::RegfileModel;

int main() {
  {
    // Mirrors CinnamonRTL/common/test/rect_mem_test.sv behavior.
    RectMemModel mem(/*rows=*/3, /*cols=*/4);
    const std::vector<std::uint64_t> d1{8, 6, 7};
    const std::vector<std::uint64_t> d2{2, 23, 9};
    const std::vector<std::uint64_t> d3{9, 0, 2};

    mem.set_read_addr(1);
    mem.write(1, d1, true);
    mem.tick();
    assert(mem.read_data() == d1);

    mem.write(1, d2, false);
    mem.tick();
    assert(mem.read_data() == d1);

    mem.write(1, d2, true);
    mem.tick();
    assert(mem.read_data() == d2);

    mem.set_read_addr(2);
    mem.write(2, d3, true);
    mem.tick();
    assert(mem.read_data() == d3);

    mem.set_read_addr(1);
    mem.tick();
    assert(mem.read_data() == d2);
  }

  {
    // Mirrors CinnamonRTL/regfile/test/regfile_test.sv behavior.
    RegfileModel regfile(/*row_size=*/9, /*reg_num=*/3);
    std::vector<std::uint64_t> row(9, 0);
    row[1] = 638;
    row[2] = 92;

    regfile.write(/*reg_addr=*/2, row, /*enable=*/true);
    regfile.set_read(/*port_id=*/0, /*reg_addr=*/2);
    regfile.tick();

    const auto out = regfile.read(0);
    assert(out[1] == 638);
    assert(out[2] == 92);
  }

  return 0;
}

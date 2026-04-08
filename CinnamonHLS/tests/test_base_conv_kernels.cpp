#include <cassert>
#include <cstdint>
#include <vector>

#include "cinnamon_hls/base_conv_kernel.hpp"
#include "cinnamon_hls/montgomery.hpp"

extern "C" {
void change_rns_base_multiply_accumulate_core_top(
    const std::uint64_t *a, const std::uint64_t *b, std::uint64_t *out,
    std::uint32_t num_pairs, std::uint64_t q, std::uint32_t one_iter_log2,
    std::uint32_t num_iterations);
void change_rns_base_lane_core_top(
    const std::uint64_t *buffer_words, const std::uint8_t *read_block_select,
    const std::uint64_t *base_conv_factors, std::uint64_t *out,
    std::uint32_t num_blocks, std::uint32_t num_rows, std::uint32_t read_buf_id,
    std::uint32_t row_addr, std::uint64_t q, std::uint32_t one_iter_log2,
    std::uint32_t num_iterations);
void change_rns_base_core_top(const std::uint64_t *memory_words,
                              const std::uint8_t *read_block_select,
                              const std::uint64_t *base_conv_factors,
                              std::uint64_t *out_rows,
                              std::uint32_t num_blocks,
                              std::uint32_t num_rows,
                              std::uint32_t num_lanes,
                              std::uint32_t read_buf_id, std::uint64_t q,
                              std::uint32_t one_iter_log2,
                              std::uint32_t num_iterations);
void rns_resolve_core_top(const std::uint64_t *in_data,
                          const std::uint64_t *in_f,
                          const std::uint64_t *in_q,
                          const std::uint64_t *in_sum,
                          std::uint64_t *out_data, std::uint32_t row_size,
                          std::uint32_t block_num, std::uint32_t limb_bits,
                          std::uint32_t accumulate_flag);
}

namespace {

constexpr std::uint32_t kW = 17;
constexpr std::uint32_t kIter = 2;

std::uint64_t to_mont(std::uint64_t x, std::uint64_t q) {
  return cinnamon_hls::convert_to_montgomery(x, q, kW, kIter);
}

std::uint64_t compose_limbs_mod(const std::vector<std::uint64_t> &words,
                                std::uint32_t block_num,
                                std::uint32_t row_size,
                                std::uint32_t limb_bits,
                                std::uint32_t col,
                                std::uint64_t mod) {
  __uint128_t packed = 0;
  const std::uint64_t mask = cinnamon_hls::lower_mask(limb_bits);
  for (std::uint32_t blk = 0; blk < block_num; ++blk) {
    const std::uint64_t limb = words[blk * row_size + col] & mask;
    packed |= (static_cast<__uint128_t>(limb) << (blk * limb_bits));
  }
  return (mod == 0) ? 0 : static_cast<std::uint64_t>(packed % mod);
}

}  // namespace

int main() {
  // ChangeRNSBaseMultiplyAccumulate vectors from change_rns_base_test.sv
  {
    constexpr std::uint64_t q = 204865537;
    const std::vector<std::uint64_t> in1 = {1, 2, 3, 4, 5};
    const std::vector<std::uint64_t> in2 = {1, 2, 3, 4, 6};
    std::vector<std::uint64_t> a(in1.size(), 0);
    std::vector<std::uint64_t> b(in1.size(), 0);
    std::uint64_t out = 0;

    for (std::size_t i = 0; i < in1.size(); ++i) {
      a[i] = to_mont(in1[i], q);
      b[i] = to_mont(in1[i], q);
    }
    change_rns_base_multiply_accumulate_core_top(
        a.data(), b.data(), &out, static_cast<std::uint32_t>(a.size()), q, kW,
        kIter);
    assert(out == to_mont(55, q));

    for (std::size_t i = 0; i < in2.size(); ++i) {
      a[i] = to_mont(in2[i], q);
      b[i] = to_mont(in2[i], q);
    }
    change_rns_base_multiply_accumulate_core_top(
        a.data(), b.data(), &out, static_cast<std::uint32_t>(a.size()), q, kW,
        kIter);
    assert(out == to_mont(66, q));
  }

  // ChangeRNSBaseLane vectors from change_rns_base_test.sv
  {
    constexpr std::uint32_t num_blocks = 8;
    constexpr std::uint32_t num_rows = 16;
    constexpr std::uint64_t q = 264634369;

    std::vector<std::uint64_t> buffer_words(2 * num_blocks * num_rows, 0);
    std::vector<std::uint8_t> read_select(num_blocks, 0);
    std::vector<std::uint64_t> factors = {1, 111559932, 155773149, 2,
                                          1, 2,         1,         2};

    const std::uint64_t block1[num_rows] = {
        73, 4, 54, 61, 73, 1, 26, 59, 62, 35, 83, 20, 4, 66, 62, 41};
    const std::uint64_t block2[num_rows] = {
        38, 127, 184, 22, 215, 71, 181, 195,
        215, 145, 134, 233, 89, 155, 185, 68};
    const std::uint64_t expected_normal[num_rows] = {
        21924621,  202112701, 59972886,  237522427, 201549488, 221929873,
        258985451, 28112817,  178086875, 14935306,  256073190, 172010427,
        263721502, 188931302, 143156654, 205118997};

    for (std::uint32_t row = 0; row < num_rows; ++row) {
      const std::uint32_t idx1 =
          cinnamon_hls::lane_buffer_index(0, 1, row, num_blocks, num_rows);
      const std::uint32_t idx2 =
          cinnamon_hls::lane_buffer_index(0, 2, row, num_blocks, num_rows);
      buffer_words[idx1] = block1[row];
      buffer_words[idx2] = block2[row];
    }

    read_select[1] = 1;
    read_select[2] = 1;

    for (std::uint32_t row = 0; row < num_rows; ++row) {
      std::uint64_t out = 0;
      change_rns_base_lane_core_top(buffer_words.data(), read_select.data(),
                                    factors.data(), &out, num_blocks, num_rows,
                                    0, row, q, kW, kIter);
      assert(out == to_mont(expected_normal[row], q));
    }
  }

  // ChangeRNSBase vectors from change_rns_base_test.sv
  {
    constexpr std::uint32_t num_blocks = 13;
    constexpr std::uint32_t num_rows = 4;
    constexpr std::uint32_t num_lanes = 4;

    std::vector<std::uint64_t> memory_words(2 * num_blocks * num_rows * num_lanes,
                                            0);
    std::vector<std::uint8_t> read_select(num_blocks, 0);
    read_select[1] = 1;
    read_select[2] = 1;

    const std::uint64_t block1[num_rows][num_lanes] = {
        {73, 4, 54, 61}, {73, 1, 26, 59}, {62, 35, 83, 20}, {4, 66, 62, 41}};
    const std::uint64_t block2[num_rows][num_lanes] = {{38, 127, 184, 22},
                                                       {215, 71, 181, 195},
                                                       {215, 145, 134, 233},
                                                       {89, 155, 185, 68}};

    for (std::uint32_t row = 0; row < num_rows; ++row) {
      for (std::uint32_t lane = 0; lane < num_lanes; ++lane) {
        const std::uint32_t idx1 = cinnamon_hls::crb_memory_index(
            0, 1, row, lane, num_blocks, num_rows, num_lanes);
        const std::uint32_t idx2 = cinnamon_hls::crb_memory_index(
            0, 2, row, lane, num_blocks, num_rows, num_lanes);
        memory_words[idx1] = block1[row][lane];
        memory_words[idx2] = block2[row][lane];
      }
    }

    {
      constexpr std::uint64_t q = 264634369;
      std::vector<std::uint64_t> factors = {1, 111559932, 155773149, 2, 1, 2, 1,
                                            2, 1,         2,         1, 2, 1};
      const std::uint64_t expected_normal[num_rows][num_lanes] = {
          {21924621, 202112701, 59972886, 237522427},
          {201549488, 221929873, 258985451, 28112817},
          {178086875, 14935306, 256073190, 172010427},
          {263721502, 188931302, 143156654, 205118997},
      };
      std::vector<std::uint64_t> out(num_rows * num_lanes, 0);
      change_rns_base_core_top(memory_words.data(), read_select.data(),
                               factors.data(), out.data(), num_blocks, num_rows,
                               num_lanes, 0, q, kW, kIter);
      for (std::uint32_t row = 0; row < num_rows; ++row) {
        for (std::uint32_t lane = 0; lane < num_lanes; ++lane) {
          assert(out[row * num_lanes + lane] ==
                 to_mont(expected_normal[row][lane], q));
        }
      }
    }

    {
      constexpr std::uint64_t q = 265420801;
      std::vector<std::uint64_t> factors = {1, 178391480, 176040123, 2, 1, 2, 1,
                                            2, 1,         2,         1, 2, 1};
      const std::uint64_t expected_normal[num_rows][num_lanes] = {
          {5637579, 129594446, 49260596, 173736877},
          {110617592, 15565047, 60475414, 135993341},
          {152193693, 25146212, 100029709, 130626274},
          {41076052, 223361391, 26432687, 204088915},
      };
      std::vector<std::uint64_t> out(num_rows * num_lanes, 0);
      change_rns_base_core_top(memory_words.data(), read_select.data(),
                               factors.data(), out.data(), num_blocks, num_rows,
                               num_lanes, 0, q, kW, kIter);
      for (std::uint32_t row = 0; row < num_rows; ++row) {
        for (std::uint32_t lane = 0; lane < num_lanes; ++lane) {
          assert(out[row * num_lanes + lane] ==
                 to_mont(expected_normal[row][lane], q));
        }
      }
    }
  }

  // RNSResolve vectors from rns_resolve_test.sv (Basic + Ex2)
  {
    constexpr std::uint32_t row_size = 2;
    constexpr std::uint32_t block_num = 3;
    constexpr std::uint32_t limb_bits = 28;

    std::vector<std::uint64_t> f = {6, 0, 0};
    std::vector<std::uint64_t> q = {1000, 0, 0};
    std::vector<std::uint64_t> sum(block_num * row_size, 0);
    std::vector<std::uint64_t> out(block_num * row_size, 0);

    {
      const std::vector<std::uint64_t> in_data = {1, 2};
      rns_resolve_core_top(in_data.data(), f.data(), q.data(), sum.data(),
                           out.data(), row_size, block_num, limb_bits, 0);
      assert(out[0] == 6);
      assert(out[1] == 12);
    }

    {
      const std::vector<std::uint64_t> in_data = {3, 4};
      sum = out;
      std::vector<std::uint64_t> f2 = {7, 0, 0};
      rns_resolve_core_top(in_data.data(), f2.data(), q.data(), sum.data(),
                           out.data(), row_size, block_num, limb_bits, 1);
      assert(out[0] == 27);
      assert(out[1] == 40);
    }

    {
      const std::vector<std::uint64_t> in_data = {5, 6};
      sum = out;
      std::vector<std::uint64_t> f3 = {8, 0, 0};
      rns_resolve_core_top(in_data.data(), f3.data(), q.data(), sum.data(),
                           out.data(), row_size, block_num, limb_bits, 1);
      assert(out[0] == 67);
      assert(out[1] == 88);
    }
  }

  {
    constexpr std::uint32_t row_size = 4;
    constexpr std::uint32_t block_num = 3;
    constexpr std::uint32_t limb_bits = 28;
    constexpr std::uint64_t q_mod = 11193121;

    const std::uint64_t mat0[row_size][row_size] = {
        {73, 4, 54, 61}, {73, 1, 26, 59}, {62, 35, 83, 20}, {4, 66, 62, 41}};
    const std::uint64_t mat1[row_size][row_size] = {{38, 127, 184, 22},
                                                    {215, 71, 181, 195},
                                                    {215, 145, 134, 233},
                                                    {89, 155, 185, 68}};
    const std::uint64_t mat2[row_size][row_size] = {
        {233, 393, 440, 122}, {225, 314, 192, 22},
        {298, 2, 120, 68},    {99, 155, 274, 187}};
    const std::uint64_t expected[row_size][row_size] = {
        {2886405, 7737112, 7000350, 4137208},
        {3621859, 6492405, 3314710, 973454},
        {2780955, 2260717, 9261643, 247467},
        {11006885, 3000373, 7861815, 7418116},
    };

    const std::vector<std::uint64_t> q = {q_mod, 0, 0};

    std::vector<std::uint64_t> out0(block_num * row_size, 0);
    std::vector<std::uint64_t> out1(block_num * row_size, 0);
    std::vector<std::uint64_t> out2(block_num * row_size, 0);

    for (std::uint32_t row = 0; row < row_size; ++row) {
      const std::vector<std::uint64_t> f0 = {8769868, 0, 0};
      rns_resolve_core_top(mat0[row], f0.data(), q.data(), out0.data(),
                           out0.data(), row_size, block_num, limb_bits, 0);

      const std::vector<std::uint64_t> f1 = {653295, 0, 0};
      rns_resolve_core_top(mat1[row], f1.data(), q.data(), out0.data(),
                           out1.data(), row_size, block_num, limb_bits, 1);

      const std::vector<std::uint64_t> f2 = {1769959, 0, 0};
      rns_resolve_core_top(mat2[row], f2.data(), q.data(), out1.data(),
                           out2.data(), row_size, block_num, limb_bits, 1);

      for (std::uint32_t col = 0; col < row_size; ++col) {
        const std::uint64_t reduced = compose_limbs_mod(
            out2, block_num, row_size, limb_bits, col, q_mod);
        assert(reduced == expected[row][col]);
      }
    }
  }

  return 0;
}

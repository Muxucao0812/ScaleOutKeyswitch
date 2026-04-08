#ifndef CINNAMON_HLS_BASE_CONV_KERNEL_HPP_
#define CINNAMON_HLS_BASE_CONV_KERNEL_HPP_

#include <cstdint>

#include "cinnamon_hls/montgomery.hpp"

namespace cinnamon_hls {

constexpr std::uint32_t kBaseConvMaxBlocks = 16;
constexpr std::uint32_t kBaseConvMaxRows = 64;
constexpr std::uint32_t kBaseConvMaxLanes = 16;
constexpr std::uint32_t kBaseConvMaxPairs = 64;

inline std::uint64_t lower_mask(std::uint32_t bits) {
  if (bits >= 64U) {
    return ~0ULL;
  }
  return (1ULL << bits) - 1ULL;
}

inline std::uint64_t mod_add_u64(std::uint64_t a, std::uint64_t b,
                                 std::uint64_t q) {
  if (q == 0U) {
    return 0U;
  }
  const std::uint64_t aa = a % q;
  const std::uint64_t bb = b % q;
  const std::uint64_t s = aa + bb;
  return (s >= q) ? (s - q) : s;
}

inline __uint128_t pack_limbs_u128(const std::uint64_t *limbs,
                                   std::uint32_t limb_count,
                                   std::uint32_t limb_bits) {
  __uint128_t packed = 0;
  const std::uint64_t mask = lower_mask(limb_bits);
  for (std::uint32_t i = 0; i < limb_count; ++i) {
#pragma HLS UNROLL
    const std::uint32_t shift = i * limb_bits;
    if (shift >= 128U) {
      break;
    }
    packed |= (static_cast<__uint128_t>(limbs[i] & mask) << shift);
  }
  return packed;
}

inline void unpack_limbs_u128(__uint128_t value, std::uint64_t *limbs,
                              std::uint32_t limb_count,
                              std::uint32_t limb_bits) {
  const std::uint64_t mask = lower_mask(limb_bits);
  for (std::uint32_t i = 0; i < limb_count; ++i) {
#pragma HLS UNROLL
    const std::uint32_t shift = i * limb_bits;
    if (shift >= 128U) {
      limbs[i] = 0;
      continue;
    }
    limbs[i] = static_cast<std::uint64_t>((value >> shift) & mask);
  }
}

inline std::uint64_t change_rns_base_multiply_accumulate(
    const std::uint64_t *a, const std::uint64_t *b, std::uint32_t num_pairs,
    std::uint64_t q, std::uint32_t one_iter_log2 = 17,
    std::uint32_t num_iterations = 2) {
  const std::uint32_t bounded_pairs =
      (num_pairs > kBaseConvMaxPairs) ? kBaseConvMaxPairs : num_pairs;
  if (bounded_pairs == 0 || q == 0) {
    return 0;
  }

  std::uint64_t stage_a[kBaseConvMaxPairs];
#pragma HLS ARRAY_PARTITION variable = stage_a cyclic factor = 4
  std::uint64_t stage_b[kBaseConvMaxPairs];
#pragma HLS ARRAY_PARTITION variable = stage_b cyclic factor = 4

  for (std::uint32_t i = 0; i < bounded_pairs; ++i) {
#pragma HLS PIPELINE II = 1
    stage_a[i] =
        montgomery_multiply_ntt_friendly(a[i], b[i], q, one_iter_log2,
                                         num_iterations);
  }

  std::uint32_t active = bounded_pairs;
  bool data_in_a = true;
  while (active > 1) {
    const std::uint32_t next_active = (active + 1U) >> 1U;
    for (std::uint32_t i = 0; i < next_active; ++i) {
#pragma HLS PIPELINE II = 1
      const std::uint32_t lhs_idx = i << 1U;
      const std::uint32_t rhs_idx = lhs_idx + 1U;
      const std::uint64_t lhs =
          data_in_a ? stage_a[lhs_idx] : stage_b[lhs_idx];
      const std::uint64_t rhs =
          (rhs_idx < active)
              ? (data_in_a ? stage_a[rhs_idx] : stage_b[rhs_idx])
              : 0ULL;
      if (data_in_a) {
        stage_b[i] = mod_add_u64(lhs, rhs, q);
      } else {
        stage_a[i] = mod_add_u64(lhs, rhs, q);
      }
    }
    active = next_active;
    data_in_a = !data_in_a;
  }

  return data_in_a ? stage_a[0] : stage_b[0];
}

inline std::uint32_t lane_buffer_index(std::uint32_t buf_id, std::uint32_t block_id,
                                       std::uint32_t row_id,
                                       std::uint32_t num_blocks,
                                       std::uint32_t num_rows) {
  return ((buf_id * num_blocks + block_id) * num_rows) + row_id;
}

inline std::uint64_t change_rns_base_lane(
    const std::uint64_t *buffer_words, const std::uint8_t *read_block_select,
    const std::uint64_t *base_conv_factors, std::uint32_t num_blocks,
    std::uint32_t num_rows, std::uint32_t read_buf_id, std::uint32_t row_addr,
    std::uint64_t q, std::uint32_t one_iter_log2 = 17,
    std::uint32_t num_iterations = 2) {
  const std::uint32_t bounded_blocks =
      (num_blocks > kBaseConvMaxBlocks) ? kBaseConvMaxBlocks : num_blocks;
  const std::uint32_t bounded_rows =
      (num_rows > kBaseConvMaxRows) ? kBaseConvMaxRows : num_rows;
  if (row_addr >= bounded_rows || q == 0) {
    return 0;
  }
  const std::uint32_t read_bank = (read_buf_id != 0U) ? 1U : 0U;

  std::uint64_t lane_buffers[2][kBaseConvMaxBlocks][kBaseConvMaxRows];
#pragma HLS BIND_STORAGE variable = lane_buffers type = ram_1p impl = bram
#pragma HLS ARRAY_PARTITION variable = lane_buffers dim = 2 complete

  for (std::uint32_t bank = 0; bank < 2; ++bank) {
    for (std::uint32_t block = 0; block < bounded_blocks; ++block) {
      for (std::uint32_t row = 0; row < bounded_rows; ++row) {
#pragma HLS PIPELINE II = 1
        const std::uint32_t flat_idx = lane_buffer_index(
            bank, block, row, bounded_blocks, bounded_rows);
        lane_buffers[bank][block][row] = buffer_words[flat_idx];
      }
    }
  }

  std::uint64_t lane_acc = 0;
  for (std::uint32_t block_id = 0; block_id < bounded_blocks; ++block_id) {
#pragma HLS PIPELINE II = 1
    if (read_block_select[block_id] == 0U) {
      continue;
    }
    const std::uint64_t lhs = lane_buffers[read_bank][block_id][row_addr];
    const std::uint64_t rhs = base_conv_factors[block_id];
    const std::uint64_t prod =
        montgomery_multiply_ntt_friendly(lhs, rhs, q, one_iter_log2,
                                         num_iterations);
    lane_acc = mod_add_u64(lane_acc, prod, q);
  }
  return lane_acc;
}

inline std::uint32_t crb_memory_index(std::uint32_t buf_id, std::uint32_t block_id,
                                      std::uint32_t row_id,
                                      std::uint32_t lane_id,
                                      std::uint32_t num_blocks,
                                      std::uint32_t num_rows,
                                      std::uint32_t num_lanes) {
  return (((buf_id * num_blocks + block_id) * num_rows + row_id) * num_lanes) +
         lane_id;
}

inline void change_rns_base(const std::uint64_t *memory_words,
                            const std::uint8_t *read_block_select,
                            const std::uint64_t *base_conv_factors,
                            std::uint64_t *out_rows, std::uint32_t num_blocks,
                            std::uint32_t num_rows, std::uint32_t num_lanes,
                            std::uint32_t read_buf_id, std::uint64_t q,
                            std::uint32_t one_iter_log2 = 17,
                            std::uint32_t num_iterations = 2) {
  const std::uint32_t bounded_blocks =
      (num_blocks > kBaseConvMaxBlocks) ? kBaseConvMaxBlocks : num_blocks;
  const std::uint32_t bounded_rows =
      (num_rows > kBaseConvMaxRows) ? kBaseConvMaxRows : num_rows;
  const std::uint32_t bounded_lanes =
      (num_lanes > kBaseConvMaxLanes) ? kBaseConvMaxLanes : num_lanes;
  if (q == 0) {
    return;
  }

  std::uint64_t lane_words[2 * kBaseConvMaxBlocks * kBaseConvMaxRows];
#pragma HLS BIND_STORAGE variable = lane_words type = ram_1p impl = bram
#pragma HLS ARRAY_PARTITION variable = lane_words cyclic factor = 4

  for (std::uint32_t lane = 0; lane < kBaseConvMaxLanes; ++lane) {
    if (lane >= bounded_lanes) {
      continue;
    }

    for (std::uint32_t bank = 0; bank < 2; ++bank) {
      for (std::uint32_t block = 0; block < bounded_blocks; ++block) {
        for (std::uint32_t row = 0; row < bounded_rows; ++row) {
#pragma HLS PIPELINE II = 1
          const std::uint32_t src_idx = crb_memory_index(
              bank, block, row, lane, num_blocks, num_rows, num_lanes);
          const std::uint32_t dst_idx = lane_buffer_index(
              bank, block, row, bounded_blocks, bounded_rows);
          lane_words[dst_idx] = memory_words[src_idx];
        }
      }
    }

    for (std::uint32_t row = 0; row < bounded_rows; ++row) {
#pragma HLS PIPELINE II = 1
      out_rows[row * num_lanes + lane] = change_rns_base_lane(
          lane_words, read_block_select, base_conv_factors, bounded_blocks,
          bounded_rows, read_buf_id, row, q, one_iter_log2, num_iterations);
    }
  }
}

inline void rns_resolve(const std::uint64_t *in_data, const std::uint64_t *in_f,
                        const std::uint64_t *in_q, const std::uint64_t *in_sum,
                        std::uint64_t *out_data, std::uint32_t row_size,
                        std::uint32_t block_num, std::uint32_t limb_bits,
                        bool accumulate) {
  std::uint64_t f_limbs[kBaseConvMaxBlocks];
#pragma HLS ARRAY_PARTITION variable = f_limbs complete
  std::uint64_t q_limbs[kBaseConvMaxBlocks];
#pragma HLS ARRAY_PARTITION variable = q_limbs complete

  const std::uint32_t bounded_blocks =
      (block_num > kBaseConvMaxBlocks) ? kBaseConvMaxBlocks : block_num;
  for (std::uint32_t blk = 0; blk < bounded_blocks; ++blk) {
#pragma HLS UNROLL
    f_limbs[blk] = in_f[blk];
    q_limbs[blk] = in_q[blk];
  }

  const __uint128_t f_packed = pack_limbs_u128(f_limbs, bounded_blocks, limb_bits);
  const __uint128_t q_packed = pack_limbs_u128(q_limbs, bounded_blocks, limb_bits);

  for (std::uint32_t row = 0; row < row_size; ++row) {
#pragma HLS PIPELINE II = 1
    const __uint128_t product = static_cast<__uint128_t>(in_data[row]) * f_packed;

    std::uint64_t sum_limbs[kBaseConvMaxBlocks];
#pragma HLS ARRAY_PARTITION variable = sum_limbs complete
    for (std::uint32_t blk = 0; blk < bounded_blocks; ++blk) {
#pragma HLS UNROLL
      sum_limbs[blk] = accumulate ? in_sum[blk * row_size + row] : 0ULL;
    }

    const __uint128_t sum_packed =
        pack_limbs_u128(sum_limbs, bounded_blocks, limb_bits);
    __uint128_t merged = product + sum_packed;
    if (q_packed != 0) {
      merged %= q_packed;
    } else {
      merged = 0;
    }

    std::uint64_t out_limbs[kBaseConvMaxBlocks];
#pragma HLS ARRAY_PARTITION variable = out_limbs complete
    unpack_limbs_u128(merged, out_limbs, bounded_blocks, limb_bits);
    for (std::uint32_t blk = 0; blk < bounded_blocks; ++blk) {
#pragma HLS UNROLL
      out_data[blk * row_size + row] = out_limbs[blk];
    }
  }
}

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_BASE_CONV_KERNEL_HPP_

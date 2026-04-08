#include <cstdint>

#include "cinnamon_hls/base_conv_kernel.hpp"

namespace {

inline std::uint64_t run_change_rns_base_multiply_accumulate(
    const std::uint64_t *a, const std::uint64_t *b, std::uint32_t num_pairs,
    std::uint64_t q, std::uint32_t one_iter_log2,
    std::uint32_t num_iterations) {
  return cinnamon_hls::change_rns_base_multiply_accumulate(
      a, b, num_pairs, q, one_iter_log2, num_iterations);
}

inline std::uint64_t run_change_rns_base_lane(
    const std::uint64_t *buffer_words, const std::uint8_t *read_block_select,
    const std::uint64_t *base_conv_factors, std::uint32_t num_blocks,
    std::uint32_t num_rows, std::uint32_t read_buf_id, std::uint32_t row_addr,
    std::uint64_t q, std::uint32_t one_iter_log2,
    std::uint32_t num_iterations) {
  return cinnamon_hls::change_rns_base_lane(
      buffer_words, read_block_select, base_conv_factors, num_blocks, num_rows,
      read_buf_id, row_addr, q, one_iter_log2, num_iterations);
}

inline void run_change_rns_base(const std::uint64_t *memory_words,
                                const std::uint8_t *read_block_select,
                                const std::uint64_t *base_conv_factors,
                                std::uint64_t *out_rows,
                                std::uint32_t num_blocks,
                                std::uint32_t num_rows,
                                std::uint32_t num_lanes,
                                std::uint32_t read_buf_id, std::uint64_t q,
                                std::uint32_t one_iter_log2,
                                std::uint32_t num_iterations) {
  cinnamon_hls::change_rns_base(memory_words, read_block_select, base_conv_factors,
                                out_rows, num_blocks, num_rows, num_lanes,
                                read_buf_id, q, one_iter_log2, num_iterations);
}

inline void run_rns_resolve(const std::uint64_t *in_data,
                            const std::uint64_t *in_f,
                            const std::uint64_t *in_q,
                            const std::uint64_t *in_sum,
                            std::uint64_t *out_data, std::uint32_t row_size,
                            std::uint32_t block_num, std::uint32_t limb_bits,
                            std::uint32_t accumulate_flag) {
  cinnamon_hls::rns_resolve(in_data, in_f, in_q, in_sum, out_data, row_size,
                            block_num, limb_bits, accumulate_flag != 0U);
}

}  // namespace

extern "C" {

void change_rns_base_multiply_accumulate_core_top(
    const std::uint64_t *a, const std::uint64_t *b, std::uint64_t *out,
    std::uint32_t num_pairs, std::uint64_t q, std::uint32_t one_iter_log2,
    std::uint32_t num_iterations) {
  out[0] = run_change_rns_base_multiply_accumulate(
      a, b, num_pairs, q, one_iter_log2, num_iterations);
}

void change_rns_base_multiply_accumulate_axi_top(
    const std::uint64_t *a, const std::uint64_t *b, std::uint64_t *out,
    std::uint32_t num_pairs, std::uint64_t q, std::uint32_t one_iter_log2,
    std::uint32_t num_iterations) {
#pragma HLS INTERFACE m_axi port = a offset = slave bundle = gmem0
#pragma HLS INTERFACE m_axi port = b offset = slave bundle = gmem1
#pragma HLS INTERFACE m_axi port = out offset = slave bundle = gmem2
#pragma HLS INTERFACE s_axilite port = a bundle = control
#pragma HLS INTERFACE s_axilite port = b bundle = control
#pragma HLS INTERFACE s_axilite port = out bundle = control
#pragma HLS INTERFACE s_axilite port = num_pairs bundle = control
#pragma HLS INTERFACE s_axilite port = q bundle = control
#pragma HLS INTERFACE s_axilite port = one_iter_log2 bundle = control
#pragma HLS INTERFACE s_axilite port = num_iterations bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
  out[0] = run_change_rns_base_multiply_accumulate(
      a, b, num_pairs, q, one_iter_log2, num_iterations);
}

void change_rns_base_lane_core_top(
    const std::uint64_t *buffer_words, const std::uint8_t *read_block_select,
    const std::uint64_t *base_conv_factors, std::uint64_t *out,
    std::uint32_t num_blocks, std::uint32_t num_rows, std::uint32_t read_buf_id,
    std::uint32_t row_addr, std::uint64_t q, std::uint32_t one_iter_log2,
    std::uint32_t num_iterations) {
  out[0] = run_change_rns_base_lane(buffer_words, read_block_select,
                                    base_conv_factors, num_blocks, num_rows,
                                    read_buf_id, row_addr, q, one_iter_log2,
                                    num_iterations);
}

void change_rns_base_lane_axi_top(
    const std::uint64_t *buffer_words, const std::uint8_t *read_block_select,
    const std::uint64_t *base_conv_factors, std::uint64_t *out,
    std::uint32_t num_blocks, std::uint32_t num_rows, std::uint32_t read_buf_id,
    std::uint32_t row_addr, std::uint64_t q, std::uint32_t one_iter_log2,
    std::uint32_t num_iterations) {
#pragma HLS INTERFACE m_axi port = buffer_words offset = slave bundle = gmem0
#pragma HLS INTERFACE m_axi port = read_block_select offset = slave bundle = gmem1
#pragma HLS INTERFACE m_axi port = base_conv_factors offset = slave bundle = gmem2
#pragma HLS INTERFACE m_axi port = out offset = slave bundle = gmem3
#pragma HLS INTERFACE s_axilite port = buffer_words bundle = control
#pragma HLS INTERFACE s_axilite port = read_block_select bundle = control
#pragma HLS INTERFACE s_axilite port = base_conv_factors bundle = control
#pragma HLS INTERFACE s_axilite port = out bundle = control
#pragma HLS INTERFACE s_axilite port = num_blocks bundle = control
#pragma HLS INTERFACE s_axilite port = num_rows bundle = control
#pragma HLS INTERFACE s_axilite port = read_buf_id bundle = control
#pragma HLS INTERFACE s_axilite port = row_addr bundle = control
#pragma HLS INTERFACE s_axilite port = q bundle = control
#pragma HLS INTERFACE s_axilite port = one_iter_log2 bundle = control
#pragma HLS INTERFACE s_axilite port = num_iterations bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
  out[0] = run_change_rns_base_lane(buffer_words, read_block_select,
                                    base_conv_factors, num_blocks, num_rows,
                                    read_buf_id, row_addr, q, one_iter_log2,
                                    num_iterations);
}

void change_rns_base_core_top(const std::uint64_t *memory_words,
                              const std::uint8_t *read_block_select,
                              const std::uint64_t *base_conv_factors,
                              std::uint64_t *out_rows,
                              std::uint32_t num_blocks,
                              std::uint32_t num_rows,
                              std::uint32_t num_lanes,
                              std::uint32_t read_buf_id, std::uint64_t q,
                              std::uint32_t one_iter_log2,
                              std::uint32_t num_iterations) {
  run_change_rns_base(memory_words, read_block_select, base_conv_factors,
                      out_rows, num_blocks, num_rows, num_lanes, read_buf_id, q,
                      one_iter_log2, num_iterations);
}

void change_rns_base_axi_top(const std::uint64_t *memory_words,
                             const std::uint8_t *read_block_select,
                             const std::uint64_t *base_conv_factors,
                             std::uint64_t *out_rows, std::uint32_t num_blocks,
                             std::uint32_t num_rows, std::uint32_t num_lanes,
                             std::uint32_t read_buf_id, std::uint64_t q,
                             std::uint32_t one_iter_log2,
                             std::uint32_t num_iterations) {
#pragma HLS INTERFACE m_axi port = memory_words offset = slave bundle = gmem0
#pragma HLS INTERFACE m_axi port = read_block_select offset = slave bundle = gmem1
#pragma HLS INTERFACE m_axi port = base_conv_factors offset = slave bundle = gmem2
#pragma HLS INTERFACE m_axi port = out_rows offset = slave bundle = gmem3
#pragma HLS INTERFACE s_axilite port = memory_words bundle = control
#pragma HLS INTERFACE s_axilite port = read_block_select bundle = control
#pragma HLS INTERFACE s_axilite port = base_conv_factors bundle = control
#pragma HLS INTERFACE s_axilite port = out_rows bundle = control
#pragma HLS INTERFACE s_axilite port = num_blocks bundle = control
#pragma HLS INTERFACE s_axilite port = num_rows bundle = control
#pragma HLS INTERFACE s_axilite port = num_lanes bundle = control
#pragma HLS INTERFACE s_axilite port = read_buf_id bundle = control
#pragma HLS INTERFACE s_axilite port = q bundle = control
#pragma HLS INTERFACE s_axilite port = one_iter_log2 bundle = control
#pragma HLS INTERFACE s_axilite port = num_iterations bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
  run_change_rns_base(memory_words, read_block_select, base_conv_factors,
                      out_rows, num_blocks, num_rows, num_lanes, read_buf_id, q,
                      one_iter_log2, num_iterations);
}

void rns_resolve_core_top(const std::uint64_t *in_data,
                          const std::uint64_t *in_f,
                          const std::uint64_t *in_q,
                          const std::uint64_t *in_sum,
                          std::uint64_t *out_data, std::uint32_t row_size,
                          std::uint32_t block_num, std::uint32_t limb_bits,
                          std::uint32_t accumulate_flag) {
  run_rns_resolve(in_data, in_f, in_q, in_sum, out_data, row_size, block_num,
                  limb_bits, accumulate_flag);
}

void rns_resolve_axi_top(const std::uint64_t *in_data, const std::uint64_t *in_f,
                         const std::uint64_t *in_q, const std::uint64_t *in_sum,
                         std::uint64_t *out_data, std::uint32_t row_size,
                         std::uint32_t block_num, std::uint32_t limb_bits,
                         std::uint32_t accumulate_flag) {
#pragma HLS INTERFACE m_axi port = in_data offset = slave bundle = gmem0
#pragma HLS INTERFACE m_axi port = in_f offset = slave bundle = gmem1
#pragma HLS INTERFACE m_axi port = in_q offset = slave bundle = gmem2
#pragma HLS INTERFACE m_axi port = in_sum offset = slave bundle = gmem3
#pragma HLS INTERFACE m_axi port = out_data offset = slave bundle = gmem4
#pragma HLS INTERFACE s_axilite port = in_data bundle = control
#pragma HLS INTERFACE s_axilite port = in_f bundle = control
#pragma HLS INTERFACE s_axilite port = in_q bundle = control
#pragma HLS INTERFACE s_axilite port = in_sum bundle = control
#pragma HLS INTERFACE s_axilite port = out_data bundle = control
#pragma HLS INTERFACE s_axilite port = row_size bundle = control
#pragma HLS INTERFACE s_axilite port = block_num bundle = control
#pragma HLS INTERFACE s_axilite port = limb_bits bundle = control
#pragma HLS INTERFACE s_axilite port = accumulate_flag bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
  run_rns_resolve(in_data, in_f, in_q, in_sum, out_data, row_size, block_num,
                  limb_bits, accumulate_flag);
}

}  // extern "C"

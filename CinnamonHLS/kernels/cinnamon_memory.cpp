#include <cstdint>

#include "kernel_memory_module.hpp"

extern "C" {

void cinnamon_memory(const std::uint64_t *instructions, const std::uint64_t *inputs,
                     std::uint64_t *outputs, std::uint32_t instruction_count,
                     std::uint32_t input_count, std::uint32_t output_count,
                     std::uint32_t partition_id) {
#pragma HLS INTERFACE m_axi port = instructions offset = slave bundle = gmem0 depth = 16384 max_widen_bitwidth = 512 num_read_outstanding = 32 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = inputs offset = slave bundle = gmem1 depth = 65536 max_widen_bitwidth = 512 num_read_outstanding = 32 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = outputs offset = slave bundle = gmem2 depth = 16384 max_widen_bitwidth = 512 num_write_outstanding = 32 max_write_burst_length = 64
#pragma HLS INTERFACE s_axilite port = instructions bundle = control
#pragma HLS INTERFACE s_axilite port = inputs bundle = control
#pragma HLS INTERFACE s_axilite port = outputs bundle = control
#pragma HLS INTERFACE s_axilite port = instruction_count bundle = control
#pragma HLS INTERFACE s_axilite port = input_count bundle = control
#pragma HLS INTERFACE s_axilite port = output_count bundle = control
#pragma HLS INTERFACE s_axilite port = partition_id bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

  cinnamon_hls_kernel::execute_memory_module(instructions, inputs, outputs,
                                             instruction_count, input_count,
                                             output_count, partition_id);
}

}  // extern "C"

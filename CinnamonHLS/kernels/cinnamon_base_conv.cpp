#include <cstdint>

#include "kernel_base_conv_module.hpp"

extern "C" {

void cinnamon_base_conv(const std::uint64_t *instructions,
                        std::uint64_t *control_words,
                        std::uint64_t *payload_words,
                        std::uint64_t *outputs,
                        std::uint32_t instruction_count,
                        std::uint32_t control_count,
                        std::uint32_t payload_count,
                        std::uint32_t output_count,
                        std::uint32_t partition_id) {
#pragma HLS INTERFACE m_axi port = instructions offset = slave bundle = gmem0 depth = 4096
#pragma HLS INTERFACE m_axi port = control_words offset = slave bundle = gmem1 depth = 65536
#pragma HLS INTERFACE m_axi port = payload_words offset = slave bundle = gmem2 depth = 524288
#pragma HLS INTERFACE m_axi port = outputs offset = slave bundle = gmem3 depth = 4096
#pragma HLS INTERFACE s_axilite port = instructions bundle = control
#pragma HLS INTERFACE s_axilite port = control_words bundle = control
#pragma HLS INTERFACE s_axilite port = payload_words bundle = control
#pragma HLS INTERFACE s_axilite port = outputs bundle = control
#pragma HLS INTERFACE s_axilite port = instruction_count bundle = control
#pragma HLS INTERFACE s_axilite port = control_count bundle = control
#pragma HLS INTERFACE s_axilite port = payload_count bundle = control
#pragma HLS INTERFACE s_axilite port = output_count bundle = control
#pragma HLS INTERFACE s_axilite port = partition_id bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

  cinnamon_hls_kernel::execute_base_conv_module(
      instructions, control_words, payload_words, outputs, instruction_count,
      control_count, payload_count, output_count, partition_id);
}

}  // extern "C"

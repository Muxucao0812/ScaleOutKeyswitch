#pragma once

#include <cstdint>

namespace cinnamon_hls_kernel {

inline void execute_dispatch_module(const std::uint64_t *instructions,
                                    const std::uint64_t *inputs,
                                    std::uint64_t *outputs,
                                    std::uint32_t instruction_count,
                                    std::uint32_t input_count,
                                    std::uint32_t output_count,
                                    std::uint32_t partition_id) {
  const std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
  const std::uint64_t kFnvPrime = 1099511628211ULL;

  std::uint64_t checksum = kFnvOffset;
  for (std::uint32_t i = 0; i < instruction_count; ++i) {
#pragma HLS PIPELINE II = 1
    checksum ^= instructions[i];
    checksum *= kFnvPrime;
  }

  for (std::uint32_t i = 0; i < input_count; ++i) {
#pragma HLS PIPELINE II = 1
    checksum ^= (inputs[i] << 1U) | (inputs[i] >> 63U);
    checksum *= kFnvPrime;
  }

  if (output_count == 0U) {
    return;
  }

  outputs[0] = checksum;
  if (output_count > 1U) {
    outputs[1] = instruction_count;
  }
  if (output_count > 2U) {
    outputs[2] = input_count;
  }
  if (output_count > 3U) {
    outputs[3] = partition_id;
  }

  for (std::uint32_t i = 4U; i < output_count; ++i) {
#pragma HLS PIPELINE II = 1
    const std::uint32_t src_idx = i - 4U;
    outputs[i] = (src_idx < input_count) ? inputs[src_idx] : 0ULL;
  }
}

}  // namespace cinnamon_hls_kernel

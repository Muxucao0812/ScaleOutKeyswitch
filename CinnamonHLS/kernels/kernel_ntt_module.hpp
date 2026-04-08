#pragma once

#include "kernel_common.hpp"

namespace cinnamon_hls_kernel {

inline void execute_ntt_module(const std::uint64_t *instructions,
                               const std::uint64_t *inputs,
                               std::uint64_t *outputs,
                               std::uint32_t instruction_word_count,
                               std::uint32_t input_count,
                               std::uint32_t output_count,
                               std::uint32_t partition_id) {
  if (output_count == 0U) {
    return;
  }

  std::uint32_t register_count = 0U;
  std::uint64_t mod = 0U;
  std::uint32_t state_base = 0U;
  init_kernel_layout(inputs, input_count, register_count, mod, state_base);

  constexpr std::uint32_t kMaxRegisters = 2048U;
  constexpr std::uint32_t kMaxSpan = 64U;
  std::uint64_t state[kMaxRegisters];
#pragma HLS BIND_STORAGE variable = state type = ram_2p impl = bram

  const std::uint32_t bounded_register_count =
      (register_count > kMaxRegisters) ? kMaxRegisters : register_count;
  init_state_from_input(inputs, input_count, state, bounded_register_count, mod,
                        state_base);

  const std::uint32_t instruction_count =
      instruction_word_count / kInstructionWordStride;
  std::uint32_t executed = 0U;

  for (std::uint32_t i = 0; i < instruction_count; ++i) {
    const DecodedInstruction inst = decode_instruction(instructions, i);
    if (inst.opcode != kOpNtt && inst.opcode != kOpInt) {
      continue;
    }
    if (bounded_register_count == 0U) {
      ++executed;
      continue;
    }

    std::uint32_t span = abs_mod_u32(inst.imm0, kMaxSpan + 1U);
    if (span < 2U || !is_power_of_two(span)) {
      if (bounded_register_count >= 8U) {
        span = 8U;
      } else if (bounded_register_count >= 4U) {
        span = 4U;
      } else {
        span = 2U;
      }
    }
    while (span > bounded_register_count && span > 2U) {
      span >>= 1U;
    }

    std::uint64_t block[kMaxSpan];
#pragma HLS ARRAY_PARTITION variable = block cyclic factor = 8
    const std::uint32_t src_base = inst.src0 % bounded_register_count;
    const std::uint32_t dst_base = inst.dst % bounded_register_count;

    for (std::uint32_t j = 0U; j < span; ++j) {
#pragma HLS PIPELINE II = 1
      block[j] = state[(src_base + j) % bounded_register_count];
    }

    const bool inverse = (inst.opcode == kOpInt);
    if (inverse) {
      ntt_apply_dif(block, span, mod, inst.rns, true);
    } else {
      ntt_apply_dit(block, span, mod, inst.rns, false);
    }

    for (std::uint32_t j = 0U; j < span; ++j) {
#pragma HLS PIPELINE II = 1
      state[(dst_base + j) % bounded_register_count] = block[j] % mod;
    }

    ++executed;
  }

  write_module_outputs(outputs, output_count, state, bounded_register_count,
                       kModuleNtt, partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

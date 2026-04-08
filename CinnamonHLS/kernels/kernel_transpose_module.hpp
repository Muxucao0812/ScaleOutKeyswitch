#pragma once

#include "kernel_common.hpp"

namespace cinnamon_hls_kernel {

inline void execute_transpose_module(const std::uint64_t *instructions,
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
  constexpr std::uint32_t kMaxSide = 32U;
  constexpr std::uint32_t kMaxWords = kMaxSide * kMaxSide;
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
    if (inst.opcode != kOpRec && inst.opcode != kOpRsv) {
      continue;
    }
    if (bounded_register_count == 0U) {
      ++executed;
      continue;
    }

    std::uint32_t side_hint = inst.rns;
    if (side_hint < 2U) {
      const std::int64_t imm0 = inst.imm0 < 0 ? -inst.imm0 : inst.imm0;
      side_hint = static_cast<std::uint32_t>(imm0);
    }
    const std::uint32_t side =
        choose_transpose_side(bounded_register_count, side_hint);
    const std::uint32_t block_words = side * side;
    if (side < 2U || block_words > kMaxWords ||
        block_words > bounded_register_count) {
      ++executed;
      continue;
    }

    const std::uint32_t src_base = inst.src0 % bounded_register_count;
    const std::uint32_t dst_base = inst.dst % bounded_register_count;

    std::uint64_t in_cols[kMaxWords];
    std::uint64_t full_cols[kMaxWords];

    // Flattened column stream: in_cols[col * side + row]
    for (std::uint32_t j = 0U; j < block_words; ++j) {
#pragma HLS PIPELINE II = 1
      in_cols[j] = state[(src_base + j) % bounded_register_count] % mod;
    }

    // TransposeFull behavior.
    transpose_full_stream(in_cols, full_cols, side, side);

    for (std::uint32_t j = 0U; j < block_words; ++j) {
#pragma HLS PIPELINE II = 1
      state[(dst_base + j) % bounded_register_count] = full_cols[j] % mod;
    }

    ++executed;
  }

  write_module_outputs(outputs, output_count, state, bounded_register_count,
                       kModuleTranspose, partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

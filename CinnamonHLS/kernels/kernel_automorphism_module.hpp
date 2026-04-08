#pragma once

#include "kernel_common.hpp"

namespace cinnamon_hls_kernel {

inline void execute_automorphism_module(const std::uint64_t *instructions,
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
  constexpr std::uint32_t kMaxPermSpan = 16U;
  constexpr std::uint32_t kMaxMatrixWords = 16U * 16U;
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
    if (inst.opcode != kOpRot) {
      continue;
    }
    if (bounded_register_count == 0U) {
      ++executed;
      continue;
    }

    if (inst.aux != 0U) {
      const std::uint32_t span = select_perm_span(bounded_register_count, inst.rns);
      const std::uint32_t src_base = inst.src0 % bounded_register_count;
      const std::uint32_t dst_base = inst.dst % bounded_register_count;

      std::uint64_t block[kMaxPermSpan];
#pragma HLS ARRAY_PARTITION variable = block cyclic factor = 8
      // RTL testbench arrays are declared as [size-1:0] and initialized with
      // assignment patterns in descending textual order. Mirror this ordering
      // so C/HLS vectors align bit-exactly with golden SV expectations.
      for (std::uint32_t j = 0U; j < span; ++j) {
#pragma HLS PIPELINE II = 1
        const std::uint32_t src_offset = span - 1U - j;
        block[j] = state[(src_base + src_offset) % bounded_register_count] % mod;
      }

      if (span >= 2U && is_power_of_two(span)) {
        // AutomorphismPermutation RTL: Benes permutation over one row vector.
        apply_benes_network(block, span, sanitize_perm_info(inst.aux, span));
      } else {
        // Fallback only when span is invalid for Benes.
        for (std::uint32_t j = 0U; j < span; ++j) {
#pragma HLS PIPELINE II = 1
          block[j] = block[(j + 1U) % span];
        }
      }

      for (std::uint32_t j = 0U; j < span; ++j) {
#pragma HLS PIPELINE II = 1
        const std::uint32_t dst_offset = span - 1U - j;
        state[(dst_base + dst_offset) % bounded_register_count] = block[j] % mod;
      }

      // Optional matrix-path: overall Automorphism RTL
      // (permute -> transpose -> permute -> transpose). Enabled when
      // instruction provides a power-of-two side in rns and enough state words.
      const bool matrix_hint_valid =
          inst.rns >= 2U && is_power_of_two(inst.rns) && inst.rns <= 16U;
      const std::uint32_t side = matrix_hint_valid ? inst.rns : 0U;
      const std::uint32_t matrix_words = side * side;
      if (matrix_hint_valid && matrix_words <= kMaxMatrixWords &&
          matrix_words <= bounded_register_count) {
        const std::uint32_t mat_src = src_base;
        const std::uint32_t mat_dst = dst_base;
        std::uint64_t matrix[kMaxMatrixWords];
        for (std::uint32_t j = 0U; j < matrix_words; ++j) {
#pragma HLS PIPELINE II = 1
          matrix[j] = state[(mat_src + j) % bounded_register_count] % mod;
        }
        std::uint64_t row_perm =
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(inst.imm0));
        row_perm |=
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(inst.imm1))
             << 32U);
        if (row_perm == 0U) {
          row_perm = inst.aux;
        }
        apply_automorphism_matrix(matrix, side, inst.aux, row_perm);
        for (std::uint32_t j = 0U; j < matrix_words; ++j) {
#pragma HLS PIPELINE II = 1
          state[(mat_dst + j) % bounded_register_count] = matrix[j] % mod;
        }
      }
    } else {
      std::uint32_t rot = abs_mod_u32(inst.imm0, bounded_register_count);
      if (rot == 0U) {
        rot = inst.rns % bounded_register_count;
      }
      const std::uint32_t src_idx = inst.src0 % bounded_register_count;
      const std::uint32_t mapped_idx = (src_idx + rot) % bounded_register_count;
      store_register(state, bounded_register_count, inst.dst, state[mapped_idx],
                     mod);
    }

    ++executed;
  }

  write_module_outputs(outputs, output_count, state, bounded_register_count,
                       kModuleAutomorphism, partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

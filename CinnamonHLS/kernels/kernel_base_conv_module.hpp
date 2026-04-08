#pragma once

#include "kernel_common.hpp"

namespace cinnamon_hls_kernel {

inline void execute_base_conv_module(const std::uint64_t *instructions,
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
  std::uint64_t state[kMaxRegisters];
#pragma HLS BIND_STORAGE variable = state type = ram_2p impl = bram

  const std::uint32_t bounded_register_count =
      (register_count > kMaxRegisters) ? kMaxRegisters : register_count;
  init_state_from_input(inputs, input_count, state, bounded_register_count, mod,
                        state_base);

  constexpr std::uint32_t kMaxBCU = 8U;
  std::uint64_t bcu_acc[kMaxBCU];
  std::uint64_t bcu_factor_seed[kMaxBCU][kMaxBCU];
#pragma HLS ARRAY_PARTITION variable = bcu_acc complete
#pragma HLS ARRAY_PARTITION variable = bcu_factor_seed complete dim = 2
  for (std::uint32_t b = 0U; b < kMaxBCU; ++b) {
    bcu_acc[b] = 0U;
    for (std::uint32_t k = 0U; k < kMaxBCU; ++k) {
      bcu_factor_seed[b][k] = static_cast<std::uint64_t>(k + 1U) % mod;
    }
  }

  const std::uint32_t instruction_count =
      instruction_word_count / kInstructionWordStride;
  std::uint32_t executed = 0U;

  for (std::uint32_t i = 0; i < instruction_count; ++i) {
#pragma HLS PIPELINE II = 1
    const DecodedInstruction inst = decode_instruction(instructions, i);
    if (!is_base_conv_opcode(inst.opcode)) {
      continue;
    }

    const std::uint64_t src0 = load_operand(state, bounded_register_count, inst,
                                            false, mod, inputs, input_count,
                                            state_base);
    const std::uint64_t src1 = load_operand(state, bounded_register_count, inst,
                                            true, mod, inputs, input_count,
                                            state_base);

    std::uint64_t result = src0;
    if (inst.opcode == kOpBci) {
      const std::uint32_t bcu = decode_bcu_id(inst.dst) % kMaxBCU;
      const std::uint32_t dst_count =
          static_cast<std::uint32_t>(inst.aux & 0xFFFFULL);
      const std::uint32_t src_count =
          static_cast<std::uint32_t>((inst.aux >> 16U) & 0xFFFFULL);
      const std::uint32_t first_dst =
          static_cast<std::uint32_t>((inst.aux >> 32U) & 0xFFFFULL);
      const std::uint32_t first_src =
          static_cast<std::uint32_t>((inst.aux >> 48U) & 0xFFFFULL);
      const std::uint64_t seed0 = signed_mod(inst.imm0, mod);
      const std::uint64_t seed1 = signed_mod(inst.imm1, mod);
      const std::uint64_t mix =
          (static_cast<std::uint64_t>(dst_count + src_count + first_dst +
                                      first_src) +
           static_cast<std::uint64_t>(inst.rns)) %
          mod;
      for (std::uint32_t k = 0U; k < kMaxBCU; ++k) {
#pragma HLS UNROLL
        const std::uint64_t lane_seed =
            (mix + static_cast<std::uint64_t>(k + 1U)) % mod;
        bcu_factor_seed[bcu][k] =
            mod_add(mod_add(seed0, seed1, mod), lane_seed, mod);
      }
      bcu_acc[bcu] = 0U;
      result = bcu_factor_seed[bcu][0];
    } else if (inst.opcode == kOpPl1 || inst.opcode == kOpBcw) {
      const std::uint32_t bcu = decode_bcu_id(inst.dst) % kMaxBCU;
      const std::uint32_t factor_idx = inst.rns % kMaxBCU;
      std::uint64_t factor = bcu_factor_seed[bcu][factor_idx] % mod;
      if (factor == 0U) {
        factor = 1U;
      }
      const std::uint64_t mac = montgomery_mul_ntt_friendly(src0, factor, mod);
      std::uint64_t acc = mod_add(bcu_acc[bcu], mac, mod);
      if (inst.opcode == kOpBcw) {
        acc = mod_add(acc, signed_mod(inst.imm0, mod), mod);
      }
      bcu_acc[bcu] = acc;
      result = acc;
    } else if (inst.opcode == kOpRsi) {
      const std::uint32_t regs_to_reset =
          static_cast<std::uint32_t>(inst.imm0 < 0 ? 0 : inst.imm0);
      if (is_general_register_operand(inst.dst, bounded_register_count)) {
        state[inst.dst] = 0U;
      }
      if (regs_to_reset > 1U &&
          is_general_register_operand(inst.src0, bounded_register_count)) {
        state[inst.src0] = 0U;
      }
      if (regs_to_reset > 2U &&
          is_general_register_operand(inst.src1, bounded_register_count)) {
        state[inst.src1] = 0U;
      }
      result = 0U;
    } else if (inst.opcode == kOpRsv) {
      const std::uint32_t bcu =
          static_cast<std::uint32_t>((inst.aux >> 48U) & 0xFFFFULL) % kMaxBCU;
      result = mod_add(src0, bcu_acc[bcu], mod);
      if (is_general_register_operand(inst.src1, bounded_register_count)) {
        result = mod_add(result, state[inst.src1] % mod, mod);
      }
    } else if (inst.opcode == kOpMod) {
      result = mod_sub(src0, src1, mod);
    }

    if (is_general_register_operand(inst.dst, bounded_register_count)) {
      store_register(state, bounded_register_count, inst.dst, result, mod);
    }
    ++executed;
  }

  write_module_outputs(outputs, output_count, state, bounded_register_count,
                       kModuleBaseConv, partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

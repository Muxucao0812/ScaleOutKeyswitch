#pragma once

#include "kernel_common.hpp"

namespace cinnamon_hls_kernel {

inline void execute_arithmetic_module(const std::uint64_t *instructions,
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

  const std::uint32_t instruction_count =
      instruction_word_count / kInstructionWordStride;
  std::uint32_t executed = 0U;

  for (std::uint32_t i = 0; i < instruction_count; ++i) {
#pragma HLS PIPELINE II = 1
    const DecodedInstruction inst = decode_instruction(instructions, i);
    if (!is_arithmetic_opcode(inst.opcode)) {
      continue;
    }

    const std::uint64_t src0 = load_operand(state, bounded_register_count, inst,
                                            false, mod, inputs, input_count,
                                            state_base);
    const std::uint64_t src1 = load_operand(state, bounded_register_count, inst,
                                            true, mod, inputs, input_count,
                                            state_base);

    std::uint64_t result = src0;
    if (inst.opcode == kOpAdd || inst.opcode == kOpAds) {
      result = mod_add(src0, src1, mod);
    } else if (inst.opcode == kOpSub || inst.opcode == kOpSus) {
      result = mod_sub(src0, src1, mod);
    } else if (inst.opcode == kOpSud) {
      const std::uint64_t diff = mod_sub(src0, src1, mod);
      result = (diff & 1ULL) ? ((diff + mod) >> 1U) % mod : (diff >> 1U) % mod;
    } else if (inst.opcode == kOpCon) {
      result = src0 % mod;
    } else if (inst.opcode == kOpNeg) {
      result = (src0 == 0U) ? 0U : (mod - (src0 % mod));
    }

    store_register(state, bounded_register_count, inst.dst, result, mod);
    ++executed;
  }

  write_module_outputs(outputs, output_count, state, bounded_register_count,
                       kModuleArithmetic, partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

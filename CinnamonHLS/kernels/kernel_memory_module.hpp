#pragma once

#include "kernel_payload_common.hpp"

namespace cinnamon_hls_kernel {

inline std::uint32_t load_memory_handle(const std::uint64_t *control,
                                        const PayloadLayout &layout,
                                        const std::uint32_t *register_handles,
                                        const DecodedInstruction &inst,
                                        std::uint32_t &status) {
  const bool src0_is_imm =
      ((inst.flags & 0x1U) != 0U) || (inst.src0 == kImmOperandId);
  if (src0_is_imm) {
    status = kPayloadStatusUnsupportedImmediate;
    return kPayloadInvalidHandle;
  }
  if (inst.src0 == kTokenOperandId) {
    const std::uint32_t handle_id = payload_pair_lookup(
        control, layout.token_directory_offset, layout.token_count, inst.aux);
    if (handle_id == kPayloadInvalidHandle) {
      status = kPayloadStatusMissingToken;
    }
    return handle_id;
  }
  if (layout.register_count == 0U) {
    return kPayloadInvalidHandle;
  }
  return register_handles[inst.src0 % layout.register_count];
}

inline void execute_memory_module(const std::uint64_t *instructions,
                                  std::uint64_t *control,
                                  std::uint64_t *payload,
                                  std::uint64_t *outputs,
                                  std::uint32_t instruction_word_count,
                                  std::uint32_t control_count,
                                  std::uint32_t payload_count,
                                  std::uint32_t output_count,
                                  std::uint32_t partition_id) {
  (void)payload;
  (void)payload_count;
  if (output_count == 0U) {
    return;
  }

  PayloadLayout layout;
  std::uint32_t status = kPayloadStatusOk;
  if (!parse_payload_layout(control, control_count, layout, status)) {
    constexpr std::uint32_t kEmptyRegisters = 0U;
    std::uint32_t empty_state[1] = {0U};
    write_payload_module_outputs(outputs, output_count, status, empty_state,
                                 kEmptyRegisters, kModuleMemory, partition_id,
                                 0U);
    return;
  }

  constexpr std::uint32_t kMaxRegisters = 2048U;
  const std::uint32_t bounded_register_count =
      (layout.register_count > kMaxRegisters) ? kMaxRegisters : layout.register_count;
  std::uint32_t register_handles[kMaxRegisters];
#pragma HLS BIND_STORAGE variable = register_handles type = ram_2p impl = bram
  for (std::uint32_t i = 0; i < bounded_register_count; ++i) {
#pragma HLS PIPELINE II = 1
    register_handles[i] = static_cast<std::uint32_t>(
        control[layout.register_handles_offset + i]);
  }

  const std::uint32_t instruction_count =
      instruction_word_count / kInstructionWordStride;
  std::uint32_t executed = 0U;
  for (std::uint32_t i = 0; i < instruction_count; ++i) {
    const DecodedInstruction inst = decode_instruction(instructions, i);
    if (!is_memory_opcode(inst.opcode)) {
      continue;
    }

    if (inst.opcode == kOpLoad || inst.opcode == kOpLoas || inst.opcode == kOpEvg ||
        inst.opcode == kOpRec) {
      const std::uint32_t handle_id =
          load_memory_handle(control, layout, register_handles, inst, status);
      if (status != kPayloadStatusOk) {
        break;
      }
      if (bounded_register_count != 0U) {
        register_handles[inst.dst % bounded_register_count] = handle_id;
      }
      ++executed;
      continue;
    }

    if (inst.opcode == kOpMov) {
      const std::uint32_t handle_id =
          load_memory_handle(control, layout, register_handles, inst, status);
      if (status != kPayloadStatusOk) {
        break;
      }
      if (bounded_register_count != 0U) {
        register_handles[inst.dst % bounded_register_count] = handle_id;
      }
      ++executed;
      continue;
    }

    if (inst.opcode == kOpStore || inst.opcode == kOpSpill || inst.opcode == kOpSnd) {
      if (bounded_register_count == 0U) {
        status = kPayloadStatusInvalidHandle;
        break;
      }
      const std::uint32_t handle_id =
          register_handles[inst.src0 % bounded_register_count];
      if (handle_id == kPayloadInvalidHandle) {
        status = kPayloadStatusInvalidHandle;
        break;
      }
      const bool updated = payload_pair_update(
          control, layout.output_directory_offset, layout.output_token_count,
          inst.aux, handle_id);
      if (!updated) {
        status = kPayloadStatusMissingOutputToken;
        break;
      }
      ++executed;
      continue;
    }

    status = kPayloadStatusUnsupportedOpcode;
    break;
  }

  for (std::uint32_t i = 0; i < bounded_register_count; ++i) {
#pragma HLS PIPELINE II = 1
    control[layout.register_handles_offset + i] = register_handles[i];
  }

  write_payload_module_outputs(outputs, output_count, status, register_handles,
                               bounded_register_count, kModuleMemory,
                               partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

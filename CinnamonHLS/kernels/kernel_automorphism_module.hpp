#pragma once

#include "kernel_payload_common.hpp"

namespace cinnamon_hls_kernel {

inline std::uint32_t resolve_automorphism_operand_handle(
    const std::uint64_t *control, const PayloadLayout &layout,
    const std::uint32_t *register_handles, const DecodedInstruction &inst,
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
    status = kPayloadStatusInvalidHandle;
    return kPayloadInvalidHandle;
  }
  const std::uint32_t handle_id = register_handles[inst.src0 % layout.register_count];
  if (handle_id == kPayloadInvalidHandle) {
    status = kPayloadStatusInvalidHandle;
  }
  return handle_id;
}

inline void execute_automorphism_module(const std::uint64_t *instructions,
                                        std::uint64_t *control,
                                        std::uint64_t *payload,
                                        std::uint64_t *outputs,
                                        std::uint32_t instruction_word_count,
                                        std::uint32_t control_count,
                                        std::uint32_t payload_count,
                                        std::uint32_t output_count,
                                        std::uint32_t partition_id) {
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
                                 kEmptyRegisters, kModuleAutomorphism,
                                 partition_id, 0U);
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
    if (inst.opcode != kOpRot) {
      continue;
    }

    const std::uint32_t src_handle = resolve_automorphism_operand_handle(
        control, layout, register_handles, inst, status);
    if (status != kPayloadStatusOk) {
      break;
    }

    const std::uint32_t rns_base_id = inst.rns;
    const std::uint32_t out_handle = payload_allocate_handle(
        control, layout, rns_base_id,
        (payload_handle_flags(control, layout, src_handle) & kPayloadFlagIsNtt) != 0U,
        status);
    if (status != kPayloadStatusOk) {
      break;
    }

    const std::uint32_t src_offset =
        payload_handle_coeff_offset(layout, src_handle);
    const std::uint32_t out_offset =
        payload_handle_coeff_offset(layout, out_handle);
    const std::int32_t rotation_amount =
        (inst.imm0 == 0) ? static_cast<std::int32_t>(inst.rns) : static_cast<std::int32_t>(inst.imm0);
    const std::uint32_t galois_elt =
        galois_elt_from_step(layout.coeff_count, rotation_amount);
    if (galois_elt == 0U) {
      status = kPayloadStatusUnsupportedOpcode;
      break;
    }
    apply_galois_ntt_permutation(&payload[src_offset], &payload[out_offset],
                                 layout.coeff_count, galois_elt);

    if (bounded_register_count != 0U) {
      register_handles[inst.dst % bounded_register_count] = out_handle;
    }
    ++executed;
  }

  for (std::uint32_t i = 0; i < bounded_register_count; ++i) {
#pragma HLS PIPELINE II = 1
    control[layout.register_handles_offset + i] = register_handles[i];
  }

  write_payload_module_outputs(outputs, output_count, status, register_handles,
                               bounded_register_count, kModuleAutomorphism,
                               partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

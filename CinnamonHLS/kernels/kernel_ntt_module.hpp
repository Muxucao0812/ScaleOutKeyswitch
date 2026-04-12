#pragma once

#include "kernel_payload_common.hpp"

namespace cinnamon_hls_kernel {

inline bool is_bcu_output_operand(std::uint32_t operand) {
  return ((operand & 0x800U) == 0x800U) && ((operand & 0xF00U) != 0xA00U);
}

inline std::uint32_t decode_bcu_output_id(std::uint32_t operand) {
  return ((operand & 0x7FFU) >> 7U) & 0x1FU;
}

inline std::uint32_t decode_bcu_output_index(std::uint32_t operand) {
  return operand & 0x7FU;
}

inline std::uint32_t resolve_ntt_operand_handle(
    const std::uint64_t *control, const PayloadLayout &layout,
    const PayloadExtraLayout &extra, const std::uint32_t *register_handles,
    const DecodedInstruction &inst, std::uint32_t &status) {
  const std::uint32_t src = inst.src0;
  const bool is_imm = ((inst.flags & 0x1U) != 0U) || (src == kImmOperandId);
  if (is_imm) {
    status = kPayloadStatusUnsupportedImmediate;
    return kPayloadInvalidHandle;
  }

  if (src == kTokenOperandId) {
    const std::uint32_t handle_id = payload_pair_lookup(
        control, layout.token_directory_offset, layout.token_count, inst.aux);
    if (handle_id == kPayloadInvalidHandle) {
      status = kPayloadStatusMissingToken;
    }
    return handle_id;
  }

  if (is_bcu_output_operand(src)) {
    if (!extra.valid) {
      status = kPayloadStatusMissingBcuValue;
      return kPayloadInvalidHandle;
    }
    const std::uint32_t bcu_id = decode_bcu_output_id(src);
    const std::uint32_t output_index = decode_bcu_output_index(src);
    if (bcu_id >= extra.bcu_unit_count || output_index >= extra.bcu_output_capacity) {
      status = kPayloadStatusMissingBcuValue;
      return kPayloadInvalidHandle;
    }
    const std::uint32_t table_cursor =
        extra.bcu_table_offset + bcu_id * extra.bcu_output_capacity + output_index;
    const std::uint32_t handle_id = static_cast<std::uint32_t>(control[table_cursor]);
    if (handle_id == kPayloadInvalidHandle) {
      status = kPayloadStatusMissingBcuValue;
    }
    return handle_id;
  }

  if (layout.register_count == 0U) {
    status = kPayloadStatusInvalidHandle;
    return kPayloadInvalidHandle;
  }
  const std::uint32_t handle_id = register_handles[src % layout.register_count];
  if (handle_id == kPayloadInvalidHandle) {
    status = kPayloadStatusInvalidHandle;
  }
  return handle_id;
}

inline void execute_ntt_module(const std::uint64_t *instructions,
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
                                 kEmptyRegisters, kModuleNtt, partition_id, 0U);
    return;
  }
  PayloadExtraLayout extra;
  parse_payload_extra_layout(control, control_count, layout, extra, status);
  if (status != kPayloadStatusOk) {
    constexpr std::uint32_t kEmptyRegisters = 0U;
    std::uint32_t empty_state[1] = {0U};
    write_payload_module_outputs(outputs, output_count, status, empty_state,
                                 kEmptyRegisters, kModuleNtt, partition_id, 0U);
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
    if (inst.opcode != kOpNtt && inst.opcode != kOpInt) {
      continue;
    }

    const std::uint32_t src_handle = resolve_ntt_operand_handle(
        control, layout, extra, register_handles, inst, status);
    if (status != kPayloadStatusOk) {
      break;
    }

    const std::uint32_t target_rns_base_id = inst.rns;
    const std::uint64_t mod =
        payload_lookup_modulus(control, layout, target_rns_base_id);
    if (mod == 0U) {
      status = kPayloadStatusMissingModulus;
      break;
    }

    const bool inverse = (inst.opcode == kOpInt);
    const std::uint32_t out_handle = payload_allocate_handle(
        control, layout, target_rns_base_id, !inverse, status);
    if (status != kPayloadStatusOk) {
      break;
    }

    const std::uint32_t src_offset =
        payload_handle_coeff_offset(layout, src_handle);
    const std::uint32_t out_offset =
        payload_handle_coeff_offset(layout, out_handle);
    for (std::uint32_t coeff = 0; coeff < layout.coeff_count; ++coeff) {
#pragma HLS PIPELINE II = 1
      payload[out_offset + coeff] = payload[src_offset + coeff] % mod;
    }
    ntt_apply_negacyclic_four_step(&payload[out_offset], layout.coeff_count, mod,
                                   target_rns_base_id, inverse);

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
                               bounded_register_count, kModuleNtt, partition_id,
                               executed);
}

}  // namespace cinnamon_hls_kernel

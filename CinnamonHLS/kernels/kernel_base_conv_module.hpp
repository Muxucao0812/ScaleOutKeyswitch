#pragma once

#include "kernel_payload_common.hpp"

namespace cinnamon_hls_kernel {

inline bool payload_decode_bci_entry(const std::uint64_t *control,
                                     std::uint32_t control_count,
                                     std::uint32_t cursor,
                                     std::uint32_t &line_crc,
                                     std::uint32_t &bcu_id,
                                     std::uint32_t &src_count,
                                     std::uint32_t &dst_count,
                                     std::uint32_t &src_base_offset,
                                     std::uint32_t &dst_base_offset,
                                     std::uint32_t &factor_offset,
                                     std::uint32_t &next_cursor) {
  if ((cursor + 4U) > control_count) {
    return false;
  }
  line_crc = static_cast<std::uint32_t>(control[cursor + 0U]);
  bcu_id = static_cast<std::uint32_t>(control[cursor + 1U]);
  src_count = static_cast<std::uint32_t>(control[cursor + 2U]);
  dst_count = static_cast<std::uint32_t>(control[cursor + 3U]);
  src_base_offset = cursor + 4U;
  dst_base_offset = src_base_offset + src_count;
  factor_offset = dst_base_offset + dst_count;
  const std::uint32_t factor_count = src_count * dst_count;
  next_cursor = factor_offset + factor_count;
  if (next_cursor > control_count) {
    return false;
  }
  return true;
}

inline bool payload_find_bci_entry(const std::uint64_t *control,
                                   std::uint32_t control_count,
                                   const PayloadExtraLayout &extra,
                                   std::uint32_t target_line_crc,
                                   std::uint32_t &entry_cursor,
                                   std::uint32_t &entry_bcu_id,
                                   std::uint32_t &src_count,
                                   std::uint32_t &dst_count,
                                   std::uint32_t &src_base_offset,
                                   std::uint32_t &dst_base_offset,
                                   std::uint32_t &factor_offset) {
  std::uint32_t cursor = extra.bci_entries_offset;
  for (std::uint32_t i = 0; i < extra.bci_entry_count; ++i) {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 512
    std::uint32_t line_crc = 0U;
    std::uint32_t bcu_id = 0U;
    std::uint32_t next_cursor = 0U;
    if (!payload_decode_bci_entry(control, control_count, cursor, line_crc, bcu_id,
                                  src_count, dst_count, src_base_offset,
                                  dst_base_offset, factor_offset, next_cursor)) {
      return false;
    }
    if (line_crc == target_line_crc) {
      entry_cursor = cursor;
      entry_bcu_id = bcu_id;
      return true;
    }
    cursor = next_cursor;
  }
  return false;
}

inline std::uint32_t resolve_base_conv_operand_handle(
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

inline void execute_base_conv_module(const std::uint64_t *instructions,
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
                                 kEmptyRegisters, kModuleBaseConv, partition_id,
                                 0U);
    return;
  }
  PayloadExtraLayout extra;
  if (!parse_payload_extra_layout(control, control_count, layout, extra, status)) {
    if (status == kPayloadStatusOk) {
      status = kPayloadStatusBadExtraLayout;
    }
    constexpr std::uint32_t kEmptyRegisters = 0U;
    std::uint32_t empty_state[1] = {0U};
    write_payload_module_outputs(outputs, output_count, status, empty_state,
                                 kEmptyRegisters, kModuleBaseConv, partition_id,
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

  constexpr std::uint32_t kMaxBcuUnits = 8U;
  std::uint32_t active_line_crc[kMaxBcuUnits];
  for (std::uint32_t i = 0; i < kMaxBcuUnits; ++i) {
#pragma HLS PIPELINE II = 1
    active_line_crc[i] =
        (i < extra.bcu_active_count)
            ? static_cast<std::uint32_t>(control[extra.bcu_active_offset + i])
            : 0U;
  }

  const std::uint32_t instruction_count =
      instruction_word_count / kInstructionWordStride;
  std::uint32_t executed = 0U;
  for (std::uint32_t i = 0; i < instruction_count; ++i) {
    const DecodedInstruction inst = decode_instruction(instructions, i);
    if (!is_base_conv_opcode(inst.opcode)) {
      continue;
    }

    if (inst.opcode == kOpBci) {
      const std::uint32_t bcu_id = decode_bcu_id(inst.dst);
      std::uint32_t entry_cursor = 0U;
      std::uint32_t entry_bcu_id = 0U;
      std::uint32_t src_count = 0U;
      std::uint32_t dst_count = 0U;
      std::uint32_t src_base_offset = 0U;
      std::uint32_t dst_base_offset = 0U;
      std::uint32_t factor_offset = 0U;
      const bool found = payload_find_bci_entry(
          control, control_count, extra, static_cast<std::uint32_t>(inst.line_crc),
          entry_cursor, entry_bcu_id, src_count, dst_count, src_base_offset,
          dst_base_offset, factor_offset);
      if (!found || entry_bcu_id != bcu_id || bcu_id >= extra.bcu_unit_count ||
          bcu_id >= kMaxBcuUnits) {
        status = kPayloadStatusMissingBciConfig;
        break;
      }
      active_line_crc[bcu_id] = static_cast<std::uint32_t>(inst.line_crc);
      for (std::uint32_t dst_idx = 0U; dst_idx < extra.bcu_output_capacity; ++dst_idx) {
#pragma HLS PIPELINE II = 1
        control[extra.bcu_table_offset + bcu_id * extra.bcu_output_capacity + dst_idx] =
            static_cast<std::uint64_t>(kPayloadInvalidHandle);
      }
      ++executed;
      continue;
    }

    if (inst.opcode != kOpPl1 && inst.opcode != kOpBcw) {
      status = kPayloadStatusUnsupportedOpcode;
      break;
    }

    const std::uint32_t bcu_id = decode_bcu_id(inst.dst);
    if (bcu_id >= extra.bcu_unit_count || bcu_id >= kMaxBcuUnits) {
      status = kPayloadStatusMissingBciConfig;
      break;
    }
    const std::uint32_t line_crc = active_line_crc[bcu_id];
    if (line_crc == 0U) {
      status = kPayloadStatusMissingBciConfig;
      break;
    }

    std::uint32_t entry_cursor = 0U;
    std::uint32_t entry_bcu_id = 0U;
    std::uint32_t src_count = 0U;
    std::uint32_t dst_count = 0U;
    std::uint32_t src_base_offset = 0U;
    std::uint32_t dst_base_offset = 0U;
    std::uint32_t factor_offset = 0U;
    const bool found = payload_find_bci_entry(
        control, control_count, extra, line_crc, entry_cursor, entry_bcu_id,
        src_count, dst_count, src_base_offset, dst_base_offset, factor_offset);
    if (!found || entry_bcu_id != bcu_id) {
      status = kPayloadStatusMissingBciConfig;
      break;
    }

    const std::uint32_t src_handle = resolve_base_conv_operand_handle(
        control, layout, register_handles, inst, status);
    if (status != kPayloadStatusOk) {
      break;
    }
    const std::uint32_t src_rns_base_id = inst.rns;
    std::uint32_t src_pos = src_count;
    for (std::uint32_t k = 0U; k < src_count; ++k) {
#pragma HLS PIPELINE II = 1
      if (static_cast<std::uint32_t>(control[src_base_offset + k]) ==
          src_rns_base_id) {
        src_pos = k;
      }
    }
    if (src_pos == src_count) {
      status = kPayloadStatusMissingBciConfig;
      break;
    }

    std::uint32_t normalized_src_handle = src_handle;
    if (inst.opcode == kOpPl1 &&
        (payload_handle_flags(control, layout, src_handle) & kPayloadFlagIsNtt) != 0U) {
      const std::uint64_t src_mod =
          payload_lookup_modulus(control, layout, src_rns_base_id);
      if (src_mod == 0U) {
        status = kPayloadStatusMissingModulus;
        break;
      }
      normalized_src_handle =
          payload_allocate_handle(control, layout, src_rns_base_id, false, status);
      if (status != kPayloadStatusOk) {
        break;
      }
      const std::uint32_t src_offset = payload_handle_coeff_offset(layout, src_handle);
      const std::uint32_t out_offset =
          payload_handle_coeff_offset(layout, normalized_src_handle);
      for (std::uint32_t coeff = 0; coeff < layout.coeff_count; ++coeff) {
#pragma HLS PIPELINE II = 1
        payload[out_offset + coeff] = payload[src_offset + coeff] % src_mod;
      }
      ntt_apply_negacyclic_four_step(&payload[out_offset], layout.coeff_count, src_mod,
                                     src_rns_base_id, true);
    }

    const std::uint32_t src_offset =
        payload_handle_coeff_offset(layout, normalized_src_handle);
    for (std::uint32_t dst_idx = 0U; dst_idx < dst_count; ++dst_idx) {
      if (dst_idx >= extra.bcu_output_capacity) {
        status = kPayloadStatusBadExtraLayout;
        break;
      }
      const std::uint32_t dst_base_id =
          static_cast<std::uint32_t>(control[dst_base_offset + dst_idx]);
      const std::uint64_t mod = payload_lookup_modulus(control, layout, dst_base_id);
      if (mod == 0U) {
        status = kPayloadStatusMissingModulus;
        break;
      }
      const std::uint32_t factor_idx = src_pos * dst_count + dst_idx;
      const std::uint64_t factor = control[factor_offset + factor_idx] % mod;
      const std::uint32_t table_cursor =
          extra.bcu_table_offset + bcu_id * extra.bcu_output_capacity + dst_idx;
      std::uint32_t out_handle = static_cast<std::uint32_t>(control[table_cursor]);
      if (out_handle == kPayloadInvalidHandle) {
        out_handle = payload_allocate_handle(control, layout, dst_base_id, false, status);
        if (status != kPayloadStatusOk) {
          break;
        }
        control[table_cursor] = out_handle;
        const std::uint32_t out_offset =
            payload_handle_coeff_offset(layout, out_handle);
        for (std::uint32_t coeff = 0; coeff < layout.coeff_count; ++coeff) {
#pragma HLS PIPELINE II = 1
          payload[out_offset + coeff] = 0U;
        }
      } else if (payload_handle_rns_base_id(control, layout, out_handle) != dst_base_id) {
        status = kPayloadStatusInvalidHandle;
        break;
      }

      const std::uint32_t out_offset = payload_handle_coeff_offset(layout, out_handle);
      for (std::uint32_t coeff = 0; coeff < layout.coeff_count; ++coeff) {
#pragma HLS PIPELINE II = 1
        const std::uint64_t src_coeff = payload[src_offset + coeff] % mod;
        const std::uint64_t scaled = mod_mul(src_coeff, factor, mod);
        payload[out_offset + coeff] =
            mod_add(payload[out_offset + coeff] % mod, scaled, mod);
      }
    }
    if (status != kPayloadStatusOk) {
      break;
    }
    ++executed;
  }

  for (std::uint32_t i = 0; i < bounded_register_count; ++i) {
#pragma HLS PIPELINE II = 1
    control[layout.register_handles_offset + i] = register_handles[i];
  }
  for (std::uint32_t i = 0; i < extra.bcu_active_count && i < kMaxBcuUnits; ++i) {
#pragma HLS PIPELINE II = 1
    control[extra.bcu_active_offset + i] = active_line_crc[i];
  }

  write_payload_module_outputs(outputs, output_count, status, register_handles,
                               bounded_register_count, kModuleBaseConv,
                               partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

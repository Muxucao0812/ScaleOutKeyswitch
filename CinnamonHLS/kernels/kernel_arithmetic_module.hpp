#pragma once

#include "kernel_payload_common.hpp"

namespace cinnamon_hls_kernel {

inline bool is_bcu_output_operand_arith(std::uint32_t operand) {
  return ((operand & 0x800U) == 0x800U) && ((operand & 0xF00U) != 0xA00U);
}

inline std::uint32_t decode_bcu_output_id_arith(std::uint32_t operand) {
  return ((operand & 0x7FFU) >> 7U) & 0x1FU;
}

inline std::uint32_t decode_bcu_output_index_arith(std::uint32_t operand) {
  return operand & 0x7FU;
}

inline bool payload_decode_bci_entry_arith(const std::uint64_t *control,
                                           std::uint32_t control_count,
                                           std::uint32_t cursor,
                                           std::uint32_t &line_crc,
                                           std::uint32_t &bcu_id,
                                           std::uint32_t &src_count,
                                           std::uint32_t &src_base_offset,
                                           std::uint32_t &next_cursor) {
  if ((cursor + 4U) > control_count) {
    return false;
  }
  line_crc = static_cast<std::uint32_t>(control[cursor + 0U]);
  bcu_id = static_cast<std::uint32_t>(control[cursor + 1U]);
  src_count = static_cast<std::uint32_t>(control[cursor + 2U]);
  const std::uint32_t dst_count = static_cast<std::uint32_t>(control[cursor + 3U]);
  src_base_offset = cursor + 4U;
  const std::uint32_t dst_base_offset = src_base_offset + src_count;
  const std::uint32_t factor_offset = dst_base_offset + dst_count;
  const std::uint32_t factor_count = src_count * dst_count;
  next_cursor = factor_offset + factor_count;
  if (next_cursor > control_count) {
    return false;
  }
  return true;
}

inline bool payload_find_bci_sources_arith(const std::uint64_t *control,
                                           std::uint32_t control_count,
                                           const PayloadExtraLayout &extra,
                                           std::uint32_t target_line_crc,
                                           std::uint32_t target_bcu_id,
                                           std::uint32_t &src_count,
                                           std::uint32_t &src_base_offset) {
  std::uint32_t cursor = extra.bci_entries_offset;
  for (std::uint32_t i = 0U; i < extra.bci_entry_count; ++i) {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 512
    std::uint32_t line_crc = 0U;
    std::uint32_t bcu_id = 0U;
    std::uint32_t next_cursor = 0U;
    if (!payload_decode_bci_entry_arith(control, control_count, cursor, line_crc,
                                        bcu_id, src_count, src_base_offset,
                                        next_cursor)) {
      return false;
    }
    if (line_crc == target_line_crc && bcu_id == target_bcu_id) {
      return true;
    }
    cursor = next_cursor;
  }
  return false;
}

inline std::uint32_t resolve_operand_handle(const std::uint64_t *control,
                                            const PayloadLayout &layout,
                                            const PayloadExtraLayout &extra,
                                            const std::uint32_t *register_handles,
                                            const DecodedInstruction &inst,
                                            bool use_src1,
                                            std::uint32_t &status) {
  const std::uint32_t src = use_src1 ? inst.src1 : inst.src0;
  const std::uint32_t imm_flag_bit = use_src1 ? 1U : 0U;
  const bool is_imm = ((inst.flags >> imm_flag_bit) & 0x1U) != 0U;
  if (is_imm || src == kImmOperandId) {
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
  if (is_bcu_output_operand_arith(src)) {
    if (!extra.valid) {
      status = kPayloadStatusMissingBcuValue;
      return kPayloadInvalidHandle;
    }
    const std::uint32_t bcu_id = decode_bcu_output_id_arith(src);
    const std::uint32_t output_index = decode_bcu_output_index_arith(src);
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

inline void execute_arithmetic_module(const std::uint64_t *instructions,
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
                                 kEmptyRegisters, kModuleArithmetic,
                                 partition_id, 0U);
    return;
  }
  PayloadExtraLayout extra;
  parse_payload_extra_layout(control, control_count, layout, extra, status);
  if (status != kPayloadStatusOk) {
    constexpr std::uint32_t kEmptyRegisters = 0U;
    std::uint32_t empty_state[1] = {0U};
    write_payload_module_outputs(outputs, output_count, status, empty_state,
                                 kEmptyRegisters, kModuleArithmetic,
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

  constexpr std::uint32_t kMaxBcuUnits = 8U;
  std::uint32_t active_line_crc[kMaxBcuUnits];
  for (std::uint32_t i = 0; i < kMaxBcuUnits; ++i) {
#pragma HLS PIPELINE II = 1
    active_line_crc[i] =
        (extra.valid && i < extra.bcu_active_count)
            ? static_cast<std::uint32_t>(control[extra.bcu_active_offset + i])
            : 0U;
  }

  constexpr std::uint32_t kMaxCoeff = 2048U;
  std::uint64_t sud_src1_ntt[kMaxCoeff];
#pragma HLS BIND_STORAGE variable = sud_src1_ntt type = ram_2p impl = bram

  const std::uint32_t instruction_count =
      instruction_word_count / kInstructionWordStride;
  std::uint32_t executed = 0U;
  for (std::uint32_t i = 0; i < instruction_count; ++i) {
    const DecodedInstruction inst = decode_instruction(instructions, i);
    if (!is_arithmetic_opcode(inst.opcode)) {
      continue;
    }

    const std::uint32_t src0_handle =
        resolve_operand_handle(control, layout, extra, register_handles, inst,
                               false, status);
    if (status != kPayloadStatusOk) {
      break;
    }
    const std::uint32_t rns_base_id = inst.rns;
    const std::uint64_t mod =
        payload_lookup_modulus(control, layout, rns_base_id);
    if (mod == 0U) {
      status = kPayloadStatusMissingModulus;
      break;
    }

    std::uint32_t src1_handle = src0_handle;
    const bool add_op = (inst.opcode == kOpAdd || inst.opcode == kOpAds);
    const bool sub_op = (inst.opcode == kOpSub || inst.opcode == kOpSus);
    const bool sud_op = (inst.opcode == kOpSud);
    const bool needs_src1 = add_op || sub_op || sud_op;
    const bool src1_is_bcu = is_bcu_output_operand_arith(inst.src1);
    if (needs_src1) {
      src1_handle =
          resolve_operand_handle(control, layout, extra, register_handles, inst,
                                 true, status);
      if (status != kPayloadStatusOk) {
        break;
      }
    }

    const std::uint32_t out_handle = payload_allocate_handle(
        control, layout, rns_base_id,
        (payload_handle_flags(control, layout, src0_handle) & kPayloadFlagIsNtt) != 0U,
        status);
    if (status != kPayloadStatusOk) {
      break;
    }

    const std::uint32_t src0_offset =
        payload_handle_coeff_offset(layout, src0_handle);
    const std::uint32_t src1_offset =
        payload_handle_coeff_offset(layout, src1_handle);
    const std::uint32_t out_offset =
        payload_handle_coeff_offset(layout, out_handle);
    const bool neg_op = (inst.opcode == kOpNeg);
    const bool con_op = (inst.opcode == kOpCon);
    if (!neg_op && !con_op && !add_op && !sub_op && !sud_op) {
      status = kPayloadStatusUnsupportedOpcode;
      break;
    }

    std::uint64_t sud_divisor_inv = 1U;
    if (sud_op) {
      if (layout.coeff_count > kMaxCoeff) {
        status = kPayloadStatusLayoutOverflow;
        break;
      }
      std::uint64_t divisor_mod = 0U;
      if (src1_is_bcu) {
        if (!extra.valid) {
          status = kPayloadStatusMissingBcuValue;
          break;
        }
        const std::uint32_t bcu_id = decode_bcu_output_id_arith(inst.src1);
        if (bcu_id >= kMaxBcuUnits || bcu_id >= extra.bcu_unit_count) {
          status = kPayloadStatusMissingBcuValue;
          break;
        }
        const std::uint32_t line_crc = active_line_crc[bcu_id];
        if (line_crc == 0U) {
          status = kPayloadStatusMissingBciConfig;
          break;
        }
        std::uint32_t src_count = 0U;
        std::uint32_t src_base_offset = 0U;
        const bool found = payload_find_bci_sources_arith(
            control, control_count, extra, line_crc, bcu_id, src_count,
            src_base_offset);
        if (!found || src_count == 0U) {
          status = kPayloadStatusMissingBciConfig;
          break;
        }
        divisor_mod = 1U;
        for (std::uint32_t k = 0U; k < src_count; ++k) {
#pragma HLS PIPELINE II = 1
          const std::uint32_t src_base_id =
              static_cast<std::uint32_t>(control[src_base_offset + k]);
          const std::uint64_t src_modulus =
              payload_lookup_modulus(control, layout, src_base_id);
          if (src_modulus == 0U) {
            status = kPayloadStatusMissingModulus;
            break;
          }
          divisor_mod = mod_mul(divisor_mod, src_modulus % mod, mod);
        }
        if (status != kPayloadStatusOk) {
          break;
        }
      } else {
        const std::uint64_t src1_modulus =
            payload_lookup_modulus(control, layout, rns_base_id);
        if (src1_modulus == 0U) {
          status = kPayloadStatusMissingModulus;
          break;
        }
        divisor_mod = src1_modulus % mod;
      }
      sud_divisor_inv = mod_inv(divisor_mod, mod);
      if (sud_divisor_inv == 0U) {
        status = kPayloadStatusMissingModulus;
        break;
      }

      const bool src1_is_ntt =
          (payload_handle_flags(control, layout, src1_handle) &
           kPayloadFlagIsNtt) != 0U;
      for (std::uint32_t coeff = 0U; coeff < layout.coeff_count; ++coeff) {
#pragma HLS PIPELINE II = 1
        sud_src1_ntt[coeff] = payload[src1_offset + coeff] % mod;
      }
      if (!src1_is_ntt) {
        ntt_apply_negacyclic_four_step(sud_src1_ntt, layout.coeff_count, mod,
                                       rns_base_id, false);
      }
    }

    for (std::uint32_t coeff = 0; coeff < layout.coeff_count; ++coeff) {
#pragma HLS PIPELINE II = 1
      const std::uint64_t a = payload[src0_offset + coeff] % mod;
      std::uint64_t result = a;
      if (add_op) {
        const std::uint64_t b = payload[src1_offset + coeff] % mod;
        result = mod_add(a, b, mod);
      } else if (sub_op) {
        const std::uint64_t b = payload[src1_offset + coeff] % mod;
        result = mod_sub(a, b, mod);
      } else if (sud_op) {
        const std::uint64_t b = sud_src1_ntt[coeff] % mod;
        const std::uint64_t diff = mod_sub(a, b, mod);
        result = mod_mul(diff, sud_divisor_inv, mod);
      } else if (con_op) {
        result = a % mod;
      } else if (neg_op) {
        result = (a == 0U) ? 0U : (mod - a);
      }
      payload[out_offset + coeff] = result;
    }

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
                               bounded_register_count, kModuleArithmetic,
                               partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

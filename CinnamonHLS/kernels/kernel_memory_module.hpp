#pragma once

#include "kernel_common.hpp"

namespace cinnamon_hls_kernel {

namespace {

constexpr std::uint32_t kFhePolyDegree = 65536U;
constexpr std::uint32_t kFheModulusBits = 32U;
constexpr std::uint32_t kFheRnsGroups = 2U;
constexpr std::uint32_t kLookupBlockSize = 16U;
constexpr std::uint32_t kMaxLookupBlocks =
    (kFhePolyDegree * kFheRnsGroups) / kLookupBlockSize;
static_assert(kFheModulusBits == 32U, "FHE memory path assumes 32-bit moduli");

inline std::uint32_t select_lookup_block(const std::uint64_t *block_first_keys,
                                         std::uint32_t block_count,
                                         std::uint64_t token_key) {
  std::uint32_t lo = 0U;
  std::uint32_t hi = block_count;
  while (lo < hi) {
#pragma HLS PIPELINE II = 1
    const std::uint32_t mid = lo + ((hi - lo) >> 1U);
    const std::uint64_t key = block_first_keys[mid];
    if (key <= token_key) {
      lo = mid + 1U;
    } else {
      hi = mid;
    }
  }
  return (lo == 0U) ? 0U : (lo - 1U);
}

inline std::uint64_t lookup_stream_value_range(
    const std::uint64_t *inputs,
    std::uint32_t table_base,
    std::uint32_t range_begin,
    std::uint32_t range_end,
    std::uint64_t token_key,
    std::uint64_t mod) {
  std::uint32_t lo = range_begin;
  std::uint32_t hi = range_end;
  while (lo < hi) {
#pragma HLS PIPELINE II = 1
    const std::uint32_t mid = lo + ((hi - lo) >> 1U);
    const std::uint32_t cursor = table_base + 1U + (mid << 1U);
    const std::uint64_t key = inputs[cursor];
    if (key == token_key) {
      const std::uint64_t value = inputs[cursor + 1U];
      return (mod == 0U) ? 0U : (value % mod);
    }
    if (key < token_key) {
      lo = mid + 1U;
    } else {
      hi = mid;
    }
  }
  return 0U;
}

inline std::uint64_t lookup_stream_value_from_cache(
    const std::uint64_t *cache_keys,
    const std::uint64_t *cache_values,
    std::uint32_t cache_count,
    std::uint64_t token_key,
    std::uint64_t mod) {
  std::uint32_t lo = 0U;
  std::uint32_t hi = cache_count;
  while (lo < hi) {
#pragma HLS PIPELINE II = 1
    const std::uint32_t mid = lo + ((hi - lo) >> 1U);
    const std::uint64_t key = cache_keys[mid];
    if (key == token_key) {
      const std::uint64_t value = cache_values[mid];
      return (mod == 0U) ? 0U : (value % mod);
    }
    if (key < token_key) {
      lo = mid + 1U;
    } else {
      hi = mid;
    }
  }
  return 0U;
}

}  // namespace

inline void execute_memory_module(const std::uint64_t *instructions,
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
  constexpr std::uint32_t kRectRows = 64U;
  constexpr std::uint32_t kRectCols = 16U;
  std::uint64_t state[kMaxRegisters];
  std::uint64_t rect_mem[kRectCols][kRectRows];
#pragma HLS BIND_STORAGE variable = state type = ram_2p impl = uram
#pragma HLS BIND_STORAGE variable = rect_mem type = ram_2p impl = bram
#pragma HLS ARRAY_PARTITION variable = state cyclic factor = 8
#pragma HLS ARRAY_PARTITION variable = rect_mem dim = 1 cyclic factor = 8

  const std::uint32_t bounded_register_count =
      (register_count > kMaxRegisters) ? kMaxRegisters : register_count;
  init_state_from_input(inputs, input_count, state, bounded_register_count, mod,
                        state_base);

  for (std::uint32_t r = 0; r < kRectRows; ++r) {
#pragma HLS PIPELINE II = 1
    for (std::uint32_t c = 0; c < kRectCols; ++c) {
#pragma HLS UNROLL factor = 8
      rect_mem[c][r] = 0ULL;
    }
  }

  const std::uint32_t table_base = state_base + bounded_register_count;
  const std::uint32_t table_count_raw =
      (table_base < input_count) ? static_cast<std::uint32_t>(inputs[table_base]) : 0U;
  const std::uint32_t pairs_avail =
      (input_count > (table_base + 1U)) ? ((input_count - (table_base + 1U)) / 2U) : 0U;
  const std::uint32_t table_count =
      (table_count_raw < pairs_avail) ? table_count_raw : pairs_avail;

  std::uint64_t lookup_block_first_keys[kMaxLookupBlocks];
  std::uint32_t lookup_block_offsets[kMaxLookupBlocks + 1U];
#pragma HLS BIND_STORAGE variable = lookup_block_first_keys type = ram_2p impl = uram
#pragma HLS BIND_STORAGE variable = lookup_block_offsets type = ram_2p impl = uram

  std::uint64_t lookup_cache_keys[kLookupBlockSize];
  std::uint64_t lookup_cache_values[kLookupBlockSize];
#pragma HLS BIND_STORAGE variable = lookup_cache_keys type = ram_2p impl = bram
#pragma HLS BIND_STORAGE variable = lookup_cache_values type = ram_2p impl = bram

  std::uint32_t lookup_block_count = 0U;
  bool use_lookup_blocks = false;
  if (table_count != 0U) {
    const std::uint32_t requested_blocks =
        (table_count + (kLookupBlockSize - 1U)) / kLookupBlockSize;
    if (requested_blocks <= kMaxLookupBlocks) {
      use_lookup_blocks = true;
      lookup_block_count = requested_blocks;
      for (std::uint32_t b = 0; b < lookup_block_count; ++b) {
#pragma HLS PIPELINE II = 1
        const std::uint32_t block_begin = b * kLookupBlockSize;
        lookup_block_offsets[b] = block_begin;
        const std::uint32_t cursor = table_base + 1U + (block_begin << 1U);
        lookup_block_first_keys[b] =
            (cursor < input_count) ? inputs[cursor] : 0ULL;
      }
      lookup_block_offsets[lookup_block_count] = table_count;
    }
  }
  std::uint32_t lookup_cache_count = 0U;
  std::uint32_t lookup_cache_block_id = 0U;
  bool lookup_cache_valid = false;

  const std::uint32_t instruction_count =
      instruction_word_count / kInstructionWordStride;
  std::uint32_t executed = 0U;

  for (std::uint32_t i = 0; i < instruction_count; ++i) {
    const DecodedInstruction inst = decode_instruction(instructions, i);
    if (!is_memory_opcode(inst.opcode)) {
      continue;
    }

    const bool src0_is_imm =
        ((inst.flags & 0x1U) != 0U) || (inst.src0 == kImmOperandId);
    const bool src0_is_token = (!src0_is_imm) && (inst.src0 == kTokenOperandId);
    const bool src0_is_reg =
        (!src0_is_imm) && (!src0_is_token) && (bounded_register_count != 0U);

    std::uint64_t src0 = 0ULL;
    if ((inst.opcode == kOpMov || inst.opcode == kOpStore ||
         inst.opcode == kOpSpill || inst.opcode == kOpSnd) &&
        src0_is_reg) {
      const std::uint64_t raw = state[inst.src0 % bounded_register_count];
      src0 = (mod == 0U) ? 0U : (raw % mod);
    } else if (src0_is_imm) {
      src0 = signed_mod(inst.imm0, mod);
    } else if (src0_is_token) {
      if (use_lookup_blocks && lookup_block_count != 0U) {
        const std::uint32_t block_id =
            select_lookup_block(lookup_block_first_keys, lookup_block_count, inst.aux);
        if ((!lookup_cache_valid) || (lookup_cache_block_id != block_id)) {
          const std::uint32_t range_begin = lookup_block_offsets[block_id];
          const std::uint32_t range_end = lookup_block_offsets[block_id + 1U];
          lookup_cache_count =
              (range_end >= range_begin) ? (range_end - range_begin) : 0U;
          for (std::uint32_t j = 0; j < kLookupBlockSize; ++j) {
#pragma HLS PIPELINE II = 1
            if (j < lookup_cache_count) {
              const std::uint32_t cursor =
                  table_base + 1U + ((range_begin + j) << 1U);
              lookup_cache_keys[j] =
                  (cursor < input_count) ? inputs[cursor] : 0ULL;
              lookup_cache_values[j] =
                  ((cursor + 1U) < input_count) ? inputs[cursor + 1U] : 0ULL;
            } else {
              lookup_cache_keys[j] = 0ULL;
              lookup_cache_values[j] = 0ULL;
            }
          }
          lookup_cache_block_id = block_id;
          lookup_cache_valid = true;
        }
        src0 = lookup_stream_value_from_cache(
            lookup_cache_keys, lookup_cache_values, lookup_cache_count, inst.aux, mod);
      } else {
        src0 = lookup_stream_value(inputs, input_count, state_base,
                                   bounded_register_count, inst.aux, mod);
      }
    } else if (bounded_register_count == 0U) {
      src0 = 0U;
    } else {
      const std::uint64_t raw = state[inst.src0 % bounded_register_count];
      src0 = (mod == 0U) ? 0U : (raw % mod);
    }
    std::uint64_t result = src0;

    if (inst.opcode == kOpStore || inst.opcode == kOpSpill ||
        inst.opcode == kOpSnd) {
      const std::uint32_t row =
          (bounded_register_count == 0U) ? 0U : (inst.dst % kRectRows);
      const std::uint32_t col = abs_mod_u32(inst.imm0, kRectCols);
      rect_mem[col][row] = (mod == 0U) ? 0U : (src0 % mod);
      result = rect_mem[col][row];
    }

    store_register(state, bounded_register_count, inst.dst, result, mod);
    ++executed;
  }

  write_module_outputs(outputs, output_count, state, bounded_register_count,
                       kModuleMemory, partition_id, executed);
}

}  // namespace cinnamon_hls_kernel

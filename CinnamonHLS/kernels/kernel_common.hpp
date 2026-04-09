#pragma once

#include <cstdint>

#include "generated_ntt_tables.hpp"

namespace cinnamon_hls_kernel {

constexpr std::uint32_t kInstructionWordStride = 4U;
constexpr std::uint32_t kOutputHeaderWords = 6U;
constexpr std::uint64_t kInputMagic = 0x43494E4E414D4F4EULL;  // "CINNAMON"
constexpr std::uint64_t kDefaultModulus = 268042241ULL;

// Matches python/cinnamon_fpga/parser.py opcode ids.
enum Opcode : std::uint32_t {
  kOpLoad = 1,
  kOpStore = 2,
  kOpMov = 3,
  kOpAdd = 4,
  kOpSub = 5,
  kOpMul = 6,
  kOpMup = 7,
  kOpMus = 8,
  kOpAds = 9,
  kOpSus = 10,
  kOpSud = 11,
  kOpNtt = 12,
  kOpInt = 13,
  kOpRot = 14,
  kOpCon = 15,
  kOpNeg = 16,
  kOpBci = 17,
  kOpPl1 = 18,
  kOpEvg = 19,
  kOpJoi = 20,
  kOpJol = 21,
  kOpSyn = 22,
  kOpRcv = 23,
  kOpDis = 24,
  kOpRsv = 25,
  kOpMod = 26,
  kOpRec = 27,
  kOpSnd = 28,
  kOpDrm = 29,
  kOpLoas = 30,
  kOpSpill = 31,
  kOpBcw = 32,
  kOpRsi = 33,
};

enum ModuleId : std::uint32_t {
  kModuleMemory = 1,
  kModuleArithmetic = 2,
  kModuleMontgomery = 3,
  kModuleNtt = 4,
  kModuleBaseConv = 5,
  kModuleAutomorphism = 6,
  kModuleTranspose = 7,
};

constexpr std::uint32_t kImmOperandId = 0xFFFU;
constexpr std::uint32_t kTokenOperandId = 0xFFEU;

inline std::uint64_t rotl64(std::uint64_t value, std::uint32_t shift) {
  const std::uint32_t s = shift & 63U;
  if (s == 0U) {
    return value;
  }
  return (value << s) | (value >> (64U - s));
}

inline std::uint64_t hash_mix(std::uint64_t v) {
  v ^= v >> 33U;
  v *= 0xff51afd7ed558ccdULL;
  v ^= v >> 33U;
  v *= 0xc4ceb9fe1a85ec53ULL;
  v ^= v >> 33U;
  return v;
}

inline std::uint64_t mod_add(std::uint64_t a, std::uint64_t b,
                             std::uint64_t mod) {
  if (mod == 0U) {
    return 0U;
  }
  const std::uint64_t aa = a % mod;
  const std::uint64_t bb = b % mod;
  const std::uint64_t s = aa + bb;
  return (s >= mod) ? (s - mod) : s;
}

inline std::uint64_t mod_sub(std::uint64_t a, std::uint64_t b,
                             std::uint64_t mod) {
  if (mod == 0U) {
    return 0U;
  }
  const std::uint64_t aa = a % mod;
  const std::uint64_t bb = b % mod;
  return (aa >= bb) ? (aa - bb) : (aa + (mod - bb));
}

inline std::uint64_t mod_mul(std::uint64_t a, std::uint64_t b,
                             std::uint64_t mod) {
  if (mod == 0U) {
    return 0U;
  }
  const __uint128_t p = static_cast<__uint128_t>(a % mod) *
                        static_cast<__uint128_t>(b % mod);
  return static_cast<std::uint64_t>(p % mod);
}

inline std::uint64_t mod_pow(std::uint64_t base, std::uint64_t exponent,
                             std::uint64_t mod) {
  if (mod == 0U) {
    return 0U;
  }
  std::uint64_t acc = 1U;
  std::uint64_t b = base % mod;
  std::uint64_t e = exponent;
  while (e != 0U) {
    if ((e & 1U) != 0U) {
      acc = mod_mul(acc, b, mod);
    }
    b = mod_mul(b, b, mod);
    e >>= 1U;
  }
  return acc;
}

inline std::uint64_t mod_inv(std::uint64_t a, std::uint64_t mod) {
  if (mod == 0U) {
    return 0U;
  }
  std::int64_t t = 0;
  std::int64_t new_t = 1;
  std::int64_t r = static_cast<std::int64_t>(mod);
  std::int64_t new_r = static_cast<std::int64_t>(a % mod);

  while (new_r != 0) {
    const std::int64_t q = r / new_r;
    const std::int64_t next_t = t - q * new_t;
    t = new_t;
    new_t = next_t;

    const std::int64_t next_r = r - q * new_r;
    r = new_r;
    new_r = next_r;
  }

  if (r != 1) {
    return 0U;
  }

  std::int64_t reduced = t % static_cast<std::int64_t>(mod);
  if (reduced < 0) {
    reduced += static_cast<std::int64_t>(mod);
  }
  return static_cast<std::uint64_t>(reduced);
}

inline std::uint64_t montgomery_reduce_ntt_friendly(std::uint64_t a,
                                                    std::uint64_t q) {
  if (q == 0U) {
    return 0U;
  }
  constexpr std::uint32_t kOneIterLog2 = 17U;
  constexpr std::uint32_t kNumIterations = 2U;
  constexpr std::uint64_t kR = (1ULL << (kOneIterLog2 * kNumIterations));
  const std::uint64_t r_mod_q = kR % q;
  const std::uint64_t r_inv = mod_inv(r_mod_q, q);
  if (r_inv == 0U) {
    return a % q;
  }
  return mod_mul(a % q, r_inv, q);
}

inline std::uint64_t montgomery_mul_ntt_friendly(std::uint64_t a,
                                                 std::uint64_t b,
                                                 std::uint64_t q) {
  return montgomery_reduce_ntt_friendly(mod_mul(a, b, q), q);
}

inline std::int64_t sign_extend_32(std::uint64_t value) {
  const std::uint32_t lo = static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
  return static_cast<std::int64_t>(static_cast<std::int32_t>(lo));
}

inline std::uint64_t signed_mod(std::int64_t value, std::uint64_t mod) {
  if (mod == 0U) {
    return 0U;
  }
  if (value >= 0) {
    return static_cast<std::uint64_t>(value) % mod;
  }
  const std::uint64_t mag = static_cast<std::uint64_t>(-value) % mod;
  return (mag == 0U) ? 0U : (mod - mag);
}

inline std::uint32_t abs_mod_u32(std::int64_t value, std::uint32_t mod) {
  if (mod == 0U) {
    return 0U;
  }
  if (value >= 0) {
    return static_cast<std::uint32_t>(value % static_cast<std::int64_t>(mod));
  }
  const std::int64_t mag = (-value) % static_cast<std::int64_t>(mod);
  return static_cast<std::uint32_t>(mag);
}

struct DecodedInstruction {
  std::uint32_t opcode = 0;
  std::uint32_t dst = 0;
  std::uint32_t src0 = 0;
  std::uint32_t src1 = 0;
  std::uint32_t rns = 0;
  std::uint32_t flags = 0;
  std::int64_t imm0 = 0;
  std::int64_t imm1 = 0;
  std::uint64_t aux = 0;
  std::uint64_t line_crc = 0;
};

inline DecodedInstruction decode_instruction(const std::uint64_t *instructions,
                                             std::uint32_t instruction_index) {
  const std::uint32_t base = instruction_index * kInstructionWordStride;
  const std::uint64_t w0 = instructions[base + 0U];
  const std::uint64_t w1 = instructions[base + 1U];
  const std::uint64_t w2 = instructions[base + 2U];
  const std::uint64_t w3 = instructions[base + 3U];

  DecodedInstruction inst;
  inst.opcode = static_cast<std::uint32_t>(w0 & 0xFFULL);
  inst.dst = static_cast<std::uint32_t>((w0 >> 8U) & 0xFFFULL);
  inst.src0 = static_cast<std::uint32_t>((w0 >> 20U) & 0xFFFULL);
  inst.src1 = static_cast<std::uint32_t>((w0 >> 32U) & 0xFFFULL);
  inst.rns = static_cast<std::uint32_t>((w0 >> 44U) & 0xFFFULL);
  inst.flags = static_cast<std::uint32_t>((w0 >> 56U) & 0xFFULL);
  inst.imm0 = sign_extend_32(w1);
  inst.imm1 = sign_extend_32(w1 >> 32U);
  inst.aux = w2;
  inst.line_crc = w3;
  return inst;
}

inline std::uint64_t lookup_stream_value(const std::uint64_t *inputs,
                                         std::uint32_t input_count,
                                         std::uint32_t state_base,
                                         std::uint32_t register_count,
                                         std::uint64_t token_key,
                                         std::uint64_t mod) {
  const std::uint32_t table_base = state_base + register_count;
  if (table_base >= input_count) {
    return 0U;
  }
  const std::uint32_t table_count_raw = static_cast<std::uint32_t>(inputs[table_base]);
  const std::uint32_t pairs_avail =
      (input_count > (table_base + 1U)) ? ((input_count - (table_base + 1U)) / 2U) : 0U;
  const std::uint32_t table_count =
      (table_count_raw < pairs_avail) ? table_count_raw : pairs_avail;

  std::uint32_t lo = 0U;
  std::uint32_t hi = table_count;
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

inline std::uint64_t load_operand(const std::uint64_t *state,
                                  std::uint32_t register_count,
                                  const DecodedInstruction &inst,
                                  bool use_src1,
                                  std::uint64_t mod,
                                  const std::uint64_t *inputs,
                                  std::uint32_t input_count,
                                  std::uint32_t state_base) {
  const std::uint32_t src = use_src1 ? inst.src1 : inst.src0;
  const std::uint32_t imm_flag_bit = use_src1 ? 1U : 0U;
  const bool is_imm = ((inst.flags >> imm_flag_bit) & 0x1U) != 0U;

  if (is_imm || src == kImmOperandId) {
    const std::int64_t imm = use_src1 ? inst.imm1 : inst.imm0;
    return signed_mod(imm, mod);
  }
  if (src == kTokenOperandId) {
    return lookup_stream_value(inputs, input_count, state_base, register_count,
                               inst.aux, mod);
  }
  if (register_count == 0U) {
    return 0U;
  }
  return (mod == 0U) ? 0U : (state[src % register_count] % mod);
}

inline void store_register(std::uint64_t *state,
                           std::uint32_t register_count,
                           std::uint32_t dst,
                           std::uint64_t value,
                           std::uint64_t mod) {
  if (register_count == 0U) {
    return;
  }
  state[dst % register_count] = (mod == 0U) ? 0U : (value % mod);
}

inline std::uint64_t compute_trace(const std::uint64_t *state,
                                   std::uint32_t register_count,
                                   std::uint32_t module_id,
                                   std::uint32_t executed) {
  std::uint64_t acc = 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(module_id);
  acc ^= static_cast<std::uint64_t>(executed) << 16U;
  for (std::uint32_t i = 0; i < register_count; ++i) {
#pragma HLS PIPELINE II = 1
    acc ^= hash_mix(state[i] ^ static_cast<std::uint64_t>(i + 1U));
    acc = rotl64(acc, 7U);
  }
  return acc;
}

inline void write_module_outputs(std::uint64_t *outputs,
                                 std::uint32_t output_count,
                                 const std::uint64_t *state,
                                 std::uint32_t register_count,
                                 std::uint32_t module_id,
                                 std::uint32_t partition_id,
                                 std::uint32_t executed) {
  if (output_count == 0U) {
    return;
  }
  outputs[0] = 0ULL;
  if (output_count > 1U) {
    outputs[1] = executed;
  }
  if (output_count > 2U) {
    outputs[2] = register_count;
  }
  if (output_count > 3U) {
    outputs[3] = module_id;
  }
  if (output_count > 4U) {
    outputs[4] = partition_id;
  }
  if (output_count > 5U) {
    outputs[5] = compute_trace(state, register_count, module_id, executed);
  }

  const std::uint32_t state_words_available =
      (output_count > kOutputHeaderWords) ? (output_count - kOutputHeaderWords) : 0U;
  const std::uint32_t words_to_copy =
      (state_words_available < register_count) ? state_words_available : register_count;

  for (std::uint32_t i = 0; i < words_to_copy; ++i) {
#pragma HLS PIPELINE II = 1
    outputs[kOutputHeaderWords + i] = state[i];
  }
  for (std::uint32_t i = words_to_copy; i < state_words_available; ++i) {
#pragma HLS PIPELINE II = 1
    outputs[kOutputHeaderWords + i] = 0ULL;
  }
}

inline void init_kernel_layout(const std::uint64_t *inputs,
                               std::uint32_t input_count,
                               std::uint32_t &register_count,
                               std::uint64_t &mod,
                               std::uint32_t &state_base) {
  const bool has_layout = (input_count >= 3U) && (inputs[0] == kInputMagic);
  register_count = has_layout
                       ? static_cast<std::uint32_t>(inputs[1])
                       : ((input_count > 0U) ? input_count : 0U);
  mod = has_layout ? ((inputs[2] == 0U) ? kDefaultModulus : inputs[2])
                   : kDefaultModulus;
  state_base = has_layout ? 3U : 0U;
}

inline void init_state_from_input(const std::uint64_t *inputs,
                                  std::uint32_t input_count,
                                  std::uint64_t *state,
                                  std::uint32_t bounded_register_count,
                                  std::uint64_t mod,
                                  std::uint32_t state_base) {
  for (std::uint32_t i = 0; i < bounded_register_count; ++i) {
#pragma HLS PIPELINE II = 1
    const std::uint32_t idx = state_base + i;
    state[i] = (idx < input_count) ? ((mod == 0U) ? 0U : (inputs[idx] % mod)) : 0ULL;
  }
}

inline bool is_power_of_two(std::uint32_t v) {
  return v != 0U && ((v & (v - 1U)) == 0U);
}

struct TwiddleTable {
  std::uint64_t mod;
  std::uint32_t prime_id;
  bool inverse;
  std::uint32_t size;
  std::uint64_t values[8];
};

inline std::uint64_t select_twiddle(const TwiddleTable &tbl,
                                    std::uint32_t exponent) {
  const std::uint32_t idx = (tbl.size == 0U) ? 0U : (exponent % tbl.size);
  return tbl.values[idx];
}

inline std::uint64_t lookup_ntt_twiddle(std::uint64_t mod,
                                        std::uint32_t prime_id,
                                        bool inverse,
                                        std::uint32_t exponent,
                                        std::uint32_t span) {
  static const TwiddleTable kTables[] = {
      {1179649ULL, 1U, false, 4U, {1ULL, 683840ULL, 689020ULL, 494273ULL,
                                   0ULL, 0ULL, 0ULL, 0ULL}},
      {2752513ULL, 9U, true, 4U, {1ULL, 13629ULL, 1331270ULL, 2065647ULL,
                                  0ULL, 0ULL, 0ULL, 0ULL}},
      {6946817ULL, 2U, true, 4U, {1ULL, 4288696ULL, 6626758ULL, 6658417ULL,
                                  0ULL, 0ULL, 0ULL, 0ULL}},
      {5767169ULL, 3U, false, 4U, {1ULL, 5392703ULL, 1838090ULL, 3643041ULL,
                                   0ULL, 0ULL, 0ULL, 0ULL}},
      {786433ULL, 4U, false, 4U, {1ULL, 388249ULL, 100025ULL, 544685ULL,
                                  0ULL, 0ULL, 0ULL, 0ULL}},
      {786433ULL, 4U, false, 8U, {1ULL, 541396ULL, 544685ULL, 711817ULL,
                                  686408ULL, 641480ULL, 388249ULL, 216230ULL}},
      {786433ULL, 9U, true, 8U, {1ULL, 570203ULL, 398184ULL, 144953ULL,
                                 100025ULL, 74616ULL, 241748ULL, 245037ULL}},
  };

  const std::uint32_t want_size = (span >= 8U) ? 8U : 4U;
  for (const auto &tbl : kTables) {
    if (tbl.mod == mod && tbl.prime_id == prime_id && tbl.inverse == inverse &&
        tbl.size == want_size) {
      return select_twiddle(tbl, exponent) % mod;
    }
  }
  for (const auto &tbl : kTables) {
    if (tbl.mod == mod && tbl.prime_id == prime_id && tbl.inverse == inverse) {
      return select_twiddle(tbl, exponent) % mod;
    }
  }

  if (mod <= 2U) {
    return 1U;
  }
  return 1U + ((static_cast<std::uint64_t>(prime_id) +
                static_cast<std::uint64_t>(exponent) * 17ULL) %
               (mod - 1U));
}

struct NegacyclicRoots {
  std::uint64_t psi = 1U;
  std::uint64_t psi_inv = 1U;
  std::uint64_t omega = 1U;
  std::uint64_t omega_inv = 1U;
  bool from_table = false;
};

inline NegacyclicRoots resolve_negacyclic_roots(std::uint64_t mod,
                                                std::uint32_t span,
                                                std::uint32_t prime_id) {
  NegacyclicRoots roots{};
  for (std::size_t i = 0; i < kNegacyclicRootTableSize; ++i) {
#pragma HLS PIPELINE II = 1
    const auto &entry = kNegacyclicRootTable[i];
    if (entry.mod == mod && entry.span == span) {
      roots.psi = entry.psi % mod;
      roots.psi_inv = entry.psi_inv % mod;
      roots.omega = entry.omega % mod;
      roots.omega_inv = entry.omega_inv % mod;
      roots.from_table = true;
      return roots;
    }
  }
  // Compatibility fallback for unexpected modulus/span pairs.
  const std::uint64_t fallback_omega =
      lookup_ntt_twiddle(mod, prime_id, false, 1U, span);
  roots.omega = (mod == 0U) ? 1U : (fallback_omega % mod);
  roots.omega_inv = mod_inv(roots.omega, mod);
  if (roots.omega_inv == 0U) {
    roots.omega_inv = 1U;
  }
  return roots;
}

inline void choose_four_step_shape(std::uint32_t span,
                                   std::uint32_t &rows,
                                   std::uint32_t &cols) {
  const std::uint32_t levels = static_cast<std::uint32_t>(__builtin_ctz(span));
  const std::uint32_t row_levels = levels >> 1U;
  rows = 1U << row_levels;
  cols = 1U << (levels - row_levels);
}

inline void small_cyclic_dft(const std::uint64_t *input,
                             std::uint64_t *output,
                             std::uint32_t length,
                             std::uint64_t root,
                             std::uint64_t mod) {
  if (length == 0U) {
    return;
  }
  for (std::uint32_t k = 0U; k < length; ++k) {
#pragma HLS PIPELINE II = 1
    const std::uint64_t step = mod_pow(root, k, mod);
    std::uint64_t tw = 1U;
    std::uint64_t acc = 0U;
    for (std::uint32_t n = 0U; n < length; ++n) {
      acc = mod_add(acc, mod_mul(input[n], tw, mod), mod);
      tw = mod_mul(tw, step, mod);
    }
    output[k] = acc;
  }
}

inline void cyclic_ntt_four_step(std::uint64_t *values,
                                 std::uint32_t span,
                                 std::uint64_t omega,
                                 std::uint64_t mod,
                                 bool inverse_cyclic) {
  constexpr std::uint32_t kMaxSpan = 64U;
  constexpr std::uint32_t kMaxFourStepSide = 8U;
  if (span < 2U || span > kMaxSpan || mod == 0U) {
    return;
  }

  std::uint32_t rows = 1U;
  std::uint32_t cols = span;
  choose_four_step_shape(span, rows, cols);
  if (rows * cols != span || rows > kMaxFourStepSide || cols > kMaxFourStepSide) {
    return;
  }

  std::uint64_t stage1[kMaxSpan];
  std::uint64_t stage2[kMaxSpan];
  std::uint64_t lane_in[kMaxFourStepSide];
  std::uint64_t lane_out[kMaxFourStepSide];
#pragma HLS ARRAY_PARTITION variable = stage1 cyclic factor = 8
#pragma HLS ARRAY_PARTITION variable = stage2 cyclic factor = 8
#pragma HLS ARRAY_PARTITION variable = lane_in complete
#pragma HLS ARRAY_PARTITION variable = lane_out complete

  const std::uint64_t row_root = mod_pow(omega, rows, mod);
  const std::uint64_t col_root = mod_pow(omega, cols, mod);

  // Stage 1: C-point transforms over n2 with x[n1 + R*n2] mapping.
  for (std::uint32_t n1 = 0U; n1 < rows; ++n1) {
    for (std::uint32_t n2 = 0U; n2 < cols; ++n2) {
#pragma HLS PIPELINE II = 1
      lane_in[n2] = values[n1 + rows * n2] % mod;
    }
    small_cyclic_dft(lane_in, lane_out, cols, row_root, mod);
    for (std::uint32_t k2 = 0U; k2 < cols; ++k2) {
#pragma HLS PIPELINE II = 1
      stage1[n1 * cols + k2] = lane_out[k2];
    }
  }

  // Stage 2: middle twiddle multiply.
  for (std::uint32_t n1 = 0U; n1 < rows; ++n1) {
    std::uint64_t tw = 1U;
    const std::uint64_t step = mod_pow(omega, n1, mod);
    for (std::uint32_t k2 = 0U; k2 < cols; ++k2) {
#pragma HLS PIPELINE II = 1
      const std::uint32_t idx = n1 * cols + k2;
      stage1[idx] = mod_mul(stage1[idx], tw, mod);
      tw = mod_mul(tw, step, mod);
    }
  }

  // Stage 3: explicit transpose to C x R.
  for (std::uint32_t n1 = 0U; n1 < rows; ++n1) {
    for (std::uint32_t k2 = 0U; k2 < cols; ++k2) {
#pragma HLS PIPELINE II = 1
      stage2[k2 * rows + n1] = stage1[n1 * cols + k2];
    }
  }

  // Stage 4: R-point transforms over n1, write k = k2 + C*k1.
  for (std::uint32_t k2 = 0U; k2 < cols; ++k2) {
    for (std::uint32_t n1 = 0U; n1 < rows; ++n1) {
#pragma HLS PIPELINE II = 1
      lane_in[n1] = stage2[k2 * rows + n1];
    }
    small_cyclic_dft(lane_in, lane_out, rows, col_root, mod);
    for (std::uint32_t k1 = 0U; k1 < rows; ++k1) {
#pragma HLS PIPELINE II = 1
      values[k2 + cols * k1] = lane_out[k1];
    }
  }

  if (inverse_cyclic) {
    const std::uint64_t span_inv = mod_inv(span, mod);
    if (span_inv != 0U) {
      for (std::uint32_t i = 0U; i < span; ++i) {
#pragma HLS PIPELINE II = 1
        values[i] = mod_mul(values[i], span_inv, mod);
      }
    }
  }
}

inline void ntt_apply_negacyclic_four_step(std::uint64_t *values,
                                           std::uint32_t span,
                                           std::uint64_t mod,
                                           std::uint32_t prime_id,
                                           bool inverse) {
  if (span < 2U || mod == 0U || !is_power_of_two(span)) {
    return;
  }
  const NegacyclicRoots roots = resolve_negacyclic_roots(mod, span, prime_id);

  if (!inverse) {
    // Pre-twist by psi^i before cyclic NTT(omega = psi^2).
    std::uint64_t twist = 1U;
    for (std::uint32_t i = 0U; i < span; ++i) {
#pragma HLS PIPELINE II = 1
      values[i] = mod_mul(values[i] % mod, twist, mod);
      twist = mod_mul(twist, roots.psi, mod);
    }
    cyclic_ntt_four_step(values, span, roots.omega, mod, false);
    return;
  }

  // Inverse cyclic NTT first, then post-twist by psi^-i.
  cyclic_ntt_four_step(values, span, roots.omega_inv, mod, true);
  std::uint64_t twist = 1U;
  for (std::uint32_t i = 0U; i < span; ++i) {
#pragma HLS PIPELINE II = 1
    values[i] = mod_mul(values[i] % mod, twist, mod);
    twist = mod_mul(twist, roots.psi_inv, mod);
  }
}

inline void ntt_apply_dit(std::uint64_t *values,
                          std::uint32_t span,
                          std::uint64_t mod,
                          std::uint32_t prime_id,
                          bool inverse) {
  ntt_apply_negacyclic_four_step(values, span, mod, prime_id, inverse);
}

inline void ntt_apply_dif(std::uint64_t *values,
                          std::uint32_t span,
                          std::uint64_t mod,
                          std::uint32_t prime_id,
                          bool inverse) {
  ntt_apply_negacyclic_four_step(values, span, mod, prime_id, inverse);
}

inline bool is_memory_opcode(std::uint32_t opcode) {
  return opcode == kOpLoad || opcode == kOpLoas || opcode == kOpStore ||
         opcode == kOpSpill || opcode == kOpMov || opcode == kOpEvg ||
         opcode == kOpRec || opcode == kOpSnd;
}

inline bool is_arithmetic_opcode(std::uint32_t opcode) {
  return opcode == kOpAdd || opcode == kOpSub || opcode == kOpAds ||
         opcode == kOpSus || opcode == kOpSud || opcode == kOpCon ||
         opcode == kOpNeg;
}


inline std::uint32_t ilog2_pow2(std::uint32_t value) {
  std::uint32_t log = 0U;
  while ((1U << log) < value) {
    ++log;
  }
  return log;
}

inline std::uint32_t automorphism_perm_info_width(std::uint32_t size) {
  if (size < 2U || !is_power_of_two(size)) {
    return 0U;
  }
  const std::uint32_t level = ilog2_pow2(size);
  return 2U * level * (size >> 1U);
}

inline std::uint64_t automorphism_perm_info_mask(std::uint32_t size) {
  const std::uint32_t width = automorphism_perm_info_width(size);
  if (width == 0U) {
    return 0U;
  }
  if (width >= 64U) {
    return ~0ULL;
  }
  return (1ULL << width) - 1ULL;
}

inline std::uint64_t sanitize_perm_info(std::uint64_t perm_info,
                                        std::uint32_t size) {
  return perm_info & automorphism_perm_info_mask(size);
}

inline void apply_benes_network(std::uint64_t *values,
                                std::uint32_t size,
                                std::uint64_t perm_bits) {
  constexpr std::uint32_t kMaxBenesSize = 16U;
  if (size < 2U || !is_power_of_two(size) || size > kMaxBenesSize) {
    return;
  }
  const std::uint32_t levels = ilog2_pow2(size);
  const std::uint32_t half_size = size >> 1U;
  const std::uint64_t perm_mask = automorphism_perm_info_mask(size);
  const std::uint64_t perm = perm_bits & perm_mask;
  std::uint64_t scratch[kMaxBenesSize];
#pragma HLS ARRAY_PARTITION variable = scratch cyclic factor = 8

  for (std::uint32_t level = 0U; level < 2U * levels; ++level) {
    for (std::uint32_t i = 0U; i < size; ++i) {
#pragma HLS PIPELINE II = 1
      scratch[i] = values[i];
    }
    const std::uint64_t layer =
        (perm >> (level * half_size)) & ((1ULL << half_size) - 1ULL);
    const std::uint32_t block_size =
        (level < levels) ? (1U << (levels - level)) : (1U << (1U + level - levels));
    const std::uint32_t half_block = block_size >> 1U;
    const std::uint32_t block_num = size / block_size;

    for (std::uint32_t block_id = 0U; block_id < block_num; ++block_id) {
      for (std::uint32_t i = 0U; i < half_block; ++i) {
#pragma HLS PIPELINE II = 1
        const std::uint32_t perm_idx = block_id * half_block + i;
        const bool do_swap = ((layer >> perm_idx) & 0x1ULL) != 0ULL;
        const std::uint32_t idx1 = block_id * block_size + i;
        const std::uint32_t idx2 = idx1 + half_block;
        values[idx1] = do_swap ? scratch[idx2] : scratch[idx1];
        values[idx2] = do_swap ? scratch[idx1] : scratch[idx2];
      }
    }
  }
}

inline std::uint32_t select_perm_span(std::uint32_t register_count,
                                      std::uint32_t hint) {
  if (hint >= 2U && is_power_of_two(hint) && hint <= register_count &&
      hint <= 16U) {
    return hint;
  }
  if (register_count >= 16U) {
    return 16U;
  }
  if (register_count >= 8U) {
    return 8U;
  }
  if (register_count >= 4U) {
    return 4U;
  }
  return (register_count >= 2U) ? 2U : 1U;
}

inline void transpose_square_matrix(const std::uint64_t *in_matrix,
                                    std::uint64_t *out_matrix,
                                    std::uint32_t side) {
  for (std::uint32_t r = 0U; r < side; ++r) {
    for (std::uint32_t c = 0U; c < side; ++c) {
#pragma HLS PIPELINE II = 1
      out_matrix[c * side + r] = in_matrix[r * side + c];
    }
  }
}

inline void apply_automorphism_matrix(std::uint64_t *matrix_words,
                                      std::uint32_t side,
                                      std::uint64_t col_perm_info,
                                      std::uint64_t row_perm_info) {
  constexpr std::uint32_t kMaxSide = 16U;
  constexpr std::uint32_t kMaxWords = kMaxSide * kMaxSide;
  std::uint64_t stage1[kMaxWords];
  std::uint64_t stage2[kMaxWords];
  std::uint64_t row_buf[kMaxSide];
#pragma HLS ARRAY_PARTITION variable = row_buf cyclic factor = 8

  const std::uint64_t col_perm = sanitize_perm_info(col_perm_info, side);
  const std::uint64_t row_perm = sanitize_perm_info(row_perm_info, side);

  // Stage 1: column permutation.
  for (std::uint32_t r = 0U; r < side; ++r) {
    for (std::uint32_t c = 0U; c < side; ++c) {
#pragma HLS PIPELINE II = 1
      row_buf[c] = matrix_words[r * side + c];
    }
    apply_benes_network(row_buf, side, col_perm);
    for (std::uint32_t c = 0U; c < side; ++c) {
#pragma HLS PIPELINE II = 1
      stage1[r * side + c] = row_buf[c];
    }
  }

  // Stage 2: transpose.
  transpose_square_matrix(stage1, stage2, side);

  // Stage 3: row permutation.
  for (std::uint32_t r = 0U; r < side; ++r) {
    for (std::uint32_t c = 0U; c < side; ++c) {
#pragma HLS PIPELINE II = 1
      row_buf[c] = stage2[r * side + c];
    }
    apply_benes_network(row_buf, side, row_perm);
    for (std::uint32_t c = 0U; c < side; ++c) {
#pragma HLS PIPELINE II = 1
      stage1[r * side + c] = row_buf[c];
    }
  }

  // Stage 4: transpose.
  transpose_square_matrix(stage1, matrix_words, side);
}


inline std::uint32_t choose_transpose_side(std::uint32_t register_count,
                                           std::uint32_t side_hint) {
  if (register_count == 0U) {
    return 1U;
  }
  if (side_hint > 1U && is_power_of_two(side_hint) &&
      side_hint * side_hint <= register_count && side_hint <= 32U) {
    return side_hint;
  }
  std::uint32_t side = 1U;
  while ((side << 1U) * (side << 1U) <= register_count && (side << 1U) <= 32U) {
    side <<= 1U;
  }
  return side;
}

inline void transpose_full_stream(const std::uint64_t *in_cols,
                                  std::uint64_t *out_cols,
                                  std::uint32_t row_size,
                                  std::uint32_t col_size) {
  if (row_size == 0U || col_size == 0U || (row_size % col_size) != 0U) {
    return;
  }
  const std::uint32_t ratio = row_size / col_size;
  for (std::uint32_t i = 0U; i < col_size; ++i) {
    for (std::uint32_t j = 0U; j < col_size; ++j) {
      for (std::uint32_t k = 0U; k < ratio; ++k) {
#pragma HLS PIPELINE II = 1
        out_cols[j * row_size + i * ratio + k] =
            in_cols[i * row_size + j * ratio + k];
      }
    }
  }
}

inline void transpose_network_permutation(const std::uint64_t *in_data,
                                          std::uint64_t *out_data,
                                          std::uint32_t cluster_num,
                                          std::uint32_t row_size) {
  if (cluster_num == 0U || row_size == 0U || (row_size % cluster_num) != 0U) {
    return;
  }
  const std::uint32_t col_size = row_size / cluster_num;
  for (std::uint32_t block = 0U; block < col_size; ++block) {
    for (std::uint32_t src_id = 0U; src_id < cluster_num; ++src_id) {
      for (std::uint32_t src_elem = 0U; src_elem < cluster_num; ++src_elem) {
#pragma HLS PIPELINE II = 1
        const std::uint32_t dest_id = src_elem;
        const std::uint32_t dest_elem = src_id;
        out_data[dest_id * row_size + block * cluster_num + dest_elem] =
            in_data[src_id * row_size + block * cluster_num + src_elem];
      }
    }
  }
}


inline bool is_montgomery_opcode(std::uint32_t opcode) {
  return opcode == kOpMul || opcode == kOpMup || opcode == kOpMus;
}

inline bool is_base_conv_opcode(std::uint32_t opcode) {
  return opcode == kOpBci || opcode == kOpPl1 || opcode == kOpBcw ||
         opcode == kOpRsi || opcode == kOpRsv || opcode == kOpMod;
}

inline bool is_general_register_operand(std::uint32_t operand,
                                        std::uint32_t register_count) {
  return register_count != 0U && operand < register_count;
}

inline std::uint32_t decode_bcu_id(std::uint32_t operand) {
  if ((operand & 0xF00U) == 0xA00U) {
    return operand & 0xFFU;
  }
  return operand;
}


}  // namespace cinnamon_hls_kernel

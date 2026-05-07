#ifndef CINNAMON_HLS_CSIM_TEST_API_HPP_
#define CINNAMON_HLS_CSIM_TEST_API_HPP_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include "kernel_common.hpp"
#include "kernel_payload_common.hpp"

namespace cinnamon_hls_test {

using PayloadKernelFn = void (*)(const std::uint64_t *instructions,
                                 std::uint64_t *control_words,
                                 std::uint64_t *payload_words,
                                 std::uint64_t *outputs,
                                 std::uint32_t instruction_count,
                                 std::uint32_t control_count,
                                 std::uint32_t payload_count,
                                 std::uint32_t output_count,
                                 std::uint32_t partition_id);

using StreamKernelFn = void (*)(const std::uint64_t *instructions,
                                const std::uint64_t *inputs,
                                std::uint64_t *outputs,
                                std::uint32_t instruction_count,
                                std::uint32_t input_count,
                                std::uint32_t output_count,
                                std::uint32_t partition_id);

constexpr std::uint32_t kOutputHeaderWords = cinnamon_hls_kernel::kOutputHeaderWords;

struct InstructionSpec {
  std::uint32_t opcode = 0U;
  std::uint32_t dst = 0U;
  std::uint32_t src0 = 0U;
  std::uint32_t src1 = 0U;
  std::uint32_t rns = 0U;
  std::uint32_t flags = 0U;
  std::int32_t imm0 = 0;
  std::int32_t imm1 = 0;
  std::uint64_t aux = 0ULL;
  std::uint64_t line_crc = 0ULL;
};

struct RnsModulus {
  std::uint32_t id = 0U;
  std::uint64_t modulus = 0ULL;
};

struct HandleMetadata {
  std::uint32_t rns_base_id = 0U;
  std::uint32_t flags = 0U;
};

struct TokenBinding {
  std::uint64_t token = 0ULL;
  std::uint32_t handle_id = 0U;
};

struct PayloadCaseSpec {
  std::uint32_t register_count = 0U;
  std::uint32_t coeff_count = 0U;
  std::uint32_t handle_count = 0U;
  std::uint32_t handle_capacity = 0U;
  std::vector<RnsModulus> rns_moduli;
  std::vector<std::uint32_t> register_handles;
  std::vector<HandleMetadata> handle_metadata;
  std::vector<TokenBinding> input_tokens;
  std::vector<TokenBinding> output_tokens;
  std::vector<std::uint64_t> extra_control_words;
  std::vector<std::uint64_t> payload_words;
};

struct PayloadBuffers {
  std::vector<std::uint64_t> control_words;
  std::vector<std::uint64_t> payload_words;
  std::vector<std::uint64_t> outputs;
};

struct StreamTokenValue {
  std::uint64_t token = 0ULL;
  std::uint64_t value = 0ULL;
};

inline std::uint64_t pack_instruction_word0(std::uint32_t opcode, std::uint32_t dst,
                                            std::uint32_t src0, std::uint32_t src1,
                                            std::uint32_t rns, std::uint32_t flags) {
  return (static_cast<std::uint64_t>(opcode) & 0xFFULL) |
         ((static_cast<std::uint64_t>(dst) & 0xFFFULL) << 8U) |
         ((static_cast<std::uint64_t>(src0) & 0xFFFULL) << 20U) |
         ((static_cast<std::uint64_t>(src1) & 0xFFFULL) << 32U) |
         ((static_cast<std::uint64_t>(rns) & 0xFFFULL) << 44U) |
         ((static_cast<std::uint64_t>(flags) & 0xFFULL) << 56U);
}

inline std::uint64_t pack_instruction_word1(std::int32_t imm0, std::int32_t imm1) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm0)) &
          0xFFFFFFFFULL) |
         ((static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm1)) &
           0xFFFFFFFFULL)
          << 32U);
}

inline std::vector<std::uint64_t> encode_instructions(
    const std::vector<InstructionSpec> &instructions) {
  std::vector<std::uint64_t> words;
  words.reserve(instructions.size() * cinnamon_hls_kernel::kInstructionWordStride);
  for (const auto &inst : instructions) {
    words.push_back(pack_instruction_word0(inst.opcode, inst.dst, inst.src0, inst.src1,
                                           inst.rns, inst.flags));
    words.push_back(pack_instruction_word1(inst.imm0, inst.imm1));
    words.push_back(inst.aux);
    words.push_back(inst.line_crc);
  }
  return words;
}

inline std::uint32_t payload_coeff_offset(std::uint32_t coeff_count,
                                          std::uint32_t handle_id) {
  assert(handle_id > 0U);
  return (handle_id - 1U) * coeff_count;
}

inline void write_handle_payload(std::vector<std::uint64_t> &payload_words,
                                 std::uint32_t coeff_count, std::uint32_t handle_id,
                                 const std::vector<std::uint64_t> &values) {
  const std::uint32_t base = payload_coeff_offset(coeff_count, handle_id);
  const std::size_t required_size = static_cast<std::size_t>(base) + coeff_count;
  if (payload_words.size() < required_size) {
    payload_words.resize(required_size, 0ULL);
  }
  for (std::uint32_t i = 0U; i < coeff_count; ++i) {
    payload_words[base + i] = (i < values.size()) ? values[i] : 0ULL;
  }
}

inline std::uint32_t payload_control_word_count(const PayloadCaseSpec &spec) {
  return cinnamon_hls_kernel::kPayloadHeaderWords +
         static_cast<std::uint32_t>(spec.rns_moduli.size()) * 2U +
         spec.register_count + (spec.handle_capacity * 2U) +
         static_cast<std::uint32_t>(spec.input_tokens.size()) * 2U +
         static_cast<std::uint32_t>(spec.output_tokens.size()) * 2U;
}

inline std::vector<TokenBinding> sort_token_bindings(
    const std::vector<TokenBinding> &entries) {
  std::vector<TokenBinding> sorted = entries;
  std::sort(sorted.begin(), sorted.end(),
            [](const TokenBinding &lhs, const TokenBinding &rhs) {
              return lhs.token < rhs.token;
            });
  return sorted;
}

inline PayloadBuffers build_payload_buffers(const PayloadCaseSpec &spec,
                                           std::uint32_t output_count_override = 0U) {
  PayloadBuffers buffers;

  const std::uint32_t base_control_count = payload_control_word_count(spec);
  const std::uint32_t control_count =
      base_control_count + static_cast<std::uint32_t>(spec.extra_control_words.size());
  buffers.control_words.assign(control_count, 0ULL);

  const std::uint32_t payload_capacity = spec.handle_capacity * spec.coeff_count;
  const std::size_t payload_size = std::max<std::size_t>(payload_capacity,
                                                          spec.payload_words.size());
  buffers.payload_words.assign(payload_size, 0ULL);
  std::copy(spec.payload_words.begin(), spec.payload_words.end(),
            buffers.payload_words.begin());

  const std::uint32_t output_count =
      (output_count_override != 0U)
          ? output_count_override
          : (kOutputHeaderWords + spec.register_count);
  buffers.outputs.assign(output_count, 0ULL);

  buffers.control_words[0] = cinnamon_hls_kernel::kPayloadControlMagic;
  buffers.control_words[1] = cinnamon_hls_kernel::kPayloadControlVersion;
  buffers.control_words[2] = spec.register_count;
  buffers.control_words[3] = spec.coeff_count;
  buffers.control_words[4] = static_cast<std::uint32_t>(spec.rns_moduli.size());
  buffers.control_words[5] = spec.handle_count;
  buffers.control_words[6] = spec.handle_capacity;
  buffers.control_words[7] = static_cast<std::uint32_t>(spec.input_tokens.size());
  buffers.control_words[8] = static_cast<std::uint32_t>(spec.output_tokens.size());

  std::uint32_t cursor = cinnamon_hls_kernel::kPayloadHeaderWords;

  for (std::uint32_t i = 0U; i < spec.rns_moduli.size(); ++i) {
    buffers.control_words[cursor + (i * 2U)] = spec.rns_moduli[i].id;
    buffers.control_words[cursor + (i * 2U) + 1U] = spec.rns_moduli[i].modulus;
  }
  cursor += static_cast<std::uint32_t>(spec.rns_moduli.size()) * 2U;

  for (std::uint32_t i = 0U; i < spec.register_count; ++i) {
    buffers.control_words[cursor + i] =
        (i < spec.register_handles.size()) ? spec.register_handles[i] : 0ULL;
  }
  cursor += spec.register_count;

  for (std::uint32_t i = 0U; i < spec.handle_capacity; ++i) {
    const HandleMetadata meta = (i < spec.handle_metadata.size())
                                    ? spec.handle_metadata[i]
                                    : HandleMetadata{};
    buffers.control_words[cursor + (i * 2U)] = meta.rns_base_id;
    buffers.control_words[cursor + (i * 2U) + 1U] = meta.flags;
  }
  cursor += spec.handle_capacity * 2U;

  const std::vector<TokenBinding> input_tokens = sort_token_bindings(spec.input_tokens);
  for (std::uint32_t i = 0U; i < input_tokens.size(); ++i) {
    buffers.control_words[cursor + (i * 2U)] = input_tokens[i].token;
    buffers.control_words[cursor + (i * 2U) + 1U] = input_tokens[i].handle_id;
  }
  cursor += static_cast<std::uint32_t>(input_tokens.size()) * 2U;

  const std::vector<TokenBinding> output_tokens = sort_token_bindings(spec.output_tokens);
  for (std::uint32_t i = 0U; i < output_tokens.size(); ++i) {
    buffers.control_words[cursor + (i * 2U)] = output_tokens[i].token;
    buffers.control_words[cursor + (i * 2U) + 1U] = output_tokens[i].handle_id;
  }
  cursor += static_cast<std::uint32_t>(output_tokens.size()) * 2U;

  std::copy(spec.extra_control_words.begin(), spec.extra_control_words.end(),
            buffers.control_words.begin() + cursor);
  return buffers;
}

inline void run_payload_kernel(PayloadKernelFn kernel,
                               const std::vector<std::uint64_t> &instruction_words,
                               PayloadBuffers &buffers,
                               std::uint32_t partition_id = 0U) {
  assert((instruction_words.size() % cinnamon_hls_kernel::kInstructionWordStride) == 0U);
  kernel(instruction_words.data(), buffers.control_words.data(),
         buffers.payload_words.data(), buffers.outputs.data(),
         static_cast<std::uint32_t>(instruction_words.size()),
         static_cast<std::uint32_t>(buffers.control_words.size()),
         static_cast<std::uint32_t>(buffers.payload_words.size()),
         static_cast<std::uint32_t>(buffers.outputs.size()), partition_id);
}

inline std::vector<std::uint64_t> build_stream_inputs(
    std::uint32_t register_count, std::uint64_t modulus,
    const std::vector<std::uint64_t> &register_words,
    const std::vector<StreamTokenValue> &token_values = {},
    bool include_empty_token_table = false) {
  std::vector<std::uint64_t> inputs;
  inputs.reserve(3U + register_count + 1U + (token_values.size() * 2U));
  inputs.push_back(cinnamon_hls_kernel::kInputMagic);
  inputs.push_back(register_count);
  inputs.push_back(modulus);
  for (std::uint32_t i = 0U; i < register_count; ++i) {
    inputs.push_back((i < register_words.size()) ? register_words[i] : 0ULL);
  }
  if (!token_values.empty() || include_empty_token_table) {
    std::vector<StreamTokenValue> sorted = token_values;
    std::sort(sorted.begin(), sorted.end(),
              [](const StreamTokenValue &lhs, const StreamTokenValue &rhs) {
                return lhs.token < rhs.token;
              });
    inputs.push_back(static_cast<std::uint64_t>(sorted.size()));
    for (const auto &entry : sorted) {
      inputs.push_back(entry.token);
      inputs.push_back(entry.value);
    }
  }
  return inputs;
}

inline void run_stream_kernel(StreamKernelFn kernel,
                              const std::vector<std::uint64_t> &instruction_words,
                              const std::vector<std::uint64_t> &inputs,
                              std::vector<std::uint64_t> &outputs,
                              std::uint32_t partition_id = 0U) {
  assert((instruction_words.size() % cinnamon_hls_kernel::kInstructionWordStride) == 0U);
  kernel(instruction_words.data(), inputs.data(), outputs.data(),
         static_cast<std::uint32_t>(instruction_words.size()),
         static_cast<std::uint32_t>(inputs.size()),
         static_cast<std::uint32_t>(outputs.size()), partition_id);
}

}  // namespace cinnamon_hls_test

#endif  // CINNAMON_HLS_CSIM_TEST_API_HPP_

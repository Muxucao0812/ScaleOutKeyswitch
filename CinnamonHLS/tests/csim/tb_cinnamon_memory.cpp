#include <cstdint>
#include <iostream>
#include <vector>

#include "cinnamon_hls/csim_test_api.hpp"

extern "C" void cinnamon_memory(const std::uint64_t *instructions,
                                std::uint64_t *control_words,
                                std::uint64_t *payload_words,
                                std::uint64_t *outputs,
                                std::uint32_t instruction_count,
                                std::uint32_t control_count,
                                std::uint32_t payload_count,
                                std::uint32_t output_count,
                                std::uint32_t partition_id);

namespace {
constexpr std::uint64_t kInToken = 0x100ULL;
constexpr std::uint64_t kOutToken = 0x200ULL;
}

int main() {
  using namespace cinnamon_hls_kernel;
  using namespace cinnamon_hls_test;

  PayloadCaseSpec spec;
  spec.register_count = 8U;
  spec.coeff_count = 4U;
  spec.handle_count = 1U;
  spec.handle_capacity = 4U;
  spec.rns_moduli = {{0U, 97ULL}};
  spec.handle_metadata = {{0U, 1U}};
  spec.input_tokens = {{kInToken, 1U}};
  spec.output_tokens = {{kOutToken, 0U}};
  write_handle_payload(spec.payload_words, spec.coeff_count, 1U,
                       {11ULL, 22ULL, 33ULL, 44ULL});

  const std::vector<std::uint64_t> instructions = encode_instructions({
      {kOpLoad, 3U, kTokenOperandId, 0U, 0U, 0U, 0, 0, kInToken, 0ULL},
      {kOpMov, 4U, 3U, 0U, 0U, 0U, 0, 0, 0ULL, 0ULL},
      {kOpStore, 0U, 4U, kTokenOperandId, 0U, 0U, 0, 0, kOutToken, 0ULL},
  });

  std::cout << "[TB][memory] start: regs=" << spec.register_count
            << ", coeff=" << spec.coeff_count
            << ", inst=" << (instructions.size() / kInstructionWordStride) << '\n';

  PayloadBuffers buffers = build_payload_buffers(spec);
  run_payload_kernel(cinnamon_memory, instructions, buffers, 0U);

  if (buffers.outputs[0] != 0ULL) {
    std::cerr << "[TB][memory][FAIL] status=" << buffers.outputs[0] << '\n';
    return 1;
  }
  if (buffers.outputs[cinnamon_hls_test::kOutputHeaderWords + 3U] != 1ULL ||
      buffers.outputs[cinnamon_hls_test::kOutputHeaderWords + 4U] != 1ULL) {
    std::cerr << "[TB][memory][FAIL] register handles mismatch: r3="
              << buffers.outputs[cinnamon_hls_test::kOutputHeaderWords + 3U]
              << ", r4="
              << buffers.outputs[cinnamon_hls_test::kOutputHeaderWords + 4U]
              << ", expected {1,1}\n";
    return 2;
  }

  const std::uint32_t output_handle_word =
      kPayloadHeaderWords + static_cast<std::uint32_t>(spec.rns_moduli.size()) * 2U +
      spec.register_count + (spec.handle_capacity * 2U) +
      static_cast<std::uint32_t>(spec.input_tokens.size()) * 2U + 1U;
  if (buffers.control_words[output_handle_word] != 1ULL) {
    std::cerr << "[TB][memory][FAIL] output token handle mismatch: got "
              << buffers.control_words[output_handle_word] << ", expected 1\n";
    return 3;
  }
  std::cout << "[TB][memory] PASS: out_token_handle="
            << buffers.control_words[output_handle_word] << '\n';
  return 0;
}

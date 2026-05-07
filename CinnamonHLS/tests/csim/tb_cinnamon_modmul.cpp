#include <cstdint>
#include <iostream>
#include <vector>

#include "cinnamon_hls/csim_test_api.hpp"

extern "C" void cinnamon_modmul(const std::uint64_t *instructions,
                                    std::uint64_t *control_words,
                                    std::uint64_t *payload_words,
                                    std::uint64_t *outputs,
                                    std::uint32_t instruction_count,
                                    std::uint32_t control_count,
                                    std::uint32_t payload_count,
                                    std::uint32_t output_count,
                                    std::uint32_t partition_id);

int main() {
  using namespace cinnamon_hls_kernel;
  using namespace cinnamon_hls_test;

  constexpr std::uint64_t q = 268042241ULL;
  constexpr std::uint64_t expected_product = 261958568ULL;

  PayloadCaseSpec spec;
  spec.register_count = 3U;
  spec.coeff_count = 1U;
  spec.handle_count = 2U;
  spec.handle_capacity = 4U;
  spec.rns_moduli = {{0U, q}};
  spec.register_handles = {1U, 2U};
  spec.handle_metadata = {{0U, 0U}, {0U, 0U}};
  write_handle_payload(spec.payload_words, spec.coeff_count, 1U, {34234ULL});
  write_handle_payload(spec.payload_words, spec.coeff_count, 2U, {7652ULL});

  const std::vector<std::uint64_t> instructions = encode_instructions({
      {kOpMul, 2U, 0U, 1U, 0U, 0U, 0, 0, 0ULL, 0ULL},
  });

  std::cout << "[TB][modmul] start: q=" << q
            << ", inst=" << (instructions.size() / kInstructionWordStride) << '\n';

  PayloadBuffers buffers = build_payload_buffers(spec);
  run_payload_kernel(cinnamon_modmul, instructions, buffers, 0U);

  if (buffers.outputs[0] != 0ULL) {
    std::cerr << "[TB][modmul][FAIL] status mismatch: outputs[0]="
              << buffers.outputs[0]
              << '\n';
    return 1;
  }
  if (buffers.outputs[1] != 1ULL) {
    std::cerr << "[TB][modmul][FAIL] executed mismatch: expected 1 got "
              << buffers.outputs[1] << '\n';
    return 2;
  }
  if (buffers.outputs[cinnamon_hls_test::kOutputHeaderWords + 2U] != 3ULL) {
    std::cerr << "[TB][modmul][FAIL] dst handle mismatch: expected 3 got "
              << buffers.outputs[cinnamon_hls_test::kOutputHeaderWords + 2U]
              << '\n';
    return 3;
  }
  if (buffers.control_words[5] != 3ULL) {
    std::cerr << "[TB][modmul][FAIL] handle count mismatch: expected 3 got "
              << buffers.control_words[5] << '\n';
    return 4;
  }

  const std::uint32_t handle3_base = payload_coeff_offset(spec.coeff_count, 3U);
  if (buffers.payload_words[handle3_base] != expected_product) {
    std::cerr << "[TB][modmul][FAIL] payload mismatch: expected "
              << expected_product << " got " << buffers.payload_words[handle3_base]
              << '\n';
    return 5;
  }
  std::cout << "[TB][modmul] PASS: product=" << buffers.payload_words[handle3_base]
            << '\n';
  return 0;
}

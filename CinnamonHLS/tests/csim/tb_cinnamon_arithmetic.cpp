#include <cstdint>
#include <iostream>
#include <vector>

#include "cinnamon_hls/csim_test_api.hpp"

extern "C" void cinnamon_arithmetic(const std::uint64_t *instructions,
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

  PayloadCaseSpec spec;
  spec.register_count = 6U;
  spec.coeff_count = 4U;
  spec.handle_count = 2U;
  spec.handle_capacity = 4U;
  spec.rns_moduli = {{0U, 97ULL}};
  spec.register_handles = {1U, 2U};
  spec.handle_metadata = {{0U, 1U}, {0U, 1U}};
  write_handle_payload(spec.payload_words, spec.coeff_count, 1U,
                       {96ULL, 1ULL, 2ULL, 3ULL});
  write_handle_payload(spec.payload_words, spec.coeff_count, 2U,
                       {1ULL, 2ULL, 3ULL, 4ULL});

  const std::vector<std::uint64_t> instructions = encode_instructions({
      {kOpAdd, 2U, 0U, 1U, 0U, 0U, 0, 0, 0ULL, 0ULL},
  });

  std::cout << "[TB][arithmetic] start: regs=" << spec.register_count
            << ", coeff=" << spec.coeff_count
            << ", inst=" << (instructions.size() / kInstructionWordStride) << '\n';

  PayloadBuffers buffers = build_payload_buffers(spec);
  run_payload_kernel(cinnamon_arithmetic, instructions, buffers, 0U);

  if (buffers.outputs[0] != 0ULL) {
    std::cerr << "[TB][arithmetic][FAIL] status=" << buffers.outputs[0] << '\n';
    return 1;
  }
  if (buffers.outputs[cinnamon_hls_test::kOutputHeaderWords + 2U] != 3ULL) {
    std::cerr << "[TB][arithmetic][FAIL] dst handle mismatch: got "
              << buffers.outputs[cinnamon_hls_test::kOutputHeaderWords + 2U]
              << ", expected 3\n";
    return 2;
  }
  if (buffers.control_words[5] != 3ULL) {
    std::cerr << "[TB][arithmetic][FAIL] handle_count mismatch: got "
              << buffers.control_words[5] << ", expected 3\n";
    return 3;
  }

  const std::uint32_t handle3_base = payload_coeff_offset(spec.coeff_count, 3U);
  if (buffers.payload_words[handle3_base + 0U] != 0ULL ||
      buffers.payload_words[handle3_base + 1U] != 3ULL ||
      buffers.payload_words[handle3_base + 2U] != 5ULL ||
      buffers.payload_words[handle3_base + 3U] != 7ULL) {
    std::cerr << "[TB][arithmetic][FAIL] payload(handle3) mismatch: got {"
              << buffers.payload_words[handle3_base + 0U] << ", "
              << buffers.payload_words[handle3_base + 1U] << ", "
              << buffers.payload_words[handle3_base + 2U] << ", "
              << buffers.payload_words[handle3_base + 3U]
              << "}, expected {0,3,5,7}\n";
    return 4;
  }
  std::cout << "[TB][arithmetic] PASS: handle3_payload={"
            << buffers.payload_words[handle3_base + 0U] << ", "
            << buffers.payload_words[handle3_base + 1U] << ", "
            << buffers.payload_words[handle3_base + 2U] << ", "
            << buffers.payload_words[handle3_base + 3U] << "}\n";
  return 0;
}

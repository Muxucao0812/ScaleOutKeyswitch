#include <cstdint>
#include <iostream>
#include <vector>

#include "cinnamon_hls/csim_test_api.hpp"

extern "C" void cinnamon_base_conv(const std::uint64_t *instructions,
                                   std::uint64_t *control_words,
                                   std::uint64_t *payload_words,
                                   std::uint64_t *outputs,
                                   std::uint32_t instruction_count,
                                   std::uint32_t control_count,
                                   std::uint32_t payload_count,
                                   std::uint32_t output_count,
                                   std::uint32_t partition_id);

namespace {
constexpr std::uint32_t kBcuOperand0 = 0xA00U;
}

int main() {
  using namespace cinnamon_hls_kernel;
  using namespace cinnamon_hls_test;

  constexpr std::uint64_t q = 264634369ULL;

  PayloadCaseSpec spec;
  spec.register_count = 1U;
  spec.coeff_count = 1U;
  spec.handle_count = 1U;
  spec.handle_capacity = 4U;
  spec.rns_moduli = {{2U, q}, {12U, q}};
  spec.register_handles = {1U};
  spec.handle_metadata = {{2U, 0U}};
  spec.extra_control_words = {
      kPayloadExtraMagic,
      kPayloadExtraVersion,
      1U,   // bcu_unit_count
      1U,   // bcu_output_capacity
      1U,   // bcu_active_count
      1U,   // bci_entry_count
      7U,   // active line crc
      0U,   // bcu table entry
      7U,   // bci line crc
      0U,   // bci id
      1U,   // bci src count
      1U,   // bci dst count
      2U,   // bci src base id
      12U,  // bci dst base id
      1U,   // bci factor
  };
  write_handle_payload(spec.payload_words, spec.coeff_count, 1U, {73ULL});

  const std::vector<std::uint64_t> instructions = encode_instructions({
      {kOpBcw, kBcuOperand0, 0U, 0U, 2U, 0U, 0, 0, 0ULL, 0ULL},
  });

  std::cout << "[TB][base_conv] start: regs=" << spec.register_count
            << ", coeff=" << spec.coeff_count
            << ", inst=" << (instructions.size() / kInstructionWordStride) << '\n';

  PayloadBuffers buffers = build_payload_buffers(spec);
  run_payload_kernel(cinnamon_base_conv, instructions, buffers, 0U);

  if (buffers.outputs[0] != 0ULL) {
    std::cerr << "[TB][base_conv][FAIL] status mismatch: outputs[0]="
              << buffers.outputs[0] << '\n';
    return 1;
  }
  if (buffers.outputs[1] != 1ULL ||
      buffers.outputs[cinnamon_hls_test::kOutputHeaderWords] != 1ULL) {
    std::cerr << "[TB][base_conv][FAIL] output mismatch: executed="
              << buffers.outputs[1] << " reg0="
              << buffers.outputs[cinnamon_hls_test::kOutputHeaderWords] << '\n';
    return 2;
  }

  const std::uint32_t extra_base =
      kPayloadHeaderWords + static_cast<std::uint32_t>(spec.rns_moduli.size()) * 2U +
      spec.register_count + (spec.handle_capacity * 2U);
  if (buffers.control_words[5] != 2ULL ||
      buffers.control_words[extra_base + 7U] != 2ULL) {
    std::cerr << "[TB][base_conv][FAIL] handle/table mismatch: handle_count="
              << buffers.control_words[5] << " table0="
              << buffers.control_words[extra_base + 7U] << '\n';
    return 3;
  }

  const std::uint32_t handle_meta_offset =
      kPayloadHeaderWords + static_cast<std::uint32_t>(spec.rns_moduli.size()) * 2U +
      spec.register_count;
  if (buffers.control_words[handle_meta_offset + 2U] != 12ULL ||
      buffers.control_words[handle_meta_offset + 3U] != 0ULL) {
    std::cerr << "[TB][base_conv][FAIL] handle2 meta mismatch: {"
              << buffers.control_words[handle_meta_offset + 2U] << ", "
              << buffers.control_words[handle_meta_offset + 3U] << "}\n";
    return 4;
  }

  const std::uint32_t handle2_base = payload_coeff_offset(spec.coeff_count, 2U);
  if (buffers.payload_words[handle2_base] != 73ULL) {
    std::cerr << "[TB][base_conv][FAIL] payload mismatch: expected 73 got "
              << buffers.payload_words[handle2_base] << '\n';
    return 5;
  }
  std::cout << "[TB][base_conv] PASS: handle2_payload="
            << buffers.payload_words[handle2_base] << '\n';
  return 0;
}

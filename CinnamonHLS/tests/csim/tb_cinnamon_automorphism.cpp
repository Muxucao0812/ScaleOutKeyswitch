#include <cstdint>
#include <iostream>
#include <vector>

#include "cinnamon_hls/csim_test_api.hpp"

extern "C" void cinnamon_automorphism(const std::uint64_t *instructions,
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

  constexpr std::uint32_t step = 2U;

  PayloadCaseSpec spec;
  spec.register_count = 1U;
  spec.coeff_count = 16U;
  spec.handle_count = 1U;
  spec.handle_capacity = 4U;
  spec.rns_moduli = {{0U, 997ULL}};
  spec.register_handles = {1U};
  spec.handle_metadata = {{0U, kPayloadFlagIsNtt}};

  std::vector<std::uint64_t> values(spec.coeff_count, 0ULL);
  for (std::uint32_t i = 0U; i < spec.coeff_count; ++i) {
    values[i] = static_cast<std::uint64_t>(i + 1U);
  }
  write_handle_payload(spec.payload_words, spec.coeff_count, 1U, values);

  const std::vector<std::uint64_t> instructions = encode_instructions({
      {kOpRot, 0U, 0U, 0U, 0U, 0U, static_cast<std::int32_t>(step), 0, 0ULL, 0ULL},
  });

  std::cout << "[TB][automorphism] start: coeff=" << spec.coeff_count
            << ", step=" << step
            << ", inst=" << (instructions.size() / kInstructionWordStride) << '\n';

  PayloadBuffers buffers = build_payload_buffers(spec);
  run_payload_kernel(cinnamon_automorphism, instructions, buffers, 0U);

  if (buffers.outputs[0] != 0ULL) {
    std::cerr << "[TB][automorphism][FAIL] status=" << buffers.outputs[0] << '\n';
    return 1;
  }
  if (buffers.outputs[1] != 1ULL ||
      buffers.outputs[cinnamon_hls_test::kOutputHeaderWords] != 2ULL) {
    std::cerr << "[TB][automorphism][FAIL] handle mismatch: executed="
              << buffers.outputs[1] << ", reg0="
              << buffers.outputs[cinnamon_hls_test::kOutputHeaderWords]
              << ", expected {1,2}\n";
    return 2;
  }

  const std::uint32_t handle_meta_offset =
      kPayloadHeaderWords + static_cast<std::uint32_t>(spec.rns_moduli.size()) * 2U +
      spec.register_count;
  if (buffers.control_words[5] != 2ULL ||
      buffers.control_words[handle_meta_offset + 2U] != 0ULL ||
      buffers.control_words[handle_meta_offset + 3U] != kPayloadFlagIsNtt) {
    std::cerr << "[TB][automorphism][FAIL] metadata mismatch: handle_count="
              << buffers.control_words[5] << ", handle2_meta={"
              << buffers.control_words[handle_meta_offset + 2U] << ", "
              << buffers.control_words[handle_meta_offset + 3U]
              << "}, expected {2,{0,1}}\n";
    return 3;
  }

  const std::uint32_t galois_elt = galois_elt_from_step(spec.coeff_count, step);
  if (galois_elt == 0U) {
    std::cerr << "[TB][automorphism][FAIL] invalid galois element for step=" << step
              << '\n';
    return 4;
  }
  std::vector<std::uint64_t> expected(spec.coeff_count, 0ULL);
  apply_galois_ntt_permutation(values.data(), expected.data(), spec.coeff_count,
                               galois_elt);
  const std::uint32_t handle2_base = payload_coeff_offset(spec.coeff_count, 2U);
  for (std::uint32_t i = 0U; i < spec.coeff_count; ++i) {
    if (buffers.payload_words[handle2_base + i] != expected[i]) {
      std::cerr << "[TB][automorphism][FAIL] payload mismatch at index " << i
                << ": got " << buffers.payload_words[handle2_base + i]
                << ", expected " << expected[i] << '\n';
      return 5;
    }
  }
  std::cout << "[TB][automorphism] PASS: galois_elt=" << galois_elt << '\n';
  return 0;
}

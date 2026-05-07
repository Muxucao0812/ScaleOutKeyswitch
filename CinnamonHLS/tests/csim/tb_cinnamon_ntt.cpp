#include <cstdint>
#include <iostream>
#include <vector>

#include "cinnamon_hls/csim_test_api.hpp"

extern "C" void cinnamon_ntt(const std::uint64_t *instructions,
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

  {
    constexpr std::uint64_t mod = 1179649ULL;

    PayloadCaseSpec spec;
    spec.register_count = 1U;
    spec.coeff_count = 4U;
    spec.handle_count = 1U;
    spec.handle_capacity = 4U;
    spec.rns_moduli = {{1U, mod}};
    spec.register_handles = {1U};
    spec.handle_metadata = {{1U, kPayloadFlagIsNtt}};
    write_handle_payload(spec.payload_words, spec.coeff_count, 1U,
                         {0ULL, 8ULL, 4ULL, 12ULL});

    const std::vector<std::uint64_t> instructions = encode_instructions({
        {kOpNtt, 0U, 0U, 0U, 1U, 0U, 4, 0, 0ULL, 0ULL},
    });

    std::cout << "[TB][ntt] span4 start: mod=" << mod
              << ", inst=" << (instructions.size() / kInstructionWordStride) << '\n';

    PayloadBuffers buffers = build_payload_buffers(spec);
    run_payload_kernel(cinnamon_ntt, instructions, buffers, 0U);

    if (buffers.outputs[0] != 0ULL) {
      std::cerr << "[TB][ntt][FAIL] span4 status mismatch: outputs[0]="
                << buffers.outputs[0] << '\n';
      return 1;
    }
    if (buffers.outputs[1] != 1ULL ||
        buffers.outputs[cinnamon_hls_test::kOutputHeaderWords] != 2ULL) {
      std::cerr << "[TB][ntt][FAIL] span4 handle mismatch: executed="
                << buffers.outputs[1]
                << " reg0="
                << buffers.outputs[cinnamon_hls_test::kOutputHeaderWords] << '\n';
      return 2;
    }

    const std::uint32_t handle_meta_offset =
        kPayloadHeaderWords + static_cast<std::uint32_t>(spec.rns_moduli.size()) * 2U +
        spec.register_count;
    if (buffers.control_words[5] != 2ULL ||
        buffers.control_words[handle_meta_offset + 2U] != 1ULL ||
        buffers.control_words[handle_meta_offset + 3U] != kPayloadFlagIsNtt) {
      std::cerr << "[TB][ntt][FAIL] span4 metadata mismatch: handle_count="
                << buffers.control_words[5] << " handle2_meta={"
                << buffers.control_words[handle_meta_offset + 2U] << ", "
                << buffers.control_words[handle_meta_offset + 3U] << "}\n";
      return 3;
    }

    const std::uint32_t handle2_base = payload_coeff_offset(spec.coeff_count, 2U);
    if (buffers.payload_words[handle2_base + 0U] != 1146641ULL ||
        buffers.payload_words[handle2_base + 1U] != 419093ULL ||
        buffers.payload_words[handle2_base + 2U] != 2288ULL ||
        buffers.payload_words[handle2_base + 3U] != 791276ULL) {
      std::cerr << "[TB][ntt][FAIL] span4 payload mismatch: got={"
                << buffers.payload_words[handle2_base + 0U] << ", "
                << buffers.payload_words[handle2_base + 1U] << ", "
                << buffers.payload_words[handle2_base + 2U] << ", "
                << buffers.payload_words[handle2_base + 3U]
                << "} expected={1146641, 419093, 2288, 791276}\n";
      return 4;
    }
    std::cout << "[TB][ntt] span4 PASS\n";
  }

  {
    constexpr std::uint64_t mod = 786433ULL;

    PayloadCaseSpec spec;
    spec.register_count = 1U;
    spec.coeff_count = 8U;
    spec.handle_count = 1U;
    spec.handle_capacity = 4U;
    spec.rns_moduli = {{4U, mod}};
    spec.register_handles = {1U};
    spec.handle_metadata = {{4U, 0U}};
    write_handle_payload(spec.payload_words, spec.coeff_count, 1U,
                         {0ULL, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL, 7ULL});

    const std::vector<std::uint64_t> instructions = encode_instructions({
        {kOpNtt, 0U, 0U, 0U, 4U, 0U, 8, 0, 0ULL, 0ULL},
        {kOpInt, 0U, 0U, 0U, 4U, 0U, 8, 0, 0ULL, 0ULL},
    });

    std::cout << "[TB][ntt] roundtrip start: mod=" << mod
              << ", inst=" << (instructions.size() / kInstructionWordStride) << '\n';

    PayloadBuffers buffers = build_payload_buffers(spec);
    run_payload_kernel(cinnamon_ntt, instructions, buffers, 0U);

    if (buffers.outputs[0] != 0ULL) {
      std::cerr << "[TB][ntt][FAIL] roundtrip status mismatch: outputs[0]="
                << buffers.outputs[0] << '\n';
      return 5;
    }
    if (buffers.outputs[1] != 2ULL ||
        buffers.outputs[cinnamon_hls_test::kOutputHeaderWords] != 3ULL) {
      std::cerr << "[TB][ntt][FAIL] roundtrip handle mismatch: executed="
                << buffers.outputs[1] << " reg0="
                << buffers.outputs[cinnamon_hls_test::kOutputHeaderWords] << '\n';
      return 6;
    }

    const std::uint32_t handle_meta_offset =
        kPayloadHeaderWords + static_cast<std::uint32_t>(spec.rns_moduli.size()) * 2U +
        spec.register_count;
    if (buffers.control_words[5] != 3ULL ||
        buffers.control_words[handle_meta_offset + 4U] != 4ULL ||
        buffers.control_words[handle_meta_offset + 5U] != 0ULL) {
      std::cerr << "[TB][ntt][FAIL] roundtrip metadata mismatch: handle_count="
                << buffers.control_words[5] << " handle3_meta={"
                << buffers.control_words[handle_meta_offset + 4U] << ", "
                << buffers.control_words[handle_meta_offset + 5U] << "}\n";
      return 7;
    }

    const std::uint32_t handle3_base = payload_coeff_offset(spec.coeff_count, 3U);
    for (std::uint32_t i = 0U; i < spec.coeff_count; ++i) {
      if (buffers.payload_words[handle3_base + i] != i) {
        std::cerr << "[TB][ntt][FAIL] roundtrip payload mismatch at index " << i
                  << ": expected " << i << " got "
                  << buffers.payload_words[handle3_base + i] << '\n';
        return 8;
      }
    }
    std::cout << "[TB][ntt] roundtrip PASS\n";
  }

  std::cout << "[TB][ntt] PASS\n";
  return 0;
}

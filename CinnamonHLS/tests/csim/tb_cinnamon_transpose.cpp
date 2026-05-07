#include <cstdint>
#include <iostream>
#include <vector>

#include "cinnamon_hls/csim_test_api.hpp"

extern "C" void cinnamon_transpose(const std::uint64_t *instructions,
                                   const std::uint64_t *inputs,
                                   std::uint64_t *outputs,
                                   std::uint32_t instruction_count,
                                   std::uint32_t input_count,
                                   std::uint32_t output_count,
                                   std::uint32_t partition_id);

int main() {
  using namespace cinnamon_hls_kernel;
  using namespace cinnamon_hls_test;

  constexpr std::uint64_t mod = 257ULL;
  constexpr std::uint32_t reg_count = 16U;

  const std::vector<std::uint64_t> inputs = build_stream_inputs(
      reg_count, mod,
      {
          0ULL, 4ULL, 8ULL,  12ULL, 1ULL, 5ULL, 9ULL,  13ULL,
          2ULL, 6ULL, 10ULL, 14ULL, 3ULL, 7ULL, 11ULL, 15ULL,
      });

  const std::vector<std::uint64_t> instructions = encode_instructions({
      {kOpRec, 0U, 0U, 0U, 4U, 0U, 0, 0, 0ULL, 0ULL},
  });

  std::cout << "[TB][transpose] start: regs=" << reg_count
            << ", inst=" << (instructions.size() / kInstructionWordStride) << '\n';

  std::vector<std::uint64_t> outputs(cinnamon_hls_test::kOutputHeaderWords + reg_count,
                                     0ULL);
  run_stream_kernel(cinnamon_transpose, instructions, inputs, outputs, 0U);

  if (outputs[0] != 0ULL) {
    std::cerr << "[TB][transpose][FAIL] status=" << outputs[0] << '\n';
    return 1;
  }
  for (std::uint32_t i = 0U; i < reg_count; ++i) {
    if (outputs[cinnamon_hls_test::kOutputHeaderWords + i] != i) {
      std::cerr << "[TB][transpose][FAIL] output mismatch at index " << i
                << ": got " << outputs[cinnamon_hls_test::kOutputHeaderWords + i]
                << ", expected " << i << '\n';
      return 2;
    }
  }
  std::cout << "[TB][transpose] PASS\n";
  return 0;
}

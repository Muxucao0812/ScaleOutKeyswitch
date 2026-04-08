#include <cstdint>

extern "C" void cinnamon_ntt(const std::uint64_t *instructions,
                             const std::uint64_t *inputs, std::uint64_t *outputs,
                             std::uint32_t instruction_count,
                             std::uint32_t input_count, std::uint32_t output_count,
                             std::uint32_t partition_id);

namespace {
constexpr std::uint64_t kInputMagic = 0x43494E4E414D4F4EULL;
constexpr std::uint32_t kHeaderWords = 6U;
constexpr std::uint32_t kAxiDepth = 4096U;

std::uint64_t encode_word0(std::uint32_t opcode, std::uint32_t dst,
                           std::uint32_t src0, std::uint32_t src1,
                           std::uint32_t rns, std::uint32_t flags) {
  return (static_cast<std::uint64_t>(opcode) & 0xFFULL) |
         ((static_cast<std::uint64_t>(dst) & 0xFFFULL) << 8U) |
         ((static_cast<std::uint64_t>(src0) & 0xFFFULL) << 20U) |
         ((static_cast<std::uint64_t>(src1) & 0xFFFULL) << 32U) |
         ((static_cast<std::uint64_t>(rns) & 0xFFFULL) << 44U) |
         ((static_cast<std::uint64_t>(flags) & 0xFFULL) << 56U);
}

std::uint64_t encode_word1(std::int32_t imm0, std::int32_t imm1) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm0)) &
          0xFFFFFFFFULL) |
         ((static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm1)) &
           0xFFFFFFFFULL)
          << 32U);
}
}  // namespace

int main() {
  constexpr std::uint64_t mod = 1179649ULL;
  constexpr std::uint32_t reg_count = 8U;
  constexpr std::uint32_t input_words = 11U;
  constexpr std::uint32_t instruction_words = 4U;
  std::uint64_t inputs[kAxiDepth] = {
      kInputMagic, reg_count, mod, 0ULL, 8ULL, 4ULL, 12ULL, 0ULL, 0ULL, 0ULL, 0ULL};
  std::uint64_t instructions[kAxiDepth] = {
      encode_word0(12U, 0U, 0U, 0U, 1U, 0U), encode_word1(4, 0), 0ULL, 0ULL};
  std::uint64_t outputs[kAxiDepth] = {};

  cinnamon_ntt(instructions, inputs, outputs,
               instruction_words,
               input_words,
               static_cast<std::uint32_t>(kHeaderWords + reg_count), 0U);

  if (outputs[0] != 0ULL) {
    return 1;
  }
  if (outputs[kHeaderWords + 0] != 23008ULL ||
      outputs[kHeaderWords + 1] != 1132065ULL ||
      outputs[kHeaderWords + 2] != 384471ULL ||
      outputs[kHeaderWords + 3] != 819754ULL) {
    return 2;
  }
  return 0;
}

#include <cstdint>

extern "C" void cinnamon_automorphism(const std::uint64_t *instructions,
                                      const std::uint64_t *inputs,
                                      std::uint64_t *outputs,
                                      std::uint32_t instruction_count,
                                      std::uint32_t input_count,
                                      std::uint32_t output_count,
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
  constexpr std::uint64_t mod = 997ULL;
  constexpr std::uint32_t reg_count = 8U;
  constexpr std::uint32_t input_words = 11U;
  constexpr std::uint32_t instruction_words = 4U;
  std::uint64_t inputs[kAxiDepth] = {kInputMagic, reg_count, mod, 7ULL, 6ULL, 5ULL, 4ULL,
                                     3ULL,       2ULL,      1ULL, 0ULL};
  const std::uint64_t perm_info = (static_cast<std::uint64_t>(2911U) << 12U) | 4095ULL;
  std::uint64_t instructions[kAxiDepth] = {
      encode_word0(14U, 0U, 0U, 0U, 0U, 0U), encode_word1(0, 0), perm_info, 0ULL};
  std::uint64_t outputs[kAxiDepth] = {};
  cinnamon_automorphism(
      instructions, inputs, outputs,
      instruction_words,
      input_words,
      static_cast<std::uint32_t>(kHeaderWords + reg_count), 0U);

  if (outputs[0] != 0ULL) {
    return 1;
  }
  if (outputs[kHeaderWords + 0] != 5ULL || outputs[kHeaderWords + 1] != 2ULL ||
      outputs[kHeaderWords + 2] != 7ULL || outputs[kHeaderWords + 3] != 4ULL ||
      outputs[kHeaderWords + 4] != 1ULL || outputs[kHeaderWords + 5] != 6ULL ||
      outputs[kHeaderWords + 6] != 3ULL || outputs[kHeaderWords + 7] != 0ULL) {
    return 2;
  }
  return 0;
}

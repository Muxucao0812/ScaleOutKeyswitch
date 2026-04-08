#include <cstdint>

extern "C" void cinnamon_memory(const std::uint64_t *instructions,
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
constexpr std::uint32_t kTokenOperandId = 0xFFEU;

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
  constexpr std::uint64_t mod = 97ULL;
  constexpr std::uint64_t token_key = 0x12345678ULL;
  constexpr std::uint32_t reg_count = 8U;
  constexpr std::uint32_t input_words = 14U;
  constexpr std::uint32_t instruction_words = 12U;
  std::uint64_t inputs[kAxiDepth] = {
      kInputMagic, reg_count, mod,
      0ULL,        0ULL,      0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL,
      1ULL, token_key, 55ULL,
  };
  std::uint64_t instructions[kAxiDepth] = {
      encode_word0(1U, 3U, kTokenOperandId, 0U, 0U, 0U), encode_word1(0, 0), token_key, 0ULL,
      encode_word0(3U, 4U, 3U, 0U, 0U, 0U),              encode_word1(0, 0), 0ULL,      0ULL,
      encode_word0(2U, 0U, 4U, kTokenOperandId, 0U, 0U), encode_word1(0, 0), token_key, 0ULL,
  };
  std::uint64_t outputs[kAxiDepth] = {};

  cinnamon_memory(
      instructions, inputs, outputs,
      instruction_words,
      input_words,
      static_cast<std::uint32_t>(kHeaderWords + reg_count), 0U);

  if (outputs[0] != 0ULL) {
    return 1;
  }
  if (outputs[kHeaderWords + 3] != 55ULL || outputs[kHeaderWords + 4] != 55ULL) {
    return 2;
  }
  return 0;
}

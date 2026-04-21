#include <cstdint>

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
constexpr std::uint64_t kPayloadMagic = 0x43494E4E5041594CULL;
constexpr std::uint64_t kPayloadVersion = 1ULL;
constexpr std::uint32_t kHeaderWords = 6U;
constexpr std::uint32_t kAxiDepth = 4096U;
constexpr std::uint32_t kTokenOperandId = 0xFFEU;
constexpr std::uint64_t kInToken = 0x100ULL;
constexpr std::uint64_t kOutToken = 0x200ULL;

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
  constexpr std::uint32_t register_count = 8U;
  constexpr std::uint32_t coeff_count = 4U;
  constexpr std::uint32_t rns_count = 1U;
  constexpr std::uint32_t handle_count = 1U;
  constexpr std::uint32_t handle_capacity = 4U;
  constexpr std::uint32_t token_count = 1U;
  constexpr std::uint32_t output_token_count = 1U;
  constexpr std::uint32_t control_count =
      9U + (rns_count * 2U) + register_count + (handle_capacity * 2U) +
      (token_count * 2U) + (output_token_count * 2U);
  constexpr std::uint32_t payload_count = handle_capacity * coeff_count;
  constexpr std::uint32_t output_count = kHeaderWords + register_count;

  std::uint64_t control_words[kAxiDepth] = {};
  control_words[0] = kPayloadMagic;
  control_words[1] = kPayloadVersion;
  control_words[2] = register_count;
  control_words[3] = coeff_count;
  control_words[4] = rns_count;
  control_words[5] = handle_count;
  control_words[6] = handle_capacity;
  control_words[7] = token_count;
  control_words[8] = output_token_count;
  control_words[9] = 0U;
  control_words[10] = 97U;
  control_words[11 + register_count + 0] = 0U;   // handle 1 rns base
  control_words[11 + register_count + 1] = 1U;   // handle 1 flags
  control_words[27] = kInToken;
  control_words[28] = 1U;
  control_words[29] = kOutToken;
  control_words[30] = 0U;

  std::uint64_t payload_words[kAxiDepth] = {};
  payload_words[0] = 11U;
  payload_words[1] = 22U;
  payload_words[2] = 33U;
  payload_words[3] = 44U;

  std::uint64_t instructions[kAxiDepth] = {
      encode_word0(1U, 3U, kTokenOperandId, 0U, 0U, 0U), encode_word1(0, 0),
      kInToken, 0ULL,
      encode_word0(3U, 4U, 3U, 0U, 0U, 0U), encode_word1(0, 0), 0ULL, 0ULL,
      encode_word0(2U, 0U, 4U, kTokenOperandId, 0U, 0U), encode_word1(0, 0),
      kOutToken, 0ULL,
  };
  std::uint64_t outputs[kAxiDepth] = {};

  cinnamon_memory(instructions, control_words, payload_words, outputs, 12U,
                  control_count, payload_count, output_count, 0U);

  if (outputs[0] != 0ULL) {
    return 1;
  }
  if (outputs[kHeaderWords + 3] != 1ULL ||
      outputs[kHeaderWords + 4] != 1ULL) {
    return 2;
  }
  if (control_words[30] != 1ULL) {
    return 3;
  }
  return 0;
}

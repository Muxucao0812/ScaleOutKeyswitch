#include <cstdint>
#include <iostream>

extern "C" void cinnamon_automorphism(const std::uint64_t *instructions,
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
constexpr std::uint32_t kPayloadFlagIsNtt = 1U;
constexpr std::uint32_t kOutputHeaderWords = 6U;
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
  constexpr std::uint32_t register_count = 1U;
  constexpr std::uint32_t coeff_count = 8U;
  constexpr std::uint32_t rns_count = 1U;
  constexpr std::uint32_t handle_count = 1U;
  constexpr std::uint32_t handle_capacity = 4U;
  constexpr std::uint32_t control_count =
      9U + (rns_count * 2U) + register_count + (handle_capacity * 2U);
  constexpr std::uint32_t payload_count = handle_capacity * coeff_count;
  constexpr std::uint32_t output_count = kOutputHeaderWords + register_count;
  constexpr std::uint64_t expected[coeff_count] = {3ULL, 2ULL, 0ULL, 1ULL,
                                                   6ULL, 7ULL, 5ULL, 4ULL};

  std::uint64_t control_words[kAxiDepth] = {};
  control_words[0] = kPayloadMagic;
  control_words[1] = kPayloadVersion;
  control_words[2] = register_count;
  control_words[3] = coeff_count;
  control_words[4] = rns_count;
  control_words[5] = handle_count;
  control_words[6] = handle_capacity;
  control_words[7] = 0U;
  control_words[8] = 0U;
  control_words[9] = 0U;
  control_words[10] = mod;
  control_words[11] = 1U;
  control_words[12] = 0U;
  control_words[13] = kPayloadFlagIsNtt;

  std::uint64_t payload_words[kAxiDepth] = {};
  for (std::uint32_t i = 0; i < coeff_count; ++i) {
    payload_words[i] = i;
  }

  std::uint64_t instructions[kAxiDepth] = {
      encode_word0(14U, 0U, 0U, 0U, 0U, 0U), encode_word1(3, 0), 0ULL, 0ULL};
  std::uint64_t outputs[kAxiDepth] = {};

  cinnamon_automorphism(instructions, control_words, payload_words, outputs, 4U,
                        control_count, payload_count, output_count, 0U);

  if (outputs[0] != 0ULL) {
    std::cerr << "automorphism status mismatch: outputs[0]=" << outputs[0] << '\n';
    return 1;
  }
  if (outputs[1] != 1ULL || outputs[kOutputHeaderWords] != 2ULL) {
    std::cerr << "automorphism handle mismatch: executed=" << outputs[1]
              << " reg0=" << outputs[kOutputHeaderWords] << '\n';
    return 2;
  }
  if (control_words[5] != 2ULL || control_words[14] != 0ULL ||
      control_words[15] != kPayloadFlagIsNtt) {
    std::cerr << "automorphism metadata mismatch: handle_count=" << control_words[5]
              << " handle2_meta={" << control_words[14] << ", "
              << control_words[15] << "}\n";
    return 3;
  }
  for (std::uint32_t i = 0; i < coeff_count; ++i) {
    if (payload_words[coeff_count + i] != expected[i]) {
      std::cerr << "automorphism mismatch at index " << i << ": expected "
                << expected[i] << " got " << payload_words[coeff_count + i]
                << '\n';
      return 4;
    }
  }
  return 0;
}

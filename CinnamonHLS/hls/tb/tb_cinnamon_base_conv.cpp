#include <cstdint>
#include <iostream>

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
constexpr std::uint64_t kPayloadMagic = 0x43494E4E5041594CULL;
constexpr std::uint64_t kPayloadVersion = 1ULL;
constexpr std::uint64_t kPayloadExtraMagic = 0x43494E4E58425243ULL;
constexpr std::uint64_t kPayloadExtraVersion = 1ULL;
constexpr std::uint32_t kOutputHeaderWords = 6U;
constexpr std::uint32_t kBcuOperand0 = 0xA00U;
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
  constexpr std::uint64_t q = 264634369ULL;
  constexpr std::uint32_t register_count = 1U;
  constexpr std::uint32_t coeff_count = 1U;
  constexpr std::uint32_t rns_count = 2U;
  constexpr std::uint32_t handle_count = 1U;
  constexpr std::uint32_t handle_capacity = 4U;
  constexpr std::uint32_t bcu_unit_count = 1U;
  constexpr std::uint32_t bcu_output_capacity = 1U;
  constexpr std::uint32_t bcu_active_count = 1U;
  constexpr std::uint32_t bci_entry_count = 1U;
  constexpr std::uint32_t control_count =
      9U + (rns_count * 2U) + register_count + (handle_capacity * 2U) +
      6U + bcu_active_count + (bcu_unit_count * bcu_output_capacity) + 7U;
  constexpr std::uint32_t payload_count = handle_capacity * coeff_count;
  constexpr std::uint32_t output_count = kOutputHeaderWords + register_count;

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
  control_words[9] = 2U;
  control_words[10] = q;
  control_words[11] = 12U;
  control_words[12] = q;
  control_words[13] = 1U;
  control_words[14] = 2U;
  control_words[15] = 0U;

  control_words[22] = kPayloadExtraMagic;
  control_words[23] = kPayloadExtraVersion;
  control_words[24] = bcu_unit_count;
  control_words[25] = bcu_output_capacity;
  control_words[26] = bcu_active_count;
  control_words[27] = bci_entry_count;
  control_words[28] = 7U;
  control_words[29] = 0U;
  control_words[30] = 7U;
  control_words[31] = 0U;
  control_words[32] = 1U;
  control_words[33] = 1U;
  control_words[34] = 2U;
  control_words[35] = 12U;
  control_words[36] = 1U;

  std::uint64_t payload_words[kAxiDepth] = {};
  payload_words[0] = 73U;

  std::uint64_t instructions[kAxiDepth] = {
      encode_word0(32U, kBcuOperand0, 0U, 0U, 2U, 0U), encode_word1(0, 0), 0ULL, 0ULL,
  };
  std::uint64_t outputs[kAxiDepth] = {};

  cinnamon_base_conv(instructions, control_words, payload_words, outputs, 4U,
                     control_count, payload_count, output_count, 0U);

  if (outputs[0] != 0ULL) {
    std::cerr << "base_conv status mismatch: outputs[0]=" << outputs[0] << '\n';
    return 1;
  }
  if (outputs[1] != 1ULL || outputs[kOutputHeaderWords] != 1ULL) {
    std::cerr << "base_conv output mismatch: executed=" << outputs[1]
              << " reg0=" << outputs[kOutputHeaderWords] << '\n';
    return 2;
  }
  if (control_words[5] != 2ULL || control_words[29] != 2ULL) {
    std::cerr << "base_conv handle/table mismatch: handle_count=" << control_words[5]
              << " table0=" << control_words[29] << '\n';
    return 3;
  }
  if (control_words[16] != 12ULL || control_words[17] != 0ULL) {
    std::cerr << "base_conv handle2 meta mismatch: {" << control_words[16] << ", "
              << control_words[17] << "}\n";
    return 4;
  }
  if (payload_words[1] != 73ULL) {
    std::cerr << "base_conv payload mismatch: expected 73 got " << payload_words[1]
              << '\n';
    return 5;
  }
  return 0;
}

#include <cstdint>
#include <iostream>

extern "C" void cinnamon_ntt(const std::uint64_t *instructions,
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
  {
    constexpr std::uint64_t mod = 1179649ULL;
    constexpr std::uint32_t register_count = 1U;
    constexpr std::uint32_t coeff_count = 4U;
    constexpr std::uint32_t rns_count = 1U;
    constexpr std::uint32_t handle_count = 1U;
    constexpr std::uint32_t handle_capacity = 4U;
    constexpr std::uint32_t control_count =
        9U + (rns_count * 2U) + register_count + (handle_capacity * 2U);
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
    control_words[9] = 1U;
    control_words[10] = mod;
    control_words[11] = 1U;
    control_words[12] = 1U;
    control_words[13] = 0U;

    std::uint64_t payload_words[kAxiDepth] = {};
    payload_words[0] = 0ULL;
    payload_words[1] = 8ULL;
    payload_words[2] = 4ULL;
    payload_words[3] = 12ULL;

    std::uint64_t instructions[kAxiDepth] = {
        encode_word0(12U, 0U, 0U, 0U, 1U, 0U), encode_word1(4, 0), 0ULL, 0ULL};
    std::uint64_t outputs[kAxiDepth] = {};

    cinnamon_ntt(instructions, control_words, payload_words, outputs, 4U,
                 control_count, payload_count, output_count, 0U);

    if (outputs[0] != 0ULL) {
      std::cerr << "ntt span4 status mismatch: outputs[0]=" << outputs[0] << '\n';
      return 1;
    }
    if (outputs[1] != 1ULL || outputs[kOutputHeaderWords] != 2ULL) {
      std::cerr << "ntt span4 handle mismatch: executed=" << outputs[1]
                << " reg0=" << outputs[kOutputHeaderWords] << '\n';
      return 2;
    }
    if (control_words[5] != 2ULL || control_words[14] != kPayloadFlagIsNtt) {
      std::cerr << "ntt span4 metadata mismatch: handle_count=" << control_words[5]
                << " flags(handle2)=" << control_words[14] << '\n';
      return 3;
    }
    if (payload_words[4] != 1146641ULL || payload_words[5] != 419093ULL ||
        payload_words[6] != 2288ULL || payload_words[7] != 791276ULL) {
      std::cerr << "ntt span4 mismatch: got={" << payload_words[4] << ", "
                << payload_words[5] << ", " << payload_words[6] << ", "
                << payload_words[7]
                << "} expected={1146641, 419093, 2288, 791276}\n";
      return 4;
    }
  }

  {
    constexpr std::uint64_t mod = 786433ULL;
    constexpr std::uint32_t register_count = 1U;
    constexpr std::uint32_t coeff_count = 8U;
    constexpr std::uint32_t rns_count = 1U;
    constexpr std::uint32_t handle_count = 1U;
    constexpr std::uint32_t handle_capacity = 4U;
    constexpr std::uint32_t control_count =
        9U + (rns_count * 2U) + register_count + (handle_capacity * 2U);
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
    control_words[9] = 4U;
    control_words[10] = mod;
    control_words[11] = 1U;
    control_words[12] = 4U;
    control_words[13] = 0U;

    std::uint64_t payload_words[kAxiDepth] = {};
    for (std::uint32_t i = 0; i < coeff_count; ++i) {
      payload_words[i] = i;
    }

    std::uint64_t instructions[kAxiDepth] = {
        encode_word0(12U, 0U, 0U, 0U, 4U, 0U), encode_word1(8, 0), 0ULL, 0ULL,
        encode_word0(13U, 0U, 0U, 0U, 4U, 0U), encode_word1(8, 0), 0ULL, 0ULL};
    std::uint64_t outputs[kAxiDepth] = {};

    cinnamon_ntt(instructions, control_words, payload_words, outputs, 8U,
                 control_count, payload_count, output_count, 0U);

    if (outputs[0] != 0ULL) {
      std::cerr << "ntt roundtrip status mismatch: outputs[0]=" << outputs[0] << '\n';
      return 5;
    }
    if (outputs[1] != 2ULL || outputs[kOutputHeaderWords] != 3ULL) {
      std::cerr << "ntt roundtrip handle mismatch: executed=" << outputs[1]
                << " reg0=" << outputs[kOutputHeaderWords] << '\n';
      return 6;
    }
    if (control_words[5] != 3ULL || control_words[16] != 4ULL ||
        control_words[17] != 0ULL) {
      std::cerr << "ntt roundtrip metadata mismatch: handle_count=" << control_words[5]
                << " handle3_meta={" << control_words[16] << ", "
                << control_words[17] << "}\n";
      return 7;
    }
    for (std::uint32_t i = 0; i < coeff_count; ++i) {
      if (payload_words[16U + i] != i) {
        std::cerr << "ntt roundtrip mismatch at index " << i
                  << ": expected " << i << " got " << payload_words[16U + i]
                  << '\n';
        return 8;
      }
    }
  }

  return 0;
}

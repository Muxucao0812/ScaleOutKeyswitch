#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cinnamon_hls/montgomery.hpp"

extern "C" {
void cinnamon_memory(const std::uint64_t *instructions, const std::uint64_t *inputs,
                     std::uint64_t *outputs, std::uint32_t instruction_count,
                     std::uint32_t input_count, std::uint32_t output_count,
                     std::uint32_t partition_id);
void cinnamon_arithmetic(const std::uint64_t *instructions,
                         const std::uint64_t *inputs, std::uint64_t *outputs,
                         std::uint32_t instruction_count,
                         std::uint32_t input_count,
                         std::uint32_t output_count,
                         std::uint32_t partition_id);
void cinnamon_montgomery(const std::uint64_t *instructions,
                         const std::uint64_t *inputs, std::uint64_t *outputs,
                         std::uint32_t instruction_count,
                         std::uint32_t input_count,
                         std::uint32_t output_count,
                         std::uint32_t partition_id);
void cinnamon_ntt(const std::uint64_t *instructions, const std::uint64_t *inputs,
                  std::uint64_t *outputs, std::uint32_t instruction_count,
                  std::uint32_t input_count, std::uint32_t output_count,
                  std::uint32_t partition_id);
void cinnamon_base_conv(const std::uint64_t *instructions,
                        const std::uint64_t *inputs, std::uint64_t *outputs,
                        std::uint32_t instruction_count,
                        std::uint32_t input_count,
                        std::uint32_t output_count,
                        std::uint32_t partition_id);
void cinnamon_automorphism(const std::uint64_t *instructions,
                           const std::uint64_t *inputs, std::uint64_t *outputs,
                           std::uint32_t instruction_count,
                           std::uint32_t input_count,
                           std::uint32_t output_count,
                           std::uint32_t partition_id);
void cinnamon_transpose(const std::uint64_t *instructions,
                        const std::uint64_t *inputs, std::uint64_t *outputs,
                        std::uint32_t instruction_count,
                        std::uint32_t input_count,
                        std::uint32_t output_count,
                        std::uint32_t partition_id);
}

namespace {

constexpr std::uint64_t kInputMagic = 0x43494E4E414D4F4EULL;
constexpr std::uint32_t kHeaderWords = 6U;
constexpr std::uint32_t kImmOperandId = 0xFFFU;
constexpr std::uint32_t kTokenOperandId = 0xFFEU;
constexpr const char *kAutomorphismReplayMagic = "CINNAMON_AUTOMORPHISM_REPLAY_V1";
constexpr const char *kAutomorphismReplayEnv = "CINNAMON_AUTOMORPHISM_REPLAY_FILE";

std::vector<std::uint64_t> run_kernel(
    void (*kernel)(const std::uint64_t *, const std::uint64_t *, std::uint64_t *,
                   std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t),
    const std::vector<std::uint64_t> &instructions,
    const std::vector<std::uint64_t> &inputs, std::uint32_t register_count,
    std::uint32_t partition_id);

struct AutomorphismReplayFixture {
  std::uint32_t partition_id = 0U;
  std::int64_t mismatch_word_index = -1;
  std::vector<std::uint64_t> instruction_words;
  std::vector<std::uint64_t> input_words;
  std::vector<std::uint64_t> expected_output_words;
  std::vector<std::uint64_t> actual_output_words;
};

std::string trim_copy(const std::string &value) {
  std::size_t begin = 0U;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

bool parse_key_value(const std::string &line, std::string &key, std::string &value) {
  std::istringstream iss(line);
  if (!(iss >> key)) {
    return false;
  }
  std::getline(iss, value);
  value = trim_copy(value);
  return true;
}

bool parse_u64(const std::string &text, std::uint64_t &out) {
  try {
    std::size_t consumed = 0U;
    const unsigned long long value = std::stoull(text, &consumed, 10);
    if (consumed != text.size()) {
      return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_i64(const std::string &text, std::int64_t &out) {
  try {
    std::size_t consumed = 0U;
    const long long value = std::stoll(text, &consumed, 10);
    if (consumed != text.size()) {
      return false;
    }
    out = static_cast<std::int64_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool load_automorphism_replay_fixture(const std::string &path,
                                      AutomorphismReplayFixture &fixture) {
  std::ifstream ifs(path);
  if (!ifs) {
    return false;
  }

  enum class Section {
    kMeta,
    kInstructions,
    kInputs,
    kExpectedOutput,
    kActualOutput,
  };
  Section section = Section::kMeta;
  bool saw_magic = false;

  std::string raw_line;
  while (std::getline(ifs, raw_line)) {
    const std::string line = trim_copy(raw_line);
    if (line.empty()) {
      continue;
    }
    if (!saw_magic) {
      if (line != kAutomorphismReplayMagic) {
        return false;
      }
      saw_magic = true;
      continue;
    }

    if (line == "--instructions--") {
      section = Section::kInstructions;
      continue;
    }
    if (line == "--inputs--") {
      section = Section::kInputs;
      continue;
    }
    if (line == "--expected_output--") {
      section = Section::kExpectedOutput;
      continue;
    }
    if (line == "--actual_output--") {
      section = Section::kActualOutput;
      continue;
    }
    if (line == "--end--") {
      break;
    }

    if (section == Section::kMeta) {
      std::string key;
      std::string value;
      if (!parse_key_value(line, key, value)) {
        return false;
      }
      if (key == "partition_id") {
        std::uint64_t parsed = 0U;
        if (!parse_u64(value, parsed)) {
          return false;
        }
        fixture.partition_id = static_cast<std::uint32_t>(parsed);
      } else if (key == "mismatch_word_index") {
        std::int64_t parsed = -1;
        if (!parse_i64(value, parsed)) {
          return false;
        }
        fixture.mismatch_word_index = parsed;
      }
      continue;
    }

    std::uint64_t word = 0U;
    if (!parse_u64(line, word)) {
      return false;
    }
    if (section == Section::kInstructions) {
      fixture.instruction_words.push_back(word);
    } else if (section == Section::kInputs) {
      fixture.input_words.push_back(word);
    } else if (section == Section::kExpectedOutput) {
      fixture.expected_output_words.push_back(word);
    } else if (section == Section::kActualOutput) {
      fixture.actual_output_words.push_back(word);
    }
  }

  if (!saw_magic) {
    return false;
  }
  if (fixture.instruction_words.empty() || fixture.input_words.empty() ||
      fixture.expected_output_words.empty()) {
    return false;
  }
  return true;
}

void run_automorphism_replay_from_fixture(const AutomorphismReplayFixture &fixture) {
  assert(fixture.input_words.size() >= 3U);
  assert(fixture.input_words[0] == kInputMagic);
  const std::uint32_t register_count = static_cast<std::uint32_t>(fixture.input_words[1]);
  assert(register_count > 0U);

  const auto out = run_kernel(cinnamon_automorphism, fixture.instruction_words,
                              fixture.input_words, register_count, fixture.partition_id);
  assert(out.size() == fixture.expected_output_words.size());
  assert(out[0] == fixture.expected_output_words[0]);  // status
  assert(out[1] == fixture.expected_output_words[1]);  // executed
  assert(out[2] == fixture.expected_output_words[2]);  // register_count
  assert(out[3] == fixture.expected_output_words[3]);  // module_id
  assert(out[4] == fixture.expected_output_words[4]);  // partition_id
  assert(out[5] == fixture.expected_output_words[5]);  // trace_acc

  for (std::size_t i = 0U; i < out.size(); ++i) {
    assert(out[i] == fixture.expected_output_words[i]);
  }

  if (fixture.mismatch_word_index >= 0 &&
      static_cast<std::size_t>(fixture.mismatch_word_index) < out.size()) {
    const std::size_t idx = static_cast<std::size_t>(fixture.mismatch_word_index);
    assert(out[idx] == fixture.expected_output_words[idx]);
  }
}

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
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm0)) & 0xFFFFFFFFULL) |
         ((static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm1)) & 0xFFFFFFFFULL)
          << 32U);
}

std::vector<std::uint64_t> make_layout_input(std::uint64_t mod,
                                             const std::vector<std::uint64_t> &state,
                                             const std::vector<std::uint64_t> &tail = {}) {
  std::vector<std::uint64_t> words;
  words.reserve(3 + state.size() + tail.size());
  words.push_back(kInputMagic);
  words.push_back(static_cast<std::uint64_t>(state.size()));
  words.push_back(mod);
  words.insert(words.end(), state.begin(), state.end());
  words.insert(words.end(), tail.begin(), tail.end());
  return words;
}

std::vector<std::uint64_t> run_kernel(
    void (*kernel)(const std::uint64_t *, const std::uint64_t *, std::uint64_t *,
                   std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t),
    const std::vector<std::uint64_t> &instructions,
    const std::vector<std::uint64_t> &inputs, std::uint32_t register_count,
    std::uint32_t partition_id = 0) {
  std::vector<std::uint64_t> outputs(kHeaderWords + register_count, 0);
  kernel(instructions.data(), inputs.data(), outputs.data(),
         static_cast<std::uint32_t>(instructions.size()),
         static_cast<std::uint32_t>(inputs.size()),
         static_cast<std::uint32_t>(outputs.size()), partition_id);
  return outputs;
}

}  // namespace

int main() {
  // Memory: load(token) -> mov -> store, token value comes from stream table.
  {
    constexpr std::uint64_t mod = 97;
    const std::uint64_t token_key = 0x12345678ULL;
    const std::vector<std::uint64_t> input = make_layout_input(
        mod, std::vector<std::uint64_t>(8, 0),
        {
            1,        // stream table entries
            token_key,
            55,
        });

    const std::vector<std::uint64_t> instructions = {
        encode_word0(1, 3, kTokenOperandId, 0, 0, 0), encode_word1(0, 0), token_key, 0,
        encode_word0(3, 4, 3, 0, 0, 0),              encode_word1(0, 0), 0,         0,
        encode_word0(2, 0, 4, kTokenOperandId, 0, 0), encode_word1(0, 0), token_key, 0,
    };

    const auto out = run_kernel(cinnamon_memory, instructions, input, 8);
    assert(out[0] == 0);
    assert(out[1] == 3);
    assert(out[kHeaderWords + 3] == 55);
    assert(out[kHeaderWords + 4] == 55);
  }

  // Memory batch + edge: loas + spill + rec should keep state semantics stable.
  {
    constexpr std::uint64_t mod = 97;
    const std::uint64_t token_key = 0xABCDEFULL;
    const std::vector<std::uint64_t> input = make_layout_input(
        mod, {1, 2, 3, 4, 5},
        {
            1,  // stream table entries
            token_key,
            88,
        });

    const std::vector<std::uint64_t> instructions = {
        // loas r0 <- token
        encode_word0(30, 0, kTokenOperandId, 0, 0, 0), encode_word1(0, 0), token_key, 0,
        // spill r0 (store-like side effect only)
        encode_word0(31, 0, 0, 0, 0, 0),              encode_word1(0, 0), 0,         0,
        // rec r1 <- r0
        encode_word0(27, 1, 0, 0, 0, 0),              encode_word1(0, 0), 0,         0,
    };

    const auto out = run_kernel(cinnamon_memory, instructions, input, 5);
    assert(out[0] == 0);
    assert(out[1] == 3);
    assert(out[kHeaderWords + 0] == 88);
    assert(out[kHeaderWords + 1] == 88);
  }

  // Arithmetic: add/sub/neg using RTL scalar vectors.
  {
    constexpr std::uint64_t mod = 97;
    const std::vector<std::uint64_t> input = make_layout_input(mod, {11, 0, 0, 0});
    const std::vector<std::uint64_t> instructions = {
        encode_word0(4, 1, 0, kImmOperandId, 0, (1U << 1U)), encode_word1(0, 7), 0, 0,
        encode_word0(5, 2, 1, 0, 0, 0),                      encode_word1(0, 0), 0, 0,
        encode_word0(16, 3, 2, 0, 0, 0),                     encode_word1(0, 0), 0, 0,
    };

    const auto out = run_kernel(cinnamon_arithmetic, instructions, input, 4);
    assert(out[kHeaderWords + 1] == 18);
    assert(out[kHeaderWords + 2] == 7);
    assert(out[kHeaderWords + 3] == 90);
  }

  // Arithmetic edge + batch: ads/sus/sud/con should all be covered.
  {
    constexpr std::uint64_t mod = 97;
    const std::vector<std::uint64_t> input = make_layout_input(mod, {96, 1, 0, 0, 0, 0});
    const std::vector<std::uint64_t> instructions = {
        // ads r2 = r0 + r1 = 0
        encode_word0(9, 2, 0, 1, 0, 0),   encode_word1(0, 0), 0, 0,
        // sus r3 = r2 - r1 = 96
        encode_word0(10, 3, 2, 1, 0, 0),  encode_word1(0, 0), 0, 0,
        // sud r4 = (r3-r1)/2 mod 97 = (95+97)/2 = 96
        encode_word0(11, 4, 3, 1, 0, 0),  encode_word1(0, 0), 0, 0,
        // con r5 = r4 (scalar model no-op)
        encode_word0(15, 5, 4, 0, 0, 0),  encode_word1(0, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_arithmetic, instructions, input, 6);
    assert(out[kHeaderWords + 2] == 0);
    assert(out[kHeaderWords + 3] == 96);
    assert(out[kHeaderWords + 4] == 96);
    assert(out[kHeaderWords + 5] == 96);
  }

  // Montgomery: vector from CinnamonRTL/montgomery_multiplier_test.sv
  {
    constexpr std::uint64_t q = 268042241;
    const std::vector<std::uint64_t> input = make_layout_input(q, {34234, 7652, 0});
    const std::vector<std::uint64_t> instructions = {
        encode_word0(6, 2, 0, 1, 0, 0), encode_word1(0, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_montgomery, instructions, input, 3);
    assert(out[kHeaderWords + 2] == 228895654);
  }

  // Montgomery batch: mul + mup + mus all share the same core math path.
  {
    constexpr std::uint64_t q = 268042241;
    const std::vector<std::uint64_t> input = make_layout_input(q, {34234, 7652, 4, 8986652, 0, 0});
    const std::vector<std::uint64_t> instructions = {
        encode_word0(6, 4, 0, 1, 0, 0), encode_word1(0, 0), 0, 0,   // mul
        encode_word0(7, 5, 0, 1, 0, 0), encode_word1(0, 0), 0, 0,   // mup
        encode_word0(8, 4, 2, 3, 0, 0), encode_word1(0, 0), 0, 0,   // mus overwrite r4
    };
    const auto out = run_kernel(cinnamon_montgomery, instructions, input, 6);
    assert(out[kHeaderWords + 4] == 266794278);
    assert(out[kHeaderWords + 5] == 228895654);
  }

  // Negacyclic NTT forward span-4 sanity vector.
  {
    constexpr std::uint64_t mod = 1179649;
    const std::vector<std::uint64_t> input = make_layout_input(
        mod, {0, 8, 4, 12, 0, 0, 0, 0});
    const std::vector<std::uint64_t> instructions = {
        // imm0=4 => span=4, prime id=1.
        encode_word0(12, 0, 0, 0, 1, 0), encode_word1(4, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_ntt, instructions, input, 8);
    assert(out[kHeaderWords + 0] == 1146641);
    assert(out[kHeaderWords + 1] == 2288);
    assert(out[kHeaderWords + 2] == 419093);
    assert(out[kHeaderWords + 3] == 791276);
  }

  // Negacyclic NTT inverse span-4 sanity vector.
  {
    constexpr std::uint64_t mod = 2752513;
    const std::vector<std::uint64_t> input = make_layout_input(
        mod, {400282, 1662337, 19381, 670193, 0, 0, 0, 0});
    const std::vector<std::uint64_t> instructions = {
        // imm0=4 => span=4, prime id=9, inverse path.
        encode_word0(13, 0, 0, 0, 9, 0), encode_word1(4, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_ntt, instructions, input, 8);
    assert(out[kHeaderWords + 0] == 2752433);
    assert(out[kHeaderWords + 1] == 2752237);
    assert(out[kHeaderWords + 2] == 2752233);
    assert(out[kHeaderWords + 3] == 2752373);
  }

  // NTT span-8 roundtrip: forward then inverse restores state.
  {
    constexpr std::uint64_t mod = 786433;
    const std::vector<std::uint64_t> input = make_layout_input(
        mod, {0, 1, 2, 3, 4, 5, 6, 7});
    const std::vector<std::uint64_t> instructions = {
        encode_word0(12, 0, 0, 0, 4, 0), encode_word1(8, 0), 0, 0,
        encode_word0(13, 0, 0, 0, 4, 0), encode_word1(8, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_ntt, instructions, input, 8);
    for (std::uint32_t i = 0; i < 8; ++i) {
      assert(out[kHeaderWords + i] == i);
    }
  }

  // Base conversion path: bci + pl1 + mod
  {
    constexpr std::uint64_t q = 264634369;
    const std::vector<std::uint64_t> input = make_layout_input(q, {73, 0, 0, 0, 0});
    const std::vector<std::uint64_t> instructions = {
        encode_word0(17, 1, 0, 0, 2, 0),  encode_word1(13, 5), 7, 0,
        encode_word0(18, 2, 0, 0, 12, 0), encode_word1(0, 0),  0, 0,
        encode_word0(26, 3, 2, 1, 0, 0),  encode_word1(0, 0),  0, 0,
    };
    const auto out = run_kernel(cinnamon_base_conv, instructions, input, 5);
    assert(out[kHeaderWords + 2] != 0);
    assert(out[kHeaderWords + 3] ==
           ((out[kHeaderWords + 2] + q - out[kHeaderWords + 1]) % q));
  }

  // Base conversion batch: bcw + rsi + rsv paths.
  {
    constexpr std::uint64_t q = 264634369;
    const std::vector<std::uint64_t> input = make_layout_input(q, {100, 11, 0, 0, 0, 0});
    const std::vector<std::uint64_t> instructions = {
        // bcw r2 <- f(r0, rns=4, imm0=7)
        encode_word0(32, 2, 0, 0, 4, 0), encode_word1(7, 0), 0, 0,
        // rsi r3 <- r2 + imm0 + imm1
        encode_word0(33, 3, 2, 0, 0, 0), encode_word1(5, 6), 0, 0,
        // rsv r4 <- r3 + r1 + imm0 + imm1
        encode_word0(25, 4, 3, 1, 0, 0), encode_word1(1, 2), 0, 0,
    };
    const auto out = run_kernel(cinnamon_base_conv, instructions, input, 6);
    assert(out[kHeaderWords + 3] == 0);
    assert(out[kHeaderWords + 4] == 11);
  }

  // Automorphism: rotate source index by +2.
  {
    constexpr std::uint64_t mod = 97;
    const std::vector<std::uint64_t> input = make_layout_input(mod, {10, 20, 30, 40});
    const std::vector<std::uint64_t> instructions = {
        encode_word0(14, 1, 0, 0, 0, 0), encode_word1(2, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_automorphism, instructions, input, 4);
    assert(out[kHeaderWords + 1] == 30);
  }

  // Automorphism edge: imm0=0 uses rns as rotation amount.
  {
    constexpr std::uint64_t mod = 97;
    const std::vector<std::uint64_t> input = make_layout_input(mod, {10, 20, 30, 40});
    const std::vector<std::uint64_t> instructions = {
        encode_word0(14, 2, 1, 0, 3, 0), encode_word1(0, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_automorphism, instructions, input, 4);
    // src index=1, rot=3 => mapped index=0.
    assert(out[kHeaderWords + 2] == 10);
  }

  // Automorphism permutation vector from CinnamonRTL/automorphism/test/automorphism_test.sv
  // Permutation: 0 3 6 1 4 7 2 5 => output block {5,2,7,4,1,6,3,0}.
  {
    constexpr std::uint64_t mod = 997;
    const std::vector<std::uint64_t> input = make_layout_input(mod, {7, 6, 5, 4, 3, 2, 1, 0});
    const std::uint64_t perm_info = (static_cast<std::uint64_t>(2911) << 12U) | 4095ULL;
    const std::vector<std::uint64_t> instructions = {
        encode_word0(14, 0, 0, 0, 0, 0), encode_word1(0, 0), perm_info, 0,
    };
    const auto out = run_kernel(cinnamon_automorphism, instructions, input, 8);
    assert(out[kHeaderWords + 0] == 5);
    assert(out[kHeaderWords + 1] == 2);
    assert(out[kHeaderWords + 2] == 7);
    assert(out[kHeaderWords + 3] == 4);
    assert(out[kHeaderWords + 4] == 1);
    assert(out[kHeaderWords + 5] == 6);
    assert(out[kHeaderWords + 6] == 3);
    assert(out[kHeaderWords + 7] == 0);
  }

  // Second automorphism permutation vector from the same RTL testbench.
  {
    constexpr std::uint64_t mod = 997;
    const std::vector<std::uint64_t> input = make_layout_input(mod, {7, 6, 5, 4, 3, 2, 1, 0});
    const std::uint64_t perm_info = (static_cast<std::uint64_t>(22) << 12U) | 4089ULL;
    const std::vector<std::uint64_t> instructions = {
        encode_word0(14, 0, 0, 0, 0, 0), encode_word1(0, 0), perm_info, 0,
    };
    const auto out = run_kernel(cinnamon_automorphism, instructions, input, 8);
    assert(out[kHeaderWords + 0] == 0);
    assert(out[kHeaderWords + 1] == 5);
    assert(out[kHeaderWords + 2] == 3);
    assert(out[kHeaderWords + 3] == 6);
    assert(out[kHeaderWords + 4] == 1);
    assert(out[kHeaderWords + 5] == 7);
    assert(out[kHeaderWords + 6] == 2);
    assert(out[kHeaderWords + 7] == 4);
  }

  // Transpose: 4x4 full transpose (column-stream layout), aligned with
  // CinnamonRTL/transpose/transpose_full.sv semantics.
  {
    constexpr std::uint64_t mod = 257;
    const std::vector<std::uint64_t> input = make_layout_input(
        mod, {
                 0, 4, 8, 12,
                 1, 5, 9, 13,
                 2, 6, 10, 14,
                 3, 7, 11, 15,
             });
    const std::vector<std::uint64_t> instructions = {
        encode_word0(27, 0, 0, 0, 4, 0), encode_word1(0, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_transpose, instructions, input, 16);
    for (std::uint32_t i = 0; i < 16; ++i) {
      assert(out[kHeaderWords + i] == i);
    }
  }

  // Transpose batch + alternate opcode path (rec + rsv).
  {
    constexpr std::uint64_t mod = 257;
    const std::vector<std::uint64_t> input =
        make_layout_input(mod, {7, 1, 8, 6, 0, 0, 0, 0});
    const std::vector<std::uint64_t> instructions = {
        // First transpose to r0..r3 => {7,8,1,6}
        encode_word0(27, 0, 0, 0, 2, 0), encode_word1(0, 0), 0, 0,
        // Transpose again from r0..r3 to r4..r7 => {7,1,8,6}
        encode_word0(25, 4, 0, 0, 2, 0), encode_word1(0, 0), 0, 0,
    };
    const auto out = run_kernel(cinnamon_transpose, instructions, input, 8);
    assert(out[kHeaderWords + 0] == 7);
    assert(out[kHeaderWords + 1] == 8);
    assert(out[kHeaderWords + 2] == 1);
    assert(out[kHeaderWords + 3] == 6);
    assert(out[kHeaderWords + 4] == 7);
    assert(out[kHeaderWords + 5] == 1);
    assert(out[kHeaderWords + 6] == 8);
    assert(out[kHeaderWords + 7] == 6);
  }

  const char *replay_path = std::getenv(kAutomorphismReplayEnv);
  if (replay_path != nullptr && replay_path[0] != '\0') {
    AutomorphismReplayFixture fixture;
    const bool loaded = load_automorphism_replay_fixture(replay_path, fixture);
    assert(loaded);
    run_automorphism_replay_from_fixture(fixture);
  }

  return 0;
}

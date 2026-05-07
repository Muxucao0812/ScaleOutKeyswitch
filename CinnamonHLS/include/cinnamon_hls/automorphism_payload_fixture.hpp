#ifndef CINNAMON_HLS_AUTOMORPHISM_PAYLOAD_FIXTURE_HPP_
#define CINNAMON_HLS_AUTOMORPHISM_PAYLOAD_FIXTURE_HPP_

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "kernel_common.hpp"

namespace cinnamon_hls_test {

constexpr std::uint64_t kPayloadMagic = 0x43494E4E5041594CULL;
constexpr std::uint64_t kPayloadVersion = 1ULL;
constexpr std::uint32_t kPayloadFlagIsNtt = 1U;
constexpr std::uint32_t kOutputHeaderWords = 6U;
constexpr std::uint64_t kModulus = 997ULL;
constexpr std::uint32_t kRegisterCount = 1U;
constexpr std::uint32_t kRnsCount = 1U;
constexpr std::uint32_t kHandleCapacity = 4U;
constexpr std::uint32_t kInitialHandleCount = 1U;
constexpr const char *kAutomorphismReplayEnv = "CINNAMON_AUTOMORPHISM_REPLAY_FILE";
constexpr const char *kAutomorphismReplayMagic = "CINNAMON_AUTOMORPHISM_PAYLOAD_REPLAY_V1";

using AutomorphismKernelFn = void (*)(const std::uint64_t *instructions,
                                      std::uint64_t *control_words,
                                      std::uint64_t *payload_words,
                                      std::uint64_t *outputs,
                                      std::uint32_t instruction_count,
                                      std::uint32_t control_count,
                                      std::uint32_t payload_count,
                                      std::uint32_t output_count,
                                      std::uint32_t partition_id);

struct AutomorphismSyntheticCase {
  std::string name;
  std::vector<std::uint64_t> values;
};

struct AutomorphismPayloadBuffers {
  std::vector<std::uint64_t> control_words;
  std::vector<std::uint64_t> payload_words;
};

struct AutomorphismReplayFixture {
  std::uint32_t partition_id = 0U;
  std::vector<std::uint64_t> instruction_words;
  std::vector<std::uint64_t> control_words;
  std::vector<std::uint64_t> payload_words;
  std::vector<std::uint64_t> expected_control_words;
  std::vector<std::uint64_t> expected_payload_words;
  std::vector<std::uint64_t> expected_output_words;
};

inline std::uint64_t encode_word0(std::uint32_t opcode, std::uint32_t dst,
                                  std::uint32_t src0, std::uint32_t src1,
                                  std::uint32_t rns, std::uint32_t flags) {
  return (static_cast<std::uint64_t>(opcode) & 0xFFULL) |
         ((static_cast<std::uint64_t>(dst) & 0xFFFULL) << 8U) |
         ((static_cast<std::uint64_t>(src0) & 0xFFFULL) << 20U) |
         ((static_cast<std::uint64_t>(src1) & 0xFFFULL) << 32U) |
         ((static_cast<std::uint64_t>(rns) & 0xFFFULL) << 44U) |
         ((static_cast<std::uint64_t>(flags) & 0xFFULL) << 56U);
}

inline std::uint64_t encode_word1(std::int32_t imm0, std::int32_t imm1) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm0)) &
          0xFFFFFFFFULL) |
         ((static_cast<std::uint64_t>(static_cast<std::uint32_t>(imm1)) &
           0xFFFFFFFFULL)
          << 32U);
}

inline std::vector<std::uint64_t> make_sequential_input(std::size_t n) {
  std::vector<std::uint64_t> values(n);
  for (std::size_t i = 0; i < n; ++i) {
    values[i] = static_cast<std::uint64_t>(i + 1U);
  }
  return values;
}

inline std::vector<std::uint64_t> make_sparse_input(std::size_t n) {
  std::vector<std::uint64_t> values(n, 0ULL);
  if (n > 0U) values[0] = 1ULL;
  if (n > 1U) values[1] = 7ULL;
  if (n > 4U) values[4] = 11ULL;
  if (n > 9U) values[9] = 23ULL;
  return values;
}

inline std::vector<std::uint64_t> make_pattern_input(std::size_t n) {
  std::vector<std::uint64_t> values(n);
  for (std::size_t i = 0; i < n; ++i) {
    values[i] = static_cast<std::uint64_t>((i * 17U + 3U) & 0xFFFFU);
  }
  return values;
}

inline std::vector<std::int32_t> default_automorphism_steps(std::size_t coeff_count) {
  return {
      1,
      2,
      3,
      -1,
      -2,
      -3,
      static_cast<std::int32_t>(coeff_count / 2U - 1U),
  };
}

inline std::vector<AutomorphismSyntheticCase> default_automorphism_cases(
    std::size_t coeff_count) {
  return {
      {"sequential", make_sequential_input(coeff_count)},
      {"sparse", make_sparse_input(coeff_count)},
      {"pattern", make_pattern_input(coeff_count)},
  };
}

inline std::vector<std::uint64_t> automorphism_expected_ntt(
    const std::vector<std::uint64_t> &values, std::int32_t step) {
  const std::uint32_t coeff_count = static_cast<std::uint32_t>(values.size());
  const std::uint32_t galois_elt =
      cinnamon_hls_kernel::galois_elt_from_step(coeff_count, step);
  std::vector<std::uint64_t> expected(coeff_count, 0ULL);
  if (galois_elt == 0U) {
    return expected;
  }
  cinnamon_hls_kernel::apply_galois_ntt_permutation(
      values.data(), expected.data(), coeff_count, galois_elt);
  return expected;
}

inline AutomorphismPayloadBuffers make_payload_buffers(
    const std::vector<std::uint64_t> &values) {
  const std::uint32_t coeff_count = static_cast<std::uint32_t>(values.size());
  const std::uint32_t control_count =
      9U + (kRnsCount * 2U) + kRegisterCount + (kHandleCapacity * 2U);
  const std::uint32_t register_handles_offset = 9U + (kRnsCount * 2U);
  const std::uint32_t handle_metadata_offset =
      register_handles_offset + kRegisterCount;

  AutomorphismPayloadBuffers buffers;
  buffers.control_words.assign(control_count, 0ULL);
  buffers.payload_words.assign(kHandleCapacity * coeff_count, 0ULL);

  buffers.control_words[0] = kPayloadMagic;
  buffers.control_words[1] = kPayloadVersion;
  buffers.control_words[2] = kRegisterCount;
  buffers.control_words[3] = coeff_count;
  buffers.control_words[4] = kRnsCount;
  buffers.control_words[5] = kInitialHandleCount;
  buffers.control_words[6] = kHandleCapacity;
  buffers.control_words[7] = 0U;
  buffers.control_words[8] = 0U;
  buffers.control_words[9] = 0U;
  buffers.control_words[10] = kModulus;
  buffers.control_words[register_handles_offset] = 1U;
  buffers.control_words[handle_metadata_offset] = 0U;
  buffers.control_words[handle_metadata_offset + 1U] = kPayloadFlagIsNtt;

  for (std::uint32_t i = 0U; i < coeff_count; ++i) {
    buffers.payload_words[i] = values[i];
  }

  return buffers;
}

inline std::string trim_copy(const std::string &value) {
  std::size_t begin = 0U;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

inline bool parse_key_value(const std::string &line, std::string &key,
                            std::string &value) {
  std::istringstream iss(line);
  if (!(iss >> key)) {
    return false;
  }
  std::getline(iss, value);
  value = trim_copy(value);
  return true;
}

inline bool parse_u64(const std::string &text, std::uint64_t &out) {
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

inline bool load_automorphism_replay_fixture(
    const std::string &path, AutomorphismReplayFixture &fixture) {
  std::ifstream ifs(path);
  if (!ifs) {
    return false;
  }

  enum class Section {
    kMeta,
    kInstructions,
    kControlWords,
    kPayloadWords,
    kExpectedControlWords,
    kExpectedPayloadWords,
    kExpectedOutputWords,
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
    if (line == "--control_words--") {
      section = Section::kControlWords;
      continue;
    }
    if (line == "--payload_words--") {
      section = Section::kPayloadWords;
      continue;
    }
    if (line == "--expected_control_words--") {
      section = Section::kExpectedControlWords;
      continue;
    }
    if (line == "--expected_payload_words--") {
      section = Section::kExpectedPayloadWords;
      continue;
    }
    if (line == "--expected_output_words--") {
      section = Section::kExpectedOutputWords;
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
      }
      continue;
    }

    std::uint64_t word = 0U;
    if (!parse_u64(line, word)) {
      return false;
    }
    if (section == Section::kInstructions) {
      fixture.instruction_words.push_back(word);
    } else if (section == Section::kControlWords) {
      fixture.control_words.push_back(word);
    } else if (section == Section::kPayloadWords) {
      fixture.payload_words.push_back(word);
    } else if (section == Section::kExpectedControlWords) {
      fixture.expected_control_words.push_back(word);
    } else if (section == Section::kExpectedPayloadWords) {
      fixture.expected_payload_words.push_back(word);
    } else if (section == Section::kExpectedOutputWords) {
      fixture.expected_output_words.push_back(word);
    }
  }

  return saw_magic && !fixture.instruction_words.empty() &&
         !fixture.control_words.empty() && !fixture.payload_words.empty() &&
         !fixture.expected_control_words.empty() &&
         !fixture.expected_payload_words.empty() &&
         !fixture.expected_output_words.empty();
}

inline bool check_words_equal(const std::vector<std::uint64_t> &got,
                              const std::vector<std::uint64_t> &expected,
                              const std::string &label, std::ostream &err) {
  if (got.size() != expected.size()) {
    err << "[FAIL] " << label << ": size mismatch, got=" << got.size()
        << ", expected=" << expected.size() << '\n';
    return false;
  }
  for (std::size_t i = 0U; i < got.size(); ++i) {
    if (got[i] != expected[i]) {
      err << "[FAIL] " << label << ": index=" << i << ", got=" << got[i]
          << ", expected=" << expected[i] << '\n';
      return false;
    }
  }
  return true;
}

inline bool run_synthetic_case(AutomorphismKernelFn kernel,
                               const std::vector<std::uint64_t> &values,
                               std::int32_t step, const std::string &case_name,
                               std::ostream &out, std::ostream &err) {
  const std::uint32_t coeff_count = static_cast<std::uint32_t>(values.size());
  const std::uint32_t galois_elt =
      cinnamon_hls_kernel::galois_elt_from_step(coeff_count, step);
  if (galois_elt == 0U) {
    err << "[SKIP] " << case_name << ", step=" << step
        << " -> unsupported galois element\n";
    return true;
  }

  const std::vector<std::uint64_t> expected =
      automorphism_expected_ntt(values, step);
  AutomorphismPayloadBuffers buffers = make_payload_buffers(values);
  std::vector<std::uint64_t> outputs(kOutputHeaderWords + kRegisterCount, 0ULL);
  const std::vector<std::uint64_t> instructions = {
      encode_word0(14U, 0U, 0U, 0U, 0U, 0U), encode_word1(step, 0), 0ULL,
      0ULL};

  kernel(instructions.data(), buffers.control_words.data(), buffers.payload_words.data(),
         outputs.data(), static_cast<std::uint32_t>(instructions.size()),
         static_cast<std::uint32_t>(buffers.control_words.size()),
         static_cast<std::uint32_t>(buffers.payload_words.size()),
         static_cast<std::uint32_t>(outputs.size()), 0U);

  const std::uint32_t register_handles_offset = 9U + (kRnsCount * 2U);
  const std::uint32_t handle_metadata_offset =
      register_handles_offset + kRegisterCount;

  if (outputs[0] != 0ULL) {
    err << "[FAIL] " << case_name << ", step=" << step
        << ": status=" << outputs[0] << '\n';
    return false;
  }
  if (outputs[1] != 1ULL) {
    err << "[FAIL] " << case_name << ", step=" << step
        << ": executed=" << outputs[1] << '\n';
    return false;
  }
  if (outputs[kOutputHeaderWords] != 2ULL) {
    err << "[FAIL] " << case_name << ", step=" << step
        << ": dst register handle=" << outputs[kOutputHeaderWords]
        << ", expected 2\n";
    return false;
  }
  if (buffers.control_words[5] != 2ULL ||
      buffers.control_words[register_handles_offset] != 2ULL) {
    err << "[FAIL] " << case_name << ", step=" << step
        << ": new handle state mismatch\n";
    return false;
  }
  if (buffers.control_words[handle_metadata_offset + 2U] != 0ULL ||
      buffers.control_words[handle_metadata_offset + 3U] != kPayloadFlagIsNtt) {
    err << "[FAIL] " << case_name << ", step=" << step
        << ": new handle metadata mismatch\n";
    return false;
  }

  for (std::uint32_t i = 0U; i < coeff_count; ++i) {
    if (buffers.payload_words[coeff_count + i] != expected[i]) {
      err << "[FAIL] " << case_name << ", step=" << step << ", index=" << i
          << ": got=" << buffers.payload_words[coeff_count + i]
          << ", expected=" << expected[i] << '\n';
      return false;
    }
  }

  out << "[PASS] " << case_name << ", step=" << step << '\n';
  return true;
}

inline bool run_replay_fixture(AutomorphismKernelFn kernel,
                               const AutomorphismReplayFixture &fixture,
                               std::ostream &out, std::ostream &err) {
  std::vector<std::uint64_t> control_words = fixture.control_words;
  std::vector<std::uint64_t> payload_words = fixture.payload_words;
  std::vector<std::uint64_t> outputs(fixture.expected_output_words.size(), 0ULL);

  kernel(fixture.instruction_words.data(), control_words.data(), payload_words.data(),
         outputs.data(), static_cast<std::uint32_t>(fixture.instruction_words.size()),
         static_cast<std::uint32_t>(control_words.size()),
         static_cast<std::uint32_t>(payload_words.size()),
         static_cast<std::uint32_t>(outputs.size()), fixture.partition_id);

  if (!check_words_equal(outputs, fixture.expected_output_words, "replay output", err)) {
    return false;
  }
  if (!check_words_equal(control_words, fixture.expected_control_words,
                         "replay control", err)) {
    return false;
  }
  if (!check_words_equal(payload_words, fixture.expected_payload_words,
                         "replay payload", err)) {
    return false;
  }

  out << "[PASS] replay fixture\n";
  return true;
}

inline int run_automorphism_payload_suite(AutomorphismKernelFn kernel,
                                          std::ostream &out = std::cout,
                                          std::ostream &err = std::cerr) {
  const char *replay_path = std::getenv(kAutomorphismReplayEnv);
  if (replay_path != nullptr && replay_path[0] != '\0') {
    AutomorphismReplayFixture fixture;
    if (!load_automorphism_replay_fixture(replay_path, fixture)) {
      err << "Failed to load automorphism replay fixture: " << replay_path << '\n';
      return 1;
    }
    return run_replay_fixture(kernel, fixture, out, err) ? 0 : 1;
  }

  constexpr std::size_t kCoeffCount = 16U;
  const std::vector<std::int32_t> steps =
      default_automorphism_steps(kCoeffCount);
  const std::vector<AutomorphismSyntheticCase> cases =
      default_automorphism_cases(kCoeffCount);

  bool all_pass = true;
  for (const auto &entry : cases) {
    for (const std::int32_t step : steps) {
      all_pass &= run_synthetic_case(kernel, entry.values, step, entry.name, out, err);
    }
  }

  if (!all_pass) {
    err << "Automorphism payload test failed.\n";
    return 1;
  }
  out << "All automorphism payload cases passed.\n";
  return 0;
}

}  // namespace cinnamon_hls_test

#endif  // CINNAMON_HLS_AUTOMORPHISM_PAYLOAD_FIXTURE_HPP_

#pragma once

#include <cstdint>

#include "kernel_common.hpp"

namespace cinnamon_hls_kernel {

constexpr std::uint64_t kPayloadControlMagic = 0x43494E4E5041594CULL;  // "CINNPAYL"
constexpr std::uint64_t kPayloadControlVersion = 1ULL;
constexpr std::uint32_t kPayloadHeaderWords = 9U;
constexpr std::uint32_t kPayloadFlagIsNtt = 1U << 0U;
constexpr std::uint32_t kPayloadInvalidHandle = 0U;
constexpr std::uint64_t kPayloadExtraMagic = 0x43494E4E58425243ULL;  // "CINNXBRC"
constexpr std::uint64_t kPayloadExtraVersion = 1ULL;
constexpr std::uint32_t kPayloadExtraHeaderWords = 6U;

enum PayloadStatus : std::uint32_t {
  kPayloadStatusOk = 0U,
  kPayloadStatusBadMagic = 1U,
  kPayloadStatusBadVersion = 2U,
  kPayloadStatusLayoutOverflow = 3U,
  kPayloadStatusHandleOverflow = 4U,
  kPayloadStatusInvalidHandle = 5U,
  kPayloadStatusMissingToken = 6U,
  kPayloadStatusMissingOutputToken = 7U,
  kPayloadStatusUnsupportedOpcode = 8U,
  kPayloadStatusUnsupportedImmediate = 9U,
  kPayloadStatusMissingModulus = 10U,
  kPayloadStatusBadExtraLayout = 11U,
  kPayloadStatusMissingBciConfig = 12U,
  kPayloadStatusMissingBcuValue = 13U,
};

struct PayloadLayout {
  std::uint32_t register_count = 0U;
  std::uint32_t coeff_count = 0U;
  std::uint32_t rns_table_count = 0U;
  std::uint32_t handle_count = 0U;
  std::uint32_t handle_capacity = 0U;
  std::uint32_t token_count = 0U;
  std::uint32_t output_token_count = 0U;
  std::uint32_t rns_table_offset = 0U;
  std::uint32_t register_handles_offset = 0U;
  std::uint32_t handle_meta_offset = 0U;
  std::uint32_t token_directory_offset = 0U;
  std::uint32_t output_directory_offset = 0U;
};

struct PayloadExtraLayout {
  bool valid = false;
  std::uint32_t base_offset = 0U;
  std::uint32_t bcu_unit_count = 0U;
  std::uint32_t bcu_output_capacity = 0U;
  std::uint32_t bcu_active_count = 0U;
  std::uint32_t bci_entry_count = 0U;
  std::uint32_t bcu_active_offset = 0U;
  std::uint32_t bcu_table_offset = 0U;
  std::uint32_t bci_entries_offset = 0U;
};

inline bool parse_payload_layout(const std::uint64_t *control,
                                 std::uint32_t control_count,
                                 PayloadLayout &layout,
                                 std::uint32_t &status) {
  if (control_count < kPayloadHeaderWords) {
    status = kPayloadStatusLayoutOverflow;
    return false;
  }
  if (control[0] != kPayloadControlMagic) {
    status = kPayloadStatusBadMagic;
    return false;
  }
  if (control[1] != kPayloadControlVersion) {
    status = kPayloadStatusBadVersion;
    return false;
  }

  layout.register_count = static_cast<std::uint32_t>(control[2]);
  layout.coeff_count = static_cast<std::uint32_t>(control[3]);
  layout.rns_table_count = static_cast<std::uint32_t>(control[4]);
  layout.handle_count = static_cast<std::uint32_t>(control[5]);
  layout.handle_capacity = static_cast<std::uint32_t>(control[6]);
  layout.token_count = static_cast<std::uint32_t>(control[7]);
  layout.output_token_count = static_cast<std::uint32_t>(control[8]);

  std::uint32_t offset = kPayloadHeaderWords;
  layout.rns_table_offset = offset;
  offset += layout.rns_table_count * 2U;
  layout.register_handles_offset = offset;
  offset += layout.register_count;
  layout.handle_meta_offset = offset;
  offset += layout.handle_capacity * 2U;
  layout.token_directory_offset = offset;
  offset += layout.token_count * 2U;
  layout.output_directory_offset = offset;
  offset += layout.output_token_count * 2U;
  if (offset > control_count) {
    status = kPayloadStatusLayoutOverflow;
    return false;
  }

  status = kPayloadStatusOk;
  return true;
}

inline bool parse_payload_extra_layout(const std::uint64_t *control,
                                       std::uint32_t control_count,
                                       const PayloadLayout &layout,
                                       PayloadExtraLayout &extra,
                                       std::uint32_t &status) {
  const std::uint32_t base_offset =
      layout.output_directory_offset + layout.output_token_count * 2U;
  extra = PayloadExtraLayout{};
  extra.base_offset = base_offset;
  if (base_offset >= control_count) {
    status = kPayloadStatusOk;
    return false;
  }
  if (control[base_offset] != kPayloadExtraMagic) {
    status = kPayloadStatusOk;
    return false;
  }
  if ((base_offset + kPayloadExtraHeaderWords) > control_count) {
    status = kPayloadStatusBadExtraLayout;
    return false;
  }
  if (control[base_offset + 1U] != kPayloadExtraVersion) {
    status = kPayloadStatusBadExtraLayout;
    return false;
  }

  extra.bcu_unit_count = static_cast<std::uint32_t>(control[base_offset + 2U]);
  extra.bcu_output_capacity =
      static_cast<std::uint32_t>(control[base_offset + 3U]);
  extra.bcu_active_count = static_cast<std::uint32_t>(control[base_offset + 4U]);
  extra.bci_entry_count = static_cast<std::uint32_t>(control[base_offset + 5U]);
  extra.bcu_active_offset = base_offset + kPayloadExtraHeaderWords;
  extra.bcu_table_offset = extra.bcu_active_offset + extra.bcu_active_count;
  extra.bci_entries_offset =
      extra.bcu_table_offset + extra.bcu_unit_count * extra.bcu_output_capacity;
  if (extra.bci_entries_offset > control_count) {
    status = kPayloadStatusBadExtraLayout;
    return false;
  }

  extra.valid = true;
  status = kPayloadStatusOk;
  return true;
}

inline std::uint64_t payload_trace(const std::uint32_t *register_handles,
                                   std::uint32_t register_count,
                                   std::uint32_t module_id,
                                   std::uint32_t executed) {
  std::uint64_t acc = 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(module_id);
  acc ^= static_cast<std::uint64_t>(executed) << 16U;
  for (std::uint32_t i = 0; i < register_count; ++i) {
#pragma HLS PIPELINE II = 1
    acc ^= hash_mix(static_cast<std::uint64_t>(register_handles[i]) ^
                    static_cast<std::uint64_t>(i + 1U));
    acc = rotl64(acc, 7U);
  }
  return acc;
}

inline void write_payload_module_outputs(std::uint64_t *outputs,
                                         std::uint32_t output_count,
                                         std::uint32_t status,
                                         const std::uint32_t *register_handles,
                                         std::uint32_t register_count,
                                         std::uint32_t module_id,
                                         std::uint32_t partition_id,
                                         std::uint32_t executed) {
  if (output_count == 0U) {
    return;
  }
  outputs[0] = static_cast<std::uint64_t>(status);
  if (output_count > 1U) {
    outputs[1] = executed;
  }
  if (output_count > 2U) {
    outputs[2] = register_count;
  }
  if (output_count > 3U) {
    outputs[3] = module_id;
  }
  if (output_count > 4U) {
    outputs[4] = partition_id;
  }
  if (output_count > 5U) {
    outputs[5] =
        payload_trace(register_handles, register_count, module_id, executed);
  }

  const std::uint32_t state_words_available =
      (output_count > kOutputHeaderWords) ? (output_count - kOutputHeaderWords) : 0U;
  const std::uint32_t words_to_copy =
      (state_words_available < register_count) ? state_words_available : register_count;
  for (std::uint32_t i = 0; i < words_to_copy; ++i) {
#pragma HLS PIPELINE II = 1
    outputs[kOutputHeaderWords + i] = register_handles[i];
  }
  for (std::uint32_t i = words_to_copy; i < state_words_available; ++i) {
#pragma HLS PIPELINE II = 1
    outputs[kOutputHeaderWords + i] = 0ULL;
  }
}

inline std::uint32_t payload_pair_lookup(const std::uint64_t *control,
                                         std::uint32_t pair_base,
                                         std::uint32_t pair_count,
                                         std::uint64_t token_key) {
  std::uint32_t lo = 0U;
  std::uint32_t hi = pair_count;
  while (lo < hi) {
#pragma HLS PIPELINE II = 1
    const std::uint32_t mid = lo + ((hi - lo) >> 1U);
    const std::uint32_t cursor = pair_base + (mid << 1U);
    const std::uint64_t key = control[cursor];
    if (key == token_key) {
      return static_cast<std::uint32_t>(control[cursor + 1U]);
    }
    if (key < token_key) {
      lo = mid + 1U;
    } else {
      hi = mid;
    }
  }
  return kPayloadInvalidHandle;
}

inline bool payload_pair_update(std::uint64_t *control, std::uint32_t pair_base,
                                std::uint32_t pair_count,
                                std::uint64_t token_key,
                                std::uint32_t handle_id) {
  std::uint32_t lo = 0U;
  std::uint32_t hi = pair_count;
  while (lo < hi) {
#pragma HLS PIPELINE II = 1
    const std::uint32_t mid = lo + ((hi - lo) >> 1U);
    const std::uint32_t cursor = pair_base + (mid << 1U);
    const std::uint64_t key = control[cursor];
    if (key == token_key) {
      control[cursor + 1U] = static_cast<std::uint64_t>(handle_id);
      return true;
    }
    if (key < token_key) {
      lo = mid + 1U;
    } else {
      hi = mid;
    }
  }
  return false;
}

inline std::uint64_t payload_lookup_modulus(const std::uint64_t *control,
                                            const PayloadLayout &layout,
                                            std::uint32_t rns_base_id) {
  for (std::uint32_t i = 0; i < layout.rns_table_count; ++i) {
#pragma HLS PIPELINE II = 1
    const std::uint32_t cursor = layout.rns_table_offset + (i << 1U);
    if (static_cast<std::uint32_t>(control[cursor]) == rns_base_id) {
      return control[cursor + 1U];
    }
  }
  return 0U;
}

inline std::uint32_t payload_handle_rns_base_id(const std::uint64_t *control,
                                                const PayloadLayout &layout,
                                                std::uint32_t handle_id) {
  const std::uint32_t live_handle_count = static_cast<std::uint32_t>(control[5]);
  if (handle_id == 0U || handle_id > live_handle_count ||
      handle_id > layout.handle_capacity) {
    return 0U;
  }
  const std::uint32_t cursor =
      layout.handle_meta_offset + ((handle_id - 1U) << 1U);
  return static_cast<std::uint32_t>(control[cursor]);
}

inline std::uint32_t payload_handle_flags(const std::uint64_t *control,
                                          const PayloadLayout &layout,
                                          std::uint32_t handle_id) {
  const std::uint32_t live_handle_count = static_cast<std::uint32_t>(control[5]);
  if (handle_id == 0U || handle_id > live_handle_count ||
      handle_id > layout.handle_capacity) {
    return 0U;
  }
  const std::uint32_t cursor =
      layout.handle_meta_offset + ((handle_id - 1U) << 1U);
  return static_cast<std::uint32_t>(control[cursor + 1U]);
}

inline std::uint32_t payload_handle_coeff_offset(const PayloadLayout &layout,
                                                 std::uint32_t handle_id) {
  return (handle_id - 1U) * layout.coeff_count;
}

inline std::uint32_t payload_allocate_handle(std::uint64_t *control,
                                             const PayloadLayout &layout,
                                             std::uint32_t rns_base_id,
                                             bool is_ntt_form,
                                             std::uint32_t &status) {
  const std::uint32_t handle_count =
      static_cast<std::uint32_t>(control[5]);
  if (handle_count >= layout.handle_capacity) {
    status = kPayloadStatusHandleOverflow;
    return 0U;
  }
  const std::uint32_t handle_id = handle_count + 1U;
  control[5] = static_cast<std::uint64_t>(handle_id);
  const std::uint32_t cursor =
      layout.handle_meta_offset + ((handle_id - 1U) << 1U);
  control[cursor] = static_cast<std::uint64_t>(rns_base_id);
  control[cursor + 1U] =
      static_cast<std::uint64_t>(is_ntt_form ? kPayloadFlagIsNtt : 0U);
  status = kPayloadStatusOk;
  return handle_id;
}

}  // namespace cinnamon_hls_kernel

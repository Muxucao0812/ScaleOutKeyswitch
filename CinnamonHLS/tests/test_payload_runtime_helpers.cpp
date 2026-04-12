#include <cassert>
#include <cstdint>
#include <vector>

#include "kernel_ntt_module.hpp"
#include "kernel_payload_common.hpp"

int main() {
  using namespace cinnamon_hls_kernel;

  std::vector<std::uint64_t> control(32, 0);
  PayloadLayout layout;
  layout.handle_count = 1;
  layout.handle_capacity = 4;
  layout.handle_meta_offset = 12;

  control[5] = 1;  // live handle count stored in payload control header
  control[12] = 7;
  control[13] = kPayloadFlagIsNtt;

  std::uint32_t status = kPayloadStatusOk;
  const std::uint32_t new_handle =
      payload_allocate_handle(control.data(), layout, 44, false, status);
  assert(status == kPayloadStatusOk);
  assert(new_handle == 2U);
  assert(control[5] == 2U);
  assert(payload_handle_rns_base_id(control.data(), layout, new_handle) == 44U);
  assert(payload_handle_flags(control.data(), layout, new_handle) == 0U);

  assert(decode_bcu_output_id(0x800U + 50U) == 0U);
  assert(decode_bcu_output_id(0x800U + (1U << 7U) + 3U) == 1U);
  assert(decode_bcu_output_id(0x800U + (7U << 7U) + 127U) == 7U);

  return 0;
}

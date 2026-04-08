#include <cassert>
#include <cstdint>
#include <vector>

#include "cinnamon_hls/montgomery.hpp"
#include "cinnamon_hls/montgomery_kernel.hpp"

extern "C" {
void montgomery_core_top(const std::uint64_t *a, const std::uint64_t *b,
                         std::uint64_t *out, std::uint32_t count,
                         std::uint64_t q, std::uint32_t opcode,
                         std::uint32_t one_iter_log2,
                         std::uint32_t num_iterations);
void montgomery_axi_top(const std::uint64_t *a, const std::uint64_t *b,
                        std::uint64_t *out, std::uint32_t count,
                        std::uint64_t q, std::uint32_t opcode,
                        std::uint32_t one_iter_log2,
                        std::uint32_t num_iterations);
}

int main() {
  constexpr std::uint32_t kW = 17;
  constexpr std::uint32_t kIter = 2;

  {
    const std::uint64_t q = 268042241;
    const std::vector<std::uint64_t> a = {34234, 14, 4, 456};
    std::vector<std::uint64_t> out(a.size(), 0);
    montgomery_core_top(a.data(), nullptr, out.data(), static_cast<std::uint32_t>(a.size()),
                        q, cinnamon_hls::kMontgomeryReduce, kW, kIter);

    assert(out[0] == 32887156);
    assert(out[1] == cinnamon_hls::montgomery_reduce_ntt_friendly(14, q, kW, kIter));
    assert(out[2] == 16728100);
    assert(out[3] == cinnamon_hls::montgomery_reduce_ntt_friendly(456, q, kW, kIter));
  }

  {
    const std::vector<std::uint64_t> a = {34234, 14, 4, 32768};
    const std::vector<std::uint64_t> b = {7652, 7652, 8986652, 8986652};
    const std::vector<std::uint64_t> q = {268042241, 265420801, 268042241, 265420801};

    std::vector<std::uint64_t> out(a.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
      montgomery_axi_top(&a[i], &b[i], &out[i], 1, q[i],
                         cinnamon_hls::kMontgomeryMultiply, kW, kIter);
    }

    assert(out[0] == 228895654);
    assert(out[1] == 20329345);
    assert(out[2] == 266794278);
    assert(out[3] == 228081843);
  }

  {
    constexpr std::uint64_t q = 268042241;
    const std::vector<std::uint64_t> x = {0, 1, 2, 5, 12345, q - 1};
    std::vector<std::uint64_t> mont(x.size(), 0);
    std::vector<std::uint64_t> back(x.size(), 0);

    montgomery_core_top(x.data(), nullptr, mont.data(), static_cast<std::uint32_t>(x.size()),
                        q, cinnamon_hls::kMontgomeryToDomain, kW, kIter);
    montgomery_core_top(mont.data(), nullptr, back.data(), static_cast<std::uint32_t>(x.size()),
                        q, cinnamon_hls::kMontgomeryFromDomain, kW, kIter);

    for (std::size_t i = 0; i < x.size(); ++i) {
      assert(back[i] == (x[i] % q));
    }
  }

  return 0;
}

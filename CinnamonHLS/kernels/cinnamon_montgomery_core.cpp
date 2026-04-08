#include <cstdint>

#include "cinnamon_hls/montgomery_kernel.hpp"

namespace {

inline void run_montgomery_core(const std::uint64_t *a, const std::uint64_t *b,
                                std::uint64_t *out, std::uint32_t count,
                                std::uint64_t q, std::uint32_t opcode,
                                std::uint32_t one_iter_log2,
                                std::uint32_t num_iterations) {
  cinnamon_hls::montgomery_core(a, b, out, count, q, opcode, one_iter_log2,
                                num_iterations);
}

}  // namespace

extern "C" {

void montgomery_core_top(const std::uint64_t *a, const std::uint64_t *b,
                         std::uint64_t *out, std::uint32_t count,
                         std::uint64_t q, std::uint32_t opcode,
                         std::uint32_t one_iter_log2,
                         std::uint32_t num_iterations) {
  run_montgomery_core(a, b, out, count, q, opcode, one_iter_log2, num_iterations);
}

void montgomery_axi_top(const std::uint64_t *a, const std::uint64_t *b,
                        std::uint64_t *out, std::uint32_t count,
                        std::uint64_t q, std::uint32_t opcode,
                        std::uint32_t one_iter_log2,
                        std::uint32_t num_iterations) {
#pragma HLS INTERFACE m_axi port = a offset = slave bundle = gmem0
#pragma HLS INTERFACE m_axi port = b offset = slave bundle = gmem1
#pragma HLS INTERFACE m_axi port = out offset = slave bundle = gmem2
#pragma HLS INTERFACE s_axilite port = a bundle = control
#pragma HLS INTERFACE s_axilite port = b bundle = control
#pragma HLS INTERFACE s_axilite port = out bundle = control
#pragma HLS INTERFACE s_axilite port = count bundle = control
#pragma HLS INTERFACE s_axilite port = q bundle = control
#pragma HLS INTERFACE s_axilite port = opcode bundle = control
#pragma HLS INTERFACE s_axilite port = one_iter_log2 bundle = control
#pragma HLS INTERFACE s_axilite port = num_iterations bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
  run_montgomery_core(a, b, out, count, q, opcode, one_iter_log2, num_iterations);
}

}  // extern "C"

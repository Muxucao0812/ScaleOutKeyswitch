#include "context.h"

#include <algorithm>

namespace Tutorial {

uint64_t CryptoContext::elementBytes() const
{
    const uint64_t bytes_per_limb = std::max<uint32_t>(1, (data_width_bits + 7) / 8);
    return bytes_per_limb * std::max<uint32_t>(1, limbs);
}

CryptoContext loadCryptoContext(
    const SST::Params& params,
    uint32_t fallback_poly_degree_n,
    uint32_t fallback_data_width_bits,
    uint32_t fallback_limbs)
{
    const SST::Params scoped = params.get_scoped_params("context");

    CryptoContext context;
    context.poly_degree_n = scoped.find<uint32_t>("n", fallback_poly_degree_n);
    context.data_width_bits = scoped.find<uint32_t>("data_width_bits", fallback_data_width_bits);
    context.limbs = scoped.find<uint32_t>("limbs", fallback_limbs);

    if (context.poly_degree_n == 0) {
        context.poly_degree_n = std::max<uint32_t>(1, fallback_poly_degree_n);
    }
    if (context.data_width_bits == 0) {
        context.data_width_bits = std::max<uint32_t>(1, fallback_data_width_bits);
    }
    if (context.limbs == 0) {
        context.limbs = std::max<uint32_t>(1, fallback_limbs);
    }

    return context;
}

} // namespace Tutorial

#ifndef _SST_TUTORIAL_CONTEXT_H
#define _SST_TUTORIAL_CONTEXT_H

#include <cstdint>

#include <sst/core/params.h>

namespace Tutorial {

struct CryptoContext {
    uint32_t poly_degree_n = 65536;
    uint32_t data_width_bits = 32;
    uint32_t limbs = 27;
    uint32_t dnum  = 3;
    // alpha = limbs / dnum
    uint32_t alpha = limbs / dnum;

    uint64_t elementBytes() const;
};

CryptoContext loadCryptoContext(
    const SST::Params& params,
    uint32_t fallback_poly_degree_n,
    uint32_t fallback_data_width_bits,
    uint32_t fallback_limbs);

} // namespace Tutorial

#endif

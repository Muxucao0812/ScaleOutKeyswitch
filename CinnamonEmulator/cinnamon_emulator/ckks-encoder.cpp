// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include "ckks-encoder.h"

namespace Cinnamon::Emulator {

CKKSEncoder::CKKSEncoder(const Context &context)
    : context_(context), use_random_seed_(false),
      pool_(seal::MemoryManager::GetPool()) {
  prng_ = std::move(
      seal::UniformRandomGeneratorFactory::DefaultFactory()->create());
  initialize();
}

CKKSEncoder::CKKSEncoder(const Context &context,
                         const seal::prng_seed_type &prng_seed)
    : context_(context), use_random_seed_(true), prng_seed_(prng_seed),
      pool_(seal::MemoryManager::GetPool()) {
  prng_ =
      std::move(seal::UniformRandomGeneratorFactory::DefaultFactory()->create(
          prng_seed_));
  initialize();
}

void CKKSEncoder::initialize() {

  size_t coeff_count = context_.n();
  slots_ = context_.slots();
  int logn = seal::util::get_power_of_two(coeff_count);

  matrix_reps_index_map_ = seal::util::allocate<size_t>(coeff_count, pool_);

  // Copy from the matrix to the value vectors
  uint64_t gen = 5;
  uint64_t pos = 1;
  uint64_t m = static_cast<uint64_t>(coeff_count) << 1;
  for (size_t i = 0; i < slots_; i++) {
    // Position in normal bit order
    uint64_t index1 = (pos - 1) >> 1;
    uint64_t index2 = (m - pos - 1) >> 1;

    // Set the bit-reversed locations
    matrix_reps_index_map_[i] =
        seal::util::safe_cast<size_t>(seal::util::reverse_bits(index1, logn));
    matrix_reps_index_map_[slots_ | i] =
        seal::util::safe_cast<size_t>(seal::util::reverse_bits(index2, logn));

    // Next primitive root
    pos *= gen;
    pos &= (m - 1);
  }

  // We need 1~(n-1)-th powers of the primitive 2n-th root, m = 2n
  root_powers_ = seal::util::allocate<std::complex<double>>(coeff_count, pool_);
  inv_root_powers_ =
      seal::util::allocate<std::complex<double>>(coeff_count, pool_);
  // Powers of the primitive 2n-th root have 4-fold symmetry
  if (m >= 8) {
    complex_roots_ = std::make_shared<seal::util::ComplexRoots>(
        seal::util::ComplexRoots(static_cast<size_t>(m), pool_));
    for (size_t i = 1; i < coeff_count; i++) {
      root_powers_[i] =
          complex_roots_->get_root(seal::util::reverse_bits(i, logn));
      inv_root_powers_[i] = conj(
          complex_roots_->get_root(seal::util::reverse_bits(i - 1, logn) + 1));
    }
  } else {
    throw std::logic_error("Invalid m");
  }

  complex_arith_ = ComplexArith();
  fft_handler_ = FFTHandler(complex_arith_);
}

std::shared_ptr<RnsPolynomial>
CKKSEncoder::encode_internal(double value, double scale,
                             const std::vector<std::uint64_t> &rns_base_ids,
                             seal::MemoryPoolHandle pool) {
  // Verify parameters.
  if (!pool) {
    throw std::invalid_argument("pool is uninitialized");
  }
  if (rns_base_ids.empty()) {
    throw std::invalid_argument("rns_base_ids is empty");
  }

  std::size_t num_limbs = rns_base_ids.size();
  std::size_t coeff_count = context_.n();

  // Quick sanity check
  // if (!product_fits_in(coeff_modulus_size, coeff_count))
  // {
  //     throw logic_error("invalid parameters");
  // }

  // Check that scale is positive and not too large
  if (scale <= 0 || (static_cast<int>(log2(scale)) + 1 >=
                     context_.rns_base_bit_count(num_limbs))) {
    throw std::invalid_argument("scale out of bounds");
  }

  // Compute the scaled value
  value *= scale;

  // Verify that the values are not too large to fit in coeff_modulus
  // Note that we have an extra + 1 for the sign bit
  // Don't compute logarithmis of numbers less than 1
  // int coeff_bit_count = static_cast<int>(log2(fabs(value))) + 2;
  int coeff_bit_count =
      static_cast<int>(std::ceil(std::log2(std::max<>(fabs(value), 1.0)))) + 1;
  if (coeff_bit_count >= context_.rns_base_bit_count(num_limbs)) {
    throw std::invalid_argument("encoded value is too large");
  }

  double two_pow_64 = pow(2.0, 64);

  std::vector<seal::Modulus> rns_modulii;
  rns_modulii.reserve(num_limbs);
  for (std::size_t i = 0; i < num_limbs; i++) {
    rns_modulii.push_back(context_.get_rns_modulus(rns_base_ids.at(i)));
  }

  // auto destination_data =
  // seal::util::allocate<seal::util::Pointer<Limb::Element_t>>(num_limbs,
  // pool_);
  auto destination_data =
      Util::allocate<Util::Pointer<Limb::Element_t>>(num_limbs);
  // std::for_each_n(seal::util::iter(destination_data), num_limbs, [&](auto &I)
  // {
  std::for_each_n(destination_data.get(), num_limbs, [&](auto &I) {
    // I = Util::allocate_uint(coeff_count, pool_);
    I = Util::allocate_uint2(coeff_count);
  });

  double coeffd = coordinate_wise_random_rounding(value);
  bool is_negative = std::signbit(coeffd);
  coeffd = std::fabs(coeffd);

  auto n = context_.n();
  // Use faster decomposition methods when possible
  if (coeff_bit_count <= 64) {
    uint64_t coeffu = static_cast<uint64_t>(fabs(coeffd));

    if (is_negative) {
      for (size_t j = 0; j < num_limbs; j++) {

        auto temp = seal::util::negate_uint_mod(
            seal::util::barrett_reduce_64(coeffu, rns_modulii.at(j)),
            rns_modulii.at(j));
        for (std::size_t i = 0; i < n; i++) {
          destination_data[j][i] = Util::safe_cast<Limb::Element_t>(temp);
        }
      }
    } else {
      for (std::size_t j = 0; j < num_limbs; j++) {
        auto temp = seal::util::barrett_reduce_64(coeffu, rns_modulii[j]);
        for (std::size_t i = 0; i < n; i++) {
          destination_data[j][i] = Util::safe_cast<Limb::Element_t>(temp);
        }
      }
    }
  } else if (coeff_bit_count <= 128) {

    std::uint64_t coeffu[2]{
        static_cast<std::uint64_t>(std::fmod(coeffd, two_pow_64)),
        static_cast<std::uint64_t>(coeffd / two_pow_64)};

    if (is_negative) {
      for (std::size_t j = 0; j < num_limbs; j++) {
        auto temp = seal::util::negate_uint_mod(
            seal::util::barrett_reduce_128(coeffu, rns_modulii.at(j)),
            rns_modulii.at(j));
        for (std::size_t i = 0; i < n; i++) {
          destination_data[j][i] = Util::safe_cast<Limb::Element_t>(temp);
        }
      }
    } else {
      for (std::size_t j = 0; j < num_limbs; j++) {
        auto temp = seal::util::barrett_reduce_128(coeffu, rns_modulii.at(j));
        for (std::size_t i = 0; i < n; i++) {
          destination_data[j][i] = Util::safe_cast<Limb::Element_t>(temp);
        }
      }
    }
  } else {
    throw std::logic_error("unimplemented");
#if 0
            // Slow case
            auto coeffu(allocate_uint(coeff_modulus_size, pool));

            // We are at this point guaranteed to fit in the allocated space
            set_zero_uint(coeff_modulus_size, coeffu.get());
            auto coeffu_ptr = coeffu.get();
            while (coeffd >= 1)
            {
                *coeffu_ptr++ = static_cast<uint64_t>(fmod(coeffd, two_pow_64));
                coeffd /= two_pow_64;
            }

            // Next decompose this coefficient
            context_data.rns_tool()->base_q()->decompose(coeffu.get(), pool);

            // Finally replace the sign if necessary
            if (is_negative)
            {
                for (size_t j = 0; j < coeff_modulus_size; j++)
                {
                    fill_n(
                        destination.data() + (j * coeff_count), coeff_count,
                        negate_uint_mod(coeffu[j], coeff_modulus[j]));
                }
            }
            else
            {
                for (size_t j = 0; j < coeff_modulus_size; j++)
                {
                    fill_n(destination.data() + (j * coeff_count), coeff_count, coeffu[j]);
                }
            }
#endif
  }

  auto destination = std::make_shared<RnsPolynomial>();
  // Transform to NTT domain
  for (std::size_t i = 0; i < num_limbs; i++) {
    auto dest = std::make_shared<Limb>(std::move(destination_data[i]),
                                       coeff_count, rns_base_ids.at(i), true);
    destination->write_limb(std::move(dest), rns_base_ids.at(i));
  }

  return destination;
}

std::map<uint32_t, Limb::Element_t> CKKSEncoder::encode_internal_scalar(
    double value, double scale, const std::vector<std::uint64_t> &rns_base_ids,
    seal::MemoryPoolHandle pool) {
  // Verify parameters.
  if (!pool) {
    throw std::invalid_argument("pool is uninitialized");
  }
  if (rns_base_ids.empty()) {
    throw std::invalid_argument("rns_base_ids is empty");
  }

  std::size_t num_limbs = rns_base_ids.size();
  std::size_t coeff_count = context_.n();

  // Quick sanity check
  // if (!product_fits_in(coeff_modulus_size, coeff_count))
  // {
  //     throw logic_error("invalid parameters");
  // }

  // Check that scale is positive and not too large
  if (scale <= 0 || (static_cast<int>(log2(scale)) + 1 >=
                     context_.rns_base_bit_count(num_limbs))) {
    throw std::invalid_argument("scale out of bounds");
  }

  // Compute the scaled value
  value *= scale;

  // Verify that the values are not too large to fit in coeff_modulus
  // Note that we have an extra + 1 for the sign bit
  // Don't compute logarithmis of numbers less than 1
  // int coeff_bit_count = static_cast<int>(log2(fabs(value))) + 2;
  int coeff_bit_count =
      static_cast<int>(std::ceil(std::log2(std::max<>(fabs(value), 1.0)))) + 1;
  if (coeff_bit_count >= context_.rns_base_bit_count(num_limbs)) {
    throw std::invalid_argument("encoded value is too large");
  }

  double two_pow_64 = pow(2.0, 64);

  std::vector<seal::Modulus> rns_modulii;
  rns_modulii.reserve(num_limbs);
  for (std::size_t i = 0; i < num_limbs; i++) {
    rns_modulii.push_back(context_.get_rns_modulus(rns_base_ids.at(i)));
  }

  double coeffd = coordinate_wise_random_rounding(value);
  bool is_negative = std::signbit(coeffd);
  coeffd = std::fabs(coeffd);

  std::map<uint32_t, Limb::Element_t> destination;

  auto n = context_.n();
  // Use faster decomposition methods when possible
  if (coeff_bit_count <= 64) {
    uint64_t coeffu = static_cast<uint64_t>(fabs(coeffd));

    if (is_negative) {
      for (size_t j = 0; j < num_limbs; j++) {

        auto temp = seal::util::negate_uint_mod(
            seal::util::barrett_reduce_64(coeffu, rns_modulii.at(j)),
            rns_modulii.at(j));
        destination[j] = Util::safe_cast<Limb::Element_t>(temp);
      }
    } else {
      for (std::size_t j = 0; j < num_limbs; j++) {
        auto temp = seal::util::barrett_reduce_64(coeffu, rns_modulii[j]);
        destination[j] = Util::safe_cast<Limb::Element_t>(temp);
      }
    }
  } else if (coeff_bit_count <= 128) {

    std::uint64_t coeffu[2]{
        static_cast<std::uint64_t>(std::fmod(coeffd, two_pow_64)),
        static_cast<std::uint64_t>(coeffd / two_pow_64)};

    if (is_negative) {
      for (std::size_t j = 0; j < num_limbs; j++) {
        auto temp = seal::util::negate_uint_mod(
            seal::util::barrett_reduce_128(coeffu, rns_modulii.at(j)),
            rns_modulii.at(j));
        destination[j] = Util::safe_cast<Limb::Element_t>(temp);
      }
    } else {
      for (std::size_t j = 0; j < num_limbs; j++) {
        auto temp = seal::util::barrett_reduce_128(coeffu, rns_modulii.at(j));
        destination[j] = Util::safe_cast<Limb::Element_t>(temp);
      }
    }
  } else {
    throw std::logic_error("unimplemented");
#if 0
            // Slow case
            auto coeffu(allocate_uint(coeff_modulus_size, pool));

            // We are at this point guaranteed to fit in the allocated space
            set_zero_uint(coeff_modulus_size, coeffu.get());
            auto coeffu_ptr = coeffu.get();
            while (coeffd >= 1)
            {
                *coeffu_ptr++ = static_cast<uint64_t>(fmod(coeffd, two_pow_64));
                coeffd /= two_pow_64;
            }

            // Next decompose this coefficient
            context_data.rns_tool()->base_q()->decompose(coeffu.get(), pool);

            // Finally replace the sign if necessary
            if (is_negative)
            {
                for (size_t j = 0; j < coeff_modulus_size; j++)
                {
                    fill_n(
                        destination.data() + (j * coeff_count), coeff_count,
                        negate_uint_mod(coeffu[j], coeff_modulus[j]));
                }
            }
            else
            {
                for (size_t j = 0; j < coeff_modulus_size; j++)
                {
                    fill_n(destination.data() + (j * coeff_count), coeff_count, coeffu[j]);
                }
            }
#endif
  }

  return destination;
}

} // namespace Cinnamon::Emulator

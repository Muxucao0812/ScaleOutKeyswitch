// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <seal/memorymanager.h>
#include <seal/seal.h>
#include <seal/util/mempool.h>
#include <seal/util/rns.h>
#include <seal/util/uintcore.h>

#include "context.h"
#include "limb.h"
#include "rns_polynomial.h"
#include "util.h"

namespace Cinnamon::Emulator {

class CKKSEncoder {

  using ComplexArith = seal::util::Arithmetic<std::complex<double>,
                                              std::complex<double>, double>;
  using FFTHandler = seal::util::DWTHandler<std::complex<double>,
                                            std::complex<double>, double>;

public:
  CKKSEncoder(const Context &context);
  CKKSEncoder(const Context &context, const seal::prng_seed_type &prng_seed);

  template <typename T,
            typename = std::enable_if_t<
                std::is_same<std::remove_cv_t<T>, double>::value ||
                std::is_same<std::remove_cv_t<T>, std::complex<double>>::value>>
  inline std::shared_ptr<RnsPolynomial>
  encode(const std::vector<T> &values, const double scale,
         const std::vector<std::uint64_t> &rns_base_ids,
         seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {
    return encode_internal(values.data(), values.size(), scale, rns_base_ids,
                           std::move(pool));
  }

  inline std::shared_ptr<RnsPolynomial>
  encode(const double value, const double scale,
         const std::vector<std::uint64_t> &rns_base_ids,
         seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {
    return encode_internal(value, scale, rns_base_ids, std::move(pool));
  }

  inline std::map<uint32_t, Limb::Element_t>
  encode_scalar(const double value, const double scale,
                const std::vector<std::uint64_t> &rns_base_ids,
                seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {
    return encode_internal_scalar(value, scale, rns_base_ids, std::move(pool));
  }

  template <typename T,
            typename = std::enable_if_t<
                std::is_same<std::remove_cv_t<T>, double>::value ||
                std::is_same<std::remove_cv_t<T>, std::complex<double>>::value>>
  inline std::vector<T>
  decode(const std::shared_ptr<RnsPolynomial> &plain, const double scale,
         seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {
    std::vector<T> destination;
    destination.resize(slots_);
    decode_internal(plain, scale, destination.data(), std::move(pool));
    return std::move(destination);
  }

  template <typename T,
            typename = std::enable_if_t<
                std::is_same<std::remove_cv_t<T>, double>::value ||
                std::is_same<std::remove_cv_t<T>, std::complex<double>>::value>>
  inline std::vector<double> encode_as_polynomial(
      const std::vector<T> &values, const double scale,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {
    return encode_as_polynomial_internal(values.data(), values.size(), scale,
                                         std::move(pool));
  }

private:
  const Context &context_;

  seal::MemoryPoolHandle pool_;

  std::size_t slots_;

  std::shared_ptr<seal::util::ComplexRoots> complex_roots_;

  // Holds 1~(n-1)-th powers of root in bit-reversed order, the 0-th power is
  // left unset.
  seal::util::Pointer<std::complex<double>> root_powers_;

  // Holds 1~(n-1)-th powers of inverse root in scrambled order, the 0-th power
  // is left unset.
  seal::util::Pointer<std::complex<double>> inv_root_powers_;

  seal::util::Pointer<std::size_t> matrix_reps_index_map_;

  ComplexArith complex_arith_;

  FFTHandler fft_handler_;

  bool use_random_seed_;
  seal::prng_seed_type prng_seed_;
  std::shared_ptr<seal::UniformRandomGenerator> prng_;

  void initialize();

  inline double coordinate_wise_random_rounding(const double coeff) {
    double coeff_floor = std::floor(coeff);
    double fractional_part = coeff - std::floor(coeff);
    double probability_round_up = fractional_part;

    uint32_t random_int;
    uint32_t MAX_UINT_32 = 0xffffffff;
    prng_->generate(sizeof(random_int),
                    reinterpret_cast<seal::seal_byte *>(&random_int));
    double random_sample = double(random_int) / double(MAX_UINT_32);
    double rounded_value;
    if (random_sample < probability_round_up) {
      rounded_value = coeff_floor + double(1.0);
    } else {
      rounded_value = coeff_floor;
    }
    return rounded_value;
  };

  std::shared_ptr<RnsPolynomial>
  encode_internal(double value, double scale,
                  const std::vector<std::uint64_t> &rns_base_ids,
                  seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());

  std::map<uint32_t, Limb::Element_t> encode_internal_scalar(
      double value, double scale,
      const std::vector<std::uint64_t> &rns_base_ids,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());

  template <typename T,
            typename = std::enable_if_t<
                std::is_same<std::remove_cv_t<T>, double>::value ||
                std::is_same<std::remove_cv_t<T>, std::complex<double>>::value>>

  std::shared_ptr<RnsPolynomial> encode_internal(

      const T *values, std::size_t values_size, const double scale,
      const std::vector<std::uint64_t> &rns_base_ids,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {
    // Verify parameters.
    if (!values && values_size > 0) {
      throw std::invalid_argument("values cannot be null");
    }
    if (values_size != context_.slots()) {
      throw std::invalid_argument("values_size must be equal to context slots");
    }
    if (!pool) {
      throw std::invalid_argument("pool is uninitialized");
    }
    if (rns_base_ids.empty()) {
      throw std::invalid_argument("rns_base_ids is empty");
    }

    std::size_t num_limbs = rns_base_ids.size();
    std::size_t coeff_count = context_.n();

    // // Quick sanity check
    // if (!seal::util::product_fits_in(coeff_modulus_size, coeff_count))
    // {
    //     throw std::logic_error("invalid parameters");
    // }

    // Check that scale is positive and not too large
    if (scale <= 0 || (static_cast<int>(log2(scale)) + 1 >=
                       context_.rns_base_bit_count(num_limbs))) {
      throw std::invalid_argument("scale out of bounds");
    }

    auto ntt_tables = context_.ntt_tables();

    // values_size is guaranteed to be no bigger than slots_
    // std::size_t n = util::mul_safe(slots_, std::size_t(2));
    auto slots_ = context_.slots();
    auto n = context_.n();

    // auto conj_values = seal::util::allocate<std::complex<double>>(n, pool,
    // 0);
    auto conj_values = Util::allocate<std::complex<double>>(n);
    std::fill_n(conj_values.get(), n, 0);
    for (std::size_t i = 0; i < values_size; i++) {
      conj_values[matrix_reps_index_map_[i]] = values[i];
      // TODO: if values are real, the following values should be set to zero,
      // and multiply results by 2.
      conj_values[matrix_reps_index_map_[i + slots_]] = std::conj(values[i]);
    }
    double fix = scale / static_cast<double>(n);
    fft_handler_.transform_from_rev(conj_values.get(),
                                    seal::util::get_power_of_two(n),
                                    inv_root_powers_.get(), &fix);

    double max_coeff = 0;
    for (std::size_t i = 0; i < n; i++) {
      max_coeff = std::max<>(max_coeff, std::fabs(conj_values[i].real()));
    }
    // Verify that the values are not too large to fit in coeff_modulus
    // Note that we have an extra + 1 for the sign bit
    // Don't compute logarithmis of numbers less than 1
    int max_coeff_bit_count =
        static_cast<int>(std::ceil(std::log2(std::max<>(max_coeff, 1.0)))) + 1;
    if (max_coeff_bit_count >= context_.rns_base_bit_count(num_limbs)) {
      throw std::invalid_argument("encoded values are too large");
    }

    double two_pow_64 = std::pow(2.0, 64);

    std::vector<seal::Modulus> rns_modulii;
    rns_modulii.reserve(num_limbs);
    for (std::size_t i = 0; i < num_limbs; i++) {
      rns_modulii.push_back(context_.get_rns_modulus(rns_base_ids.at(i)));
    }

    // auto destination_data =
    // seal::util::allocate<seal::util::Pointer<Limb::Element_t>>(num_limbs,
    // pool_); auto destination_data =
    // Util::allocate<seal::util::Pointer<Limb::Element_t>>(num_limbs);
    auto destination_data =
        Util::allocate<Util::Pointer<Limb::Element_t>>(num_limbs);
    std::for_each_n(seal::util::iter(destination_data.get()), num_limbs,
                    [&](auto &I) {
                      // I = Util::allocate_uint(coeff_count, pool_);
                      I = Util::allocate_uint2(coeff_count);
                    });

    // Use faster decomposition methods when possible
    if (max_coeff_bit_count <= 64) {
      for (std::size_t i = 0; i < n; i++) {
        double coeffd = coordinate_wise_random_rounding(conj_values[i].real());
        // double coeffd = std::round(conj_values[i].real());
        bool is_negative = std::signbit(coeffd);

        std::uint64_t coeffu = static_cast<std::uint64_t>(std::fabs(coeffd));

        if (is_negative) {
          for (std::size_t j = 0; j < num_limbs; j++) {
            auto temp = seal::util::negate_uint_mod(
                seal::util::barrett_reduce_64(coeffu, rns_modulii.at(j)),
                rns_modulii.at(j));
            destination_data[j][i] = Util::safe_cast<Limb::Element_t>(temp);
          }
        } else {
          for (std::size_t j = 0; j < num_limbs; j++) {
            auto temp = seal::util::barrett_reduce_64(coeffu, rns_modulii[j]);
            destination_data[j][i] = Util::safe_cast<Limb::Element_t>(temp);
          }
        }
      }
    } else if (max_coeff_bit_count <= 128) {
      for (std::size_t i = 0; i < n; i++) {
        double coeffd = std::round(conj_values[i].real());
        bool is_negative = std::signbit(coeffd);
        coeffd = std::fabs(coeffd);

        std::uint64_t coeffu[2]{
            static_cast<std::uint64_t>(std::fmod(coeffd, two_pow_64)),
            static_cast<std::uint64_t>(coeffd / two_pow_64)};

        if (is_negative) {
          for (std::size_t j = 0; j < num_limbs; j++) {
            // destination[i + (j * coeff_count)] = util::negate_uint_mod(
            //     util::barrett_reduce_128(coeffu, coeff_modulus[j]),
            //     coeff_modulus[j]);
            auto temp = seal::util::negate_uint_mod(
                seal::util::barrett_reduce_128(coeffu, rns_modulii.at(j)),
                rns_modulii.at(j));
            destination_data[j][i] = Util::safe_cast<Limb::Element_t>(temp);
          }
        } else {
          for (std::size_t j = 0; j < num_limbs; j++) {
            // destination[i + (j * coeff_count)] =
            // util::barrett_reduce_128(coeffu, coeff_modulus[j]);
            auto temp =
                seal::util::barrett_reduce_128(coeffu, rns_modulii.at(j));
            destination_data[j][i] = Util::safe_cast<Limb::Element_t>(temp);
          }
        }
      }
    } else {
      throw std::logic_error("unimplemented");
#if 0
                // TODO: Fit this
                // Slow case
                auto coeffu(seal::util::allocate_uint(num_limbs, pool));
                for (std::size_t i = 0; i < n; i++)
                {
                    double coeffd = std::round(conj_values[i].real());
                    bool is_negative = std::signbit(coeffd);
                    coeffd = std::fabs(coeffd);

                    // We are at this point guaranteed to fit in the allocated space
                    seal::util::set_zero_uint(num_limbs, coeffu.get());
                    auto coeffu_ptr = coeffu.get();
                    while (coeffd >= 1)
                    {
                        *coeffu_ptr++ = static_cast<std::uint64_t>(std::fmod(coeffd, two_pow_64));
                        coeffd /= two_pow_64;
                    }

                    // Next decompose this coefficient
                    context_data.rns_tool()->base_q()->decompose(coeffu.get(), pool);

                    // Finally replace the sign if necessary
                    if (is_negative)
                    {
                        for (std::size_t j = 0; j < coeff_modulus_size; j++)
                        {
                            destination[i + (j * coeff_count)] = util::negate_uint_mod(coeffu[j], coeff_modulus[j]);
                        }
                    }
                    else
                    {
                        for (std::size_t j = 0; j < coeff_modulus_size; j++)
                        {
                            destination[i + (j * coeff_count)] = coeffu[j];
                        }
                    }
                }
#endif
    }

    auto destination = std::make_shared<RnsPolynomial>();
    // Transform to NTT domain
    for (std::size_t i = 0; i < num_limbs; i++) {
      Util::ntt_negacyclic_harvey(destination_data[i].get(),
                                  ntt_tables[rns_base_ids.at(i)]);
      auto dest = std::make_shared<Limb>(std::move(destination_data[i]),
                                         coeff_count, rns_base_ids.at(i), true);
      // destination.push_back(std::move(dest));
      destination->write_limb(std::move(dest), rns_base_ids.at(i));
    }

    return destination;
  }

  template <typename T,
            typename = std::enable_if_t<
                std::is_same<std::remove_cv_t<T>, double>::value ||
                std::is_same<std::remove_cv_t<T>, std::complex<double>>::value>>
  void decode_internal(const std::shared_ptr<RnsPolynomial> &input,
                       const double scale, T *destination,
                       seal::MemoryPoolHandle pool) {
    // Verify parameters.
    if (input->empty()) {
      throw std::invalid_argument("input can't be empty");
    }
    for (const auto &[rns_id, limb] : *input) {
      if (!limb->is_ntt_form()) {
        throw std::invalid_argument("input is not in NTT form");
      }
    }
    if (!destination) {
      throw std::invalid_argument("destination cannot be null");
    }
    if (!pool) {
      throw std::invalid_argument("pool is uninitialized");
    }

    // auto &context_data = *context_.get_context_data(plain.parms_id());
    // auto &parms = context_data.parms();
    std::size_t num_limbs = input->num_limbs();
    std::vector<std::uint64_t> rns_base_ids;
    std::vector<seal::Modulus> rns_modulii;
    rns_base_ids.reserve(num_limbs);
    rns_modulii.reserve(num_limbs);
    for (auto &[rns_base_id, limb] : *input) {
      rns_base_ids.push_back(rns_base_id);
      rns_modulii.push_back(context_.get_rns_modulus(rns_base_id));
    }

    std::size_t coeff_count = context_.n();
    std::size_t rns_poly_uint64_count =
        seal::util::mul_safe(coeff_count, num_limbs);

    auto ntt_tables = context_.seal_ntt_tables();

    // Check that scale is positive and not too large
    if (scale <= 0 || (static_cast<int>(log2(scale)) >=
                       context_.rns_base_bit_count(num_limbs))) {
      throw std::invalid_argument("scale out of bounds");
    }

    seal::util::RNSBase rns_base(rns_modulii, pool);
    auto rns_base_prod = rns_base.base_prod();

    // upper_half_threshold is (rns_base_prod + 1)//2
    auto upper_half_threshold = seal::util::allocate_uint(num_limbs, pool);
    seal::util::increment_uint(rns_base_prod, num_limbs,
                               upper_half_threshold.get());
    seal::util::right_shift_uint(upper_half_threshold.get(), 1, num_limbs,
                                 upper_half_threshold.get());
    // auto upper_half_threshold = context_data.upper_half_threshold();
    int logn = seal::util::get_power_of_two(coeff_count);

    // Quick sanity check
    // if ((logn < 0) || (coeff_count < 2*MIN_SLOTS) || (coeff_count >
    // 2*MAX_SLOTS))
    // {
    //     throw std::logic_error("invalid parameters");
    // }

    double inv_scale = double(1.0) / scale;

    // Create mutable copy of input
    auto plain_copy(seal::util::allocate_uint(rns_poly_uint64_count, pool));

    // Transform each polynomial from NTT domain
    for (std::size_t i = 0; i < num_limbs; i++) {
      for (size_t j = 0; j < coeff_count; j++) {
        plain_copy.get()[i * coeff_count + j] = Util::safe_cast<std::uint64_t>(
            input->at(rns_base_ids.at(i))->data()[j]);
      }
      seal::util::inverse_ntt_negacyclic_harvey(
          plain_copy.get() + (i * coeff_count), ntt_tables[rns_base_ids.at(i)]);
    }

    // CRT-compose the polynomial
    rns_base.compose_array(plain_copy.get(), coeff_count, pool);

    // Create floating-point representations of the multi-precision integer
    // coefficients
    double two_pow_64 = std::pow(2.0, 64);
    // auto res = seal::util::allocate<std::complex<double>>(coeff_count, pool);
    auto res = Util::allocate<std::complex<double>>(coeff_count);
    for (std::size_t i = 0; i < coeff_count; i++) {
      res[i] = 0.0;
      if (seal::util::is_greater_than_or_equal_uint(
              plain_copy.get() + (i * num_limbs), upper_half_threshold.get(),
              num_limbs)) {
        double scaled_two_pow_64 = inv_scale;
        for (std::size_t j = 0; j < num_limbs;
             j++, scaled_two_pow_64 *= two_pow_64) {
          if (plain_copy[i * num_limbs + j] > rns_base_prod[j]) {
            auto diff = plain_copy[i * num_limbs + j] - rns_base_prod[j];
            res[i] +=
                diff ? static_cast<double>(diff) * scaled_two_pow_64 : 0.0;
          } else {
            auto diff = rns_base_prod[j] - plain_copy[i * num_limbs + j];
            res[i] -=
                diff ? static_cast<double>(diff) * scaled_two_pow_64 : 0.0;
          }
        }
      } else {
        double scaled_two_pow_64 = inv_scale;
        for (std::size_t j = 0; j < num_limbs;
             j++, scaled_two_pow_64 *= two_pow_64) {
          auto curr_coeff = plain_copy[i * num_limbs + j];
          res[i] += curr_coeff
                        ? static_cast<double>(curr_coeff) * scaled_two_pow_64
                        : 0.0;
        }
      }

      // Scaling instead incorporated above; this can help in cases
      // where otherwise pow(two_pow_64, j) would overflow due to very
      // large coeff_modulus_size and very large scale
      // res[i] = res_accum * inv_scale;
    }

    fft_handler_.transform_to_rev(res.get(), logn, root_powers_.get());

    for (std::size_t i = 0; i < slots_; i++) {
      destination[i] = seal::from_complex<T>(
          res[static_cast<std::size_t>(matrix_reps_index_map_[i])]);
    }
  }

  template <typename T,
            typename = std::enable_if_t<
                std::is_same<std::remove_cv_t<T>, double>::value ||
                std::is_same<std::remove_cv_t<T>, std::complex<double>>::value>>
  std::vector<double> encode_as_polynomial_internal(

      const T *values, std::size_t values_size, const double scale,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {
    // Verify parameters.
    if (!values && values_size > 0) {
      throw std::invalid_argument("values cannot be null");
    }
    if (values_size != context_.slots()) {
      throw std::invalid_argument("values_size must be equal to context slots");
    }
    if (!pool) {
      throw std::invalid_argument("pool is uninitialized");
    }

    std::size_t coeff_count = context_.n();

    auto ntt_tables = context_.ntt_tables();

    auto slots_ = context_.slots();
    auto n = context_.n();

    // auto conj_values = seal::util::allocate<std::complex<double>>(n, pool,
    // 0);
    auto conj_values = Util::allocate<std::complex<double>>(n);
    std::fill_n(conj_values.get(), n, 0);
    for (std::size_t i = 0; i < values_size; i++) {
      conj_values[matrix_reps_index_map_[i]] = values[i];
      // TODO: if values are real, the following values should be set to zero,
      // and multiply results by 2.
      conj_values[matrix_reps_index_map_[i + slots_]] = std::conj(values[i]);
    }
    double fix = scale / static_cast<double>(n);
    fft_handler_.transform_from_rev(conj_values.get(),
                                    seal::util::get_power_of_two(n),
                                    inv_root_powers_.get(), &fix);

    std::vector<double> destination;
    destination.resize(coeff_count);

    for (size_t i = 0; i < coeff_count; i++) {
      destination[i] = std::round(conj_values[i].real());
    }

    return std::move(destination);
  }
};
} // namespace Cinnamon::Emulator
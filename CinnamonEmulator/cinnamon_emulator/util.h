// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <seal/seal.h>
#include <seal/util/rns.h>

#include "config.h"
#include "limb.h"
#include "pointer.h"

#define CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)                                 \
  template <typename T,                                                        \
            typename = std::enable_if_t<                                       \
                std::is_same<std::remove_cv_t<T>, std::uint64_t>::value ||     \
                std::is_same<std::remove_cv_t<T>, std::uint32_t>::value>>

namespace Cinnamon::Emulator {

/** Wrapper Class around seal::util for Cinnamon::Emulator.
 * Performs appropriate typecasting
 */
namespace Util {

template <typename T, typename S,
          typename = std::enable_if_t<std::is_arithmetic<T>::value>,
          typename = std::enable_if_t<std::is_arithmetic<S>::value>>
inline T safe_cast(S value) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  return seal::util::safe_cast<T>(value);
#else
  return static_cast<T>(value);
#endif
}

template <
    typename T_,
    typename = std::enable_if_t<std::is_standard_layout<typename std::remove_cv<
        typename std::remove_reference<T_>::type>::type>::value>>
inline auto allocate(const size_t count) {

  using T =
      typename std::remove_cv<typename std::remove_reference<T_>::type>::type;
  return Util::Pointer<T>(count);
}

inline auto allocate_uint2(const size_t count) {
  return allocate<Limb::Element_t>(count);
}

/**
Returns ((operand1*operand2) + operand3) mod modulus
*/
CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
inline T multiply_add_uint_mod(const T &operand1, const T &operand2,
                               const T &operand3,
                               const seal::Modulus &modulus) {
  if constexpr (std::is_same<T, std::uint64_t>::value) {
    return seal::util::multiply_add_uint_mod(operand1, operand2, operand3,
                                             modulus);
  } else {
    std::uint64_t operand1_64 = static_cast<uint64_t>(operand1);
    std::uint64_t operand2_64 = static_cast<uint64_t>(operand2);
    std::uint64_t operand3_64 = static_cast<uint64_t>(operand3);
    auto result = seal::util::multiply_add_uint_mod(operand1_64, operand2_64,
                                                    operand3_64, modulus);
    return Util::safe_cast<T>(result);
  }
}

/**
Returns (operand1+operand2) mod modulus
*/
CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
inline T add_uint_mod(T operand1, T operand2, const seal::Modulus &modulus) {
  operand1 += operand2;
  auto modulus_value = Util::safe_cast<T>(modulus.value());
  return operand1 >= modulus_value ? operand1 - modulus_value : operand1;
}

/**
Returns (operand1-operand2) mod modulus
*/
CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
inline T subtract_uint_mod(const T &operand1, const T &operand2,
                           const seal::Modulus &modulus) {
  if constexpr (std::is_same<T, std::uint64_t>::value) {
    return seal::util::sub_uint_mod(operand1, operand2, modulus);
  } else {
    std::uint64_t operand1_64 = static_cast<uint64_t>(operand1);
    std::uint64_t operand2_64 = static_cast<uint64_t>(operand2);
    auto result = seal::util::sub_uint_mod(operand1_64, operand2_64, modulus);
    return Util::safe_cast<T>(result);
  }
}

/**
Returns (operand1*operand2) mod modulus
*/
CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
inline T multiply_uint_mod(T operand1, T operand2,
                           const seal::Modulus &modulus) {
  if constexpr (std::is_same<T, std::uint64_t>::value) {
    return seal::util::multiply_uint_mod(operand1, operand2, modulus);
  } else if constexpr (std::is_same<T, std::uint32_t>::value) {
    std::uint64_t operand1_64 = static_cast<uint64_t>(operand1);
    std::uint64_t operand2_64 = static_cast<uint64_t>(operand2);
    std::uint64_t product = operand1_64 * operand2_64;
    auto result = seal::util::barrett_reduce_64(product, modulus);
    // auto result1 =
    //     seal::util::multiply_uint_mod(operand1_64, operand2_64, modulus);
    return Util::safe_cast<T>(result);
  }
}

/**
Returns (-1*operand1) mod modulus
*/
CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
inline T negate_uint_mod(T operand, const seal::Modulus &modulus) {
  if constexpr (std::is_same<T, std::uint64_t>::value) {
    return seal::util::negate_uint_mod(operand, modulus);
  } else if constexpr (std::is_same<T, std::uint32_t>::value) {
    auto modulus32 = Util::safe_cast<std::uint32_t>(modulus.value());
    std::int32_t non_zero = static_cast<std::int32_t>(operand != 0);
    return (modulus32 - operand) & static_cast<std::uint32_t>(-non_zero);
  }
}

CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
inline auto set_uint(const T *value, const size_t count, T *result) {
  if (result == value) {
    return;
  }
  std::memcpy(result, value, count * sizeof(T));
}

} // namespace Util
} // namespace Cinnamon::Emulator
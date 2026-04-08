// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <seal/memorymanager.h>
#include <seal/seal.h>
#include <seal/util/mempool.h>
#include <seal/util/rns.h>
#include <seal/util/uintcore.h>

#include "ckks-encoder.h"
#include "context.h"
#include "limb.h"
#include "rns_polynomial.h"
#include "util.h"

namespace Cinnamon::Emulator {
class CKKSEncryptor {
  using LimbVector = std::vector<std::shared_ptr<Limb>>;

public:
  CKKSEncryptor(const Context &context,
                const std::vector<std::int64_t> &secret_key,
                const std::vector<std::int64_t> &ephemeral_key);
  // CKKSEncryptor(const Context &context, const std::vector<std::int64_t>
  // &secret_key, const seal::prng_seed_type &prng_seed);
  CKKSEncryptor(const Context &context,
                const std::vector<std::int64_t> &secret_key,
                const std::vector<std::int64_t> &ephemeral_key,
                const seal::prng_seed_type &prng_seed);

  CKKSEncryptor() = delete;
  CKKSEncryptor(const CKKSEncryptor &) = delete;
  CKKSEncryptor(CKKSEncryptor &&) = default;
  CKKSEncryptor &operator=(CKKSEncryptor &&) = delete;

  template <typename T,
            typename = std::enable_if_t<
                std::is_same<std::remove_cv_t<T>, double>::value ||
                std::is_same<std::remove_cv_t<T>, std::complex<double>>::value>>
  inline std::pair<std::shared_ptr<RnsPolynomial>,
                   std::shared_ptr<RnsPolynomial>>
  encode_and_encrypt(
      const std::vector<T> &values, const double scale,
      const std::vector<std::uint64_t> &rns_base_ids,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {

    auto encoded_value = encoder_.encode(values, scale, rns_base_ids, pool);
    return encrypt_internal(encoded_value, rns_base_ids, pool);
  }

  inline std::pair<std::shared_ptr<RnsPolynomial>,
                   std::shared_ptr<RnsPolynomial>>
  encode_and_encrypt(
      const double &value, const double scale,
      const std::vector<std::uint64_t> &rns_base_ids,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {

    auto encoded_value = encoder_.encode(value, scale, rns_base_ids, pool);
    return encrypt_internal(encoded_value, rns_base_ids, pool);
  }

  template <typename T,
            typename = std::enable_if_t<
                std::is_same<std::remove_cv_t<T>, double>::value ||
                std::is_same<std::remove_cv_t<T>, std::complex<double>>::value>>
  inline std::vector<T> decrypt_and_decode(
      const std::pair<RnsPolynomialPtr, RnsPolynomialPtr> &ciphertext,
      const std::vector<std::uint64_t> &rns_base_ids, const double scale,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool()) {
    auto decrypt = decrypt_internal(ciphertext, rns_base_ids, pool);
    return encoder_.decode<T>(decrypt, scale, std::move(pool));
  }

  std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
  encrypt_zero(const LimbVector &key_rns,
               const std::vector<std::uint64_t> &rns_base_ids,
               seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
  encrypt_zero(const std::vector<std::uint64_t> &rns_base_ids,
               seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool(),
               bool use_ephemeral_key = false);

  std::pair<RnsPolynomialPtr, RnsPolynomialPtr> generate_relin_evalkey(
      const std::uint64_t level, const std::uint64_t extension_size,
      const std::vector<std::uint64_t> &digit_partition,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  std::pair<RnsPolynomialPtr, RnsPolynomialPtr> generate_rotation_evalkey(
      const std::uint32_t rotation_amout, const std::uint64_t level,
      const std::uint64_t extension_size,
      const std::vector<std::uint64_t> &digit_partition,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  std::pair<RnsPolynomialPtr, RnsPolynomialPtr> generate_rotation_inv_evalkey(
      const std::uint32_t rotation_amout, const std::uint64_t level,
      const std::uint64_t extension_size,
      const std::vector<std::uint64_t> &digit_partition,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  std::pair<RnsPolynomialPtr, RnsPolynomialPtr> generate_conjugation_evalkey(
      const std::uint64_t level, const std::uint64_t extension_size,
      const std::vector<std::uint64_t> &digit_partition,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  std::pair<RnsPolynomialPtr, RnsPolynomialPtr> generate_bootstrap_evalkey(
      const std::uint64_t level, const std::uint64_t extension_size,
      const std::vector<std::uint64_t> &digit_partition,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  std::pair<RnsPolynomialPtr, RnsPolynomialPtr> generate_ephemeral_evalkey(
      const std::uint64_t level, const std::uint64_t extension_size,
      const std::vector<std::uint64_t> &digit_partition,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  std::pair<RnsPolynomialPtr, RnsPolynomialPtr> generate_bootstrap_evalkey2(
      const std::uint64_t level, const std::uint64_t extension_size,
      const std::vector<std::uint64_t> &digit_partition,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  // std::pair<LimbVector, LimbVector> generate_conjugation_evalkey(const
  // std::uint32_t rotation_amout, const std::uint64_t level, const
  // std::uint64_t extension_size, const std::vector<std::uint64_t>
  // &digit_partition, seal::MemoryPoolHandle pool =
  // seal::MemoryManager::GetPool()); std::pair<LimbVector, LimbVector>
  // generate_bootstrap_evalkey(const std::uint32_t rotation_amout, const
  // std::uint64_t level, const std::uint64_t extension_size, const
  // std::vector<std::uint64_t> &digit_partition, seal::MemoryPoolHandle pool =
  // seal::MemoryManager::GetPool());

private:
  const Context &context_;
  seal::MemoryPoolHandle pool_;
  CKKSEncoder encoder_;
  const std::vector<std::int64_t> secret_key_;
  const std::vector<std::int64_t> ephemeral_key_;
  std::vector<std::shared_ptr<Limb>> secret_key_rns_;
  std::vector<std::shared_ptr<Limb>> secret_key_squared_rns_;
  std::vector<std::shared_ptr<Limb>> ephemeral_key_rns_;
  bool use_random_seed_;
  seal::prng_seed_type prng_seed_;
  std::shared_ptr<seal::UniformRandomGenerator> prng_;
  bool save_ciphertext_random_seed_ = false;

  void compute_secret_key_rns();
  void compute_ephemeral_key_rns();
  RnsPolynomialPtr
  sample_poly_uniform(std::shared_ptr<seal::UniformRandomGenerator> &prng,
                      const std::vector<std::uint64_t> &rns_base_ids);
  RnsPolynomialPtr
  sample_poly_cbd(std::shared_ptr<seal::UniformRandomGenerator> &prng,
                  const std::vector<std::uint64_t> &rns_base_ids);

  std::pair<std::shared_ptr<RnsPolynomial>, std::shared_ptr<RnsPolynomial>>
  encrypt_internal(
      const std::shared_ptr<RnsPolynomial> &message,
      const std::vector<std::uint64_t> &rns_base_ids,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  RnsPolynomialPtr decrypt_internal(
      const std::pair<RnsPolynomialPtr, RnsPolynomialPtr> &ciphertext,
      const std::vector<std::uint64_t> &rns_base_ids,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());

  std::pair<RnsPolynomialPtr, RnsPolynomialPtr> generate_evalkey(
      const LimbVector &old_key, const LimbVector &new_key,
      const std::uint64_t level, const std::uint64_t extension_size,
      const std::vector<std::uint64_t> &digit_partition,
      seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool());
  std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
  generate_evalkey(const LimbVector &new_key, const std::uint64_t level,
                   const std::uint64_t extension_size,
                   const std::vector<std::uint64_t> &digit_partition,
                   seal::MemoryPoolHandle pool = seal::MemoryManager::GetPool(),
                   bool use_ephemeral_key = false);
};
} // namespace Cinnamon::Emulator
// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include "ckks-encryptor.h"
#include <seal/util/globals.h>
#include <seal/util/polyarithsmallmod.h>
#include <seal/util/uintarithmod.h>

namespace Cinnamon::Emulator {

RnsPolynomialPtr CKKSEncryptor::sample_poly_uniform(
    std::shared_ptr<seal::UniformRandomGenerator> &prng,
    const std::vector<std::uint64_t> &rns_base_ids) {
  auto coeff_count = context_.n();
  auto num_limbs = rns_base_ids.size();
  auto coeff_byte_count =
      seal::util::mul_safe(coeff_count, sizeof(Limb::Element_t));

  auto result = std::make_shared<RnsPolynomial>();
  // result.reserve(num_limbs);

  constexpr auto max_random = ([&]() {
    if constexpr (std::is_same<Limb::Element_t, std::uint32_t>::value) {
      return static_cast<uint32_t>(0xFFFFFFFFUL);
    } else if constexpr (std::is_same<Limb::Element_t, std::uint64_t>::value) {
      return static_cast<uint64_t>(0xFFFFFFFFFFFFFFFFULL);
    } else {
      throw std::logic_error("Invalid Type");
    }
  })();

  // Fill the destination buffer with fresh randomness
  for (size_t i = 0; i < num_limbs; i++) {

    auto &modulus = context_.get_rns_modulus(rns_base_ids[i]);
    // auto destination = seal::util::allocate<Limb::Element_t>(coeff_count,
    // pool_);
    auto destination = Util::allocate<Limb::Element_t>(coeff_count);
    prng->generate(coeff_byte_count,
                   reinterpret_cast<seal::seal_byte *>(destination.get()));

    auto max_multiple =
        max_random - Util::safe_cast<Limb::Element_t>(
                         seal::util::barrett_reduce_64(
                             static_cast<uint64_t>(max_random), modulus) -
                         1);
    std::transform(destination.get(), destination.get() + coeff_count,
                   destination.get(), [&](Limb::Element_t rand) {
                     // This ensures uniform distribution
                     while (rand >= max_multiple) {
                       prng->generate(
                           sizeof(Limb::Element_t),
                           reinterpret_cast<seal::seal_byte *>(&rand));
                     }
                     uint64_t rand64 = rand;
                     return Util::safe_cast<Limb::Element_t>(
                         seal::util::barrett_reduce_64(rand64, modulus));
                   });

    auto limb = std::make_shared<Limb>(std::move(destination), coeff_count,
                                       rns_base_ids.at(i), true);
    result->write_limb(std::move(limb), rns_base_ids.at(i));
  }

  return std::move(result);
}

RnsPolynomialPtr CKKSEncryptor::sample_poly_cbd(
    std::shared_ptr<seal::UniformRandomGenerator> &prng,
    const std::vector<std::uint64_t> &rns_base_ids) {
  auto coeff_count = context_.n();
  auto num_limbs = rns_base_ids.size();
  auto ntt_tables = context_.ntt_tables();

  auto result = std::make_shared<RnsPolynomial>();
  // result.reserve(num_limbs);

  if (seal::util::are_close(seal::util::global_variables::noise_max_deviation,
                            0.0)) {

    for (size_t i = 0; i < num_limbs; i++) {

      auto modulus = context_.get_rns_modulus(rns_base_ids.at(i));
      // auto destination = Util::allocate_uint(coeff_count, pool_);
      auto destination = Util::allocate_uint2(coeff_count);
      for (size_t j = 0; j < coeff_count; j++) {
        destination[j] = 0;
      }
      auto limb = std::make_shared<Limb>(std::move(destination), coeff_count,
                                         rns_base_ids.at(i), true);
      result->write_limb(std::move(limb), rns_base_ids.at(i));
    }

    return std::move(result);
  }

  if (!seal::util::are_close(
          seal::util::global_variables::noise_standard_deviation, 3.2)) {
    throw std::logic_error("centered binomial distribution only supports "
                           "standard deviation 3.2; use rounded "
                           "Gaussian instead");
  }

  auto cbd = [&]() {
    unsigned char x[6];
    prng->generate(6, reinterpret_cast<seal::seal_byte *>(x));
    x[2] &= 0x1F;
    x[5] &= 0x1F;
    return seal::util::hamming_weight(x[0]) + seal::util::hamming_weight(x[1]) +
           seal::util::hamming_weight(x[2]) - seal::util::hamming_weight(x[3]) -
           seal::util::hamming_weight(x[4]) - seal::util::hamming_weight(x[5]);
  };

  // std::vector<seal::util::Pointer<Limb::Element_t>> buffer;
  std::vector<Util::Pointer<Limb::Element_t>> buffer;
  buffer.resize(num_limbs);
  for (size_t i = 0; i < num_limbs; i++) {
    // buffer[i] = Util::allocate_uint(coeff_count, pool_);
    buffer[i] = Util::allocate_uint2(coeff_count);
  }

  for (size_t i = 0; i < coeff_count; i++) {
    int32_t noise = cbd();
    Limb::Element_t flag =
        static_cast<Limb::Element_t>(-static_cast<Limb::Element_t>(noise < 0));
    for (size_t j = 0; j < num_limbs; j++) {
      auto modulus = Util::safe_cast<Limb::Element_t>(
          context_.get_rns_modulus(rns_base_ids.at(j)).value());
      buffer[j][i] = static_cast<Limb::Element_t>(noise) + (flag & modulus);
    }
  }

  for (size_t i = 0; i < num_limbs; i++) {
    // auto modulus = context_.get_rns_modulus(rns_base_ids.at(i)).value();
    // buffer[i] = seal::util::allocate_zero_uint(coeff_count,pool_);
    Util::ntt_negacyclic_harvey(buffer[i].get(),
                                ntt_tables[rns_base_ids.at(i)]);
    auto limb = std::make_shared<Limb>(std::move(buffer.at(i)), coeff_count,
                                       rns_base_ids.at(i), true);
    result->write_limb(std::move(limb), rns_base_ids.at(i));
  }

  return std::move(result);

  // SEAL_ITERATE(iter(destination), coeff_count, [&](auto &I) {
  //     int32_t noise = cbd();
  //     uint64_t flag = static_cast<uint64_t>(-static_cast<int64_t>(noise <
  //     0)); SEAL_ITERATE(
  //         iter(StrideIter<uint64_t *>(&I, coeff_count), coeff_modulus),
  //         coeff_modulus_size,
  //         [&](auto J) { *get<0>(J) = static_cast<uint64_t>(noise) + (flag &
  //         get<1>(J).value()); });
  // });
}

CKKSEncryptor::CKKSEncryptor(const Context &context,
                             const std::vector<std::int64_t> &secret_key,
                             const std::vector<std::int64_t> &ephemeral_key)
    : context_(context), encoder_(context_), secret_key_(secret_key),
      ephemeral_key_(ephemeral_key), use_random_seed_(true),
      pool_(seal::MemoryManager::GetPool()) {
  if (secret_key_.size() != context_.n()) {
    throw std::invalid_argument("secret key too small");
  }
  prng_ = std::move(
      seal::UniformRandomGeneratorFactory::DefaultFactory()->create());
  compute_secret_key_rns();
  compute_ephemeral_key_rns();
}

CKKSEncryptor::CKKSEncryptor(const Context &context,
                             const std::vector<std::int64_t> &secret_key,
                             const std::vector<std::int64_t> &ephemeral_key,
                             const seal::prng_seed_type &prng_seed)
    : context_(context), encoder_(context_, prng_seed), secret_key_(secret_key),
      ephemeral_key_(ephemeral_key), use_random_seed_(false),
      prng_seed_(std::move(prng_seed)), pool_(seal::MemoryManager::GetPool()) {
  if (secret_key_.size() != context_.n()) {
    throw std::invalid_argument("secret key too small");
  }
  prng_ =
      std::move(seal::UniformRandomGeneratorFactory::DefaultFactory()->create(
          prng_seed_));
  compute_secret_key_rns();
  compute_ephemeral_key_rns();
}

void CKKSEncryptor::compute_secret_key_rns() {
  auto &rns_modulii = context_.rns_bases();
  secret_key_rns_.reserve(rns_modulii.size());
  auto coeff_count = context_.n();
  auto ntt_tables = context_.ntt_tables();
  for (size_t i = 0; i < rns_modulii.size(); i++) {
    // auto buffer = Util::allocate_uint(coeff_count, pool_);
    auto buffer = Util::allocate_uint2(coeff_count);
    // auto modulus = *rns_modulii.at(i).data();
    // std::cout << "bufferj: ";
    for (int j = 0; j < coeff_count; j++) {
      std::int64_t value = secret_key_.at(j);
      std::uint64_t valueu = seal::util::safe_cast<uint64_t>(std::abs(value));
      if (value < 0) {
        buffer[j] =
            Util::safe_cast<Limb::Element_t>(seal::util::negate_uint_mod(
                seal::util::barrett_reduce_64(valueu, rns_modulii.at(i)),
                rns_modulii.at(i)));
      } else {
        buffer[j] = Util::safe_cast<Limb::Element_t>(
            seal::util::barrett_reduce_64(valueu, rns_modulii.at(i)));
      }
    }
    Util::ntt_negacyclic_harvey(buffer.get(), ntt_tables[i]);
    // auto buffer_square = Util::allocate_uint(coeff_count, pool_);
    auto buffer_square = Util::allocate_uint2(coeff_count);
    for (int j = 0; j < coeff_count; j++) {
      // std::cout << buffer[j] << ",";
      buffer_square[j] =
          Util::multiply_uint_mod(buffer[j], buffer[j], rns_modulii.at(i));
    }
    // std::cout << "\n\n\n";
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count, i, true);
    secret_key_rns_.push_back(std::move(limb));
    auto limb_square =
        std::make_shared<Limb>(std::move(buffer_square), coeff_count, i, true);
    secret_key_squared_rns_.push_back(std::move(limb_square));
  }
}

void CKKSEncryptor::compute_ephemeral_key_rns() {
  auto &rns_modulii = context_.rns_bases();
  ephemeral_key_rns_.reserve(rns_modulii.size());
  auto coeff_count = context_.n();
  auto ntt_tables = context_.ntt_tables();
  for (size_t i = 0; i < rns_modulii.size(); i++) {
    // auto buffer = Util::allocate_uint(coeff_count, pool_);
    auto buffer = Util::allocate_uint2(coeff_count);
    for (int j = 0; j < coeff_count; j++) {
      std::int64_t value = ephemeral_key_.at(j);
      std::uint64_t valueu = seal::util::safe_cast<uint64_t>(std::abs(value));
      if (value < 0) {
        buffer[j] =
            Util::safe_cast<Limb::Element_t>(seal::util::negate_uint_mod(
                seal::util::barrett_reduce_64(valueu, rns_modulii.at(i)),
                rns_modulii.at(i)));
      } else {
        buffer[j] = Util::safe_cast<Limb::Element_t>(
            seal::util::barrett_reduce_64(valueu, rns_modulii.at(i)));
      }
    }
    Util::ntt_negacyclic_harvey(buffer.get(), ntt_tables[i]);
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count, i, true);
    ephemeral_key_rns_.push_back(std::move(limb));
  }
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::encrypt_zero(const std::vector<std::uint64_t> &rns_base_ids,
                            seal::MemoryPoolHandle pool,
                            bool use_ephemeral_key) {
  if (use_ephemeral_key) {
    return encrypt_zero(ephemeral_key_rns_, rns_base_ids, pool);
  }
  return encrypt_zero(secret_key_rns_, rns_base_ids, pool);

  size_t coeff_count = context_.n();
  auto num_limbs = rns_base_ids.size();

  // Sample a public seed for generating uniform randomness
  seal::prng_seed_type public_prng_seed;
  prng_->generate(seal::prng_seed_byte_count,
                  reinterpret_cast<seal::seal_byte *>(public_prng_seed.data()));

  // Set up a new default PRNG for expanding u from the seed sampled above
  auto ciphertext_prng =
      save_ciphertext_random_seed_
          ? seal::UniformRandomGeneratorFactory::DefaultFactory()->create(
                public_prng_seed)
          : prng_;
  // auto ciphertext_prng =
  // seal::UniformRandomGeneratorFactory::DefaultFactory()->create(public_prng_seed);

  // Sample e <-- chi
  RnsPolynomialPtr noise = sample_poly_cbd(prng_, rns_base_ids);

  // Generate ciphertext: (c[0], c[1]) = ([-(as+e)]_q, a)

  // Sample c1 uniformly at random
  RnsPolynomialPtr c1 =
      std::move(sample_poly_uniform(ciphertext_prng, rns_base_ids));

  auto c0 = std::make_shared<RnsPolynomial>();
  for (size_t i = 0; i < num_limbs; i++) {
    auto rns_base_id = rns_base_ids.at(i);
    // auto buffer = Util::allocate_uint(coeff_count, pool_);
    auto buffer = Util::allocate_uint2(coeff_count);
    auto modulus = context_.get_rns_modulus(rns_base_ids.at(i));
    auto c1_data = c1->at(rns_base_id)->data();
    Limb::Element_t *secret_key_data = nullptr;
    if (use_ephemeral_key) {
      secret_key_data = ephemeral_key_rns_.at(rns_base_ids.at(i))->data();
    } else {
      secret_key_data = secret_key_rns_.at(rns_base_ids.at(i))->data();
    }
    auto noise_data = noise->at(rns_base_id)->data();
    for (size_t j = 0; j < coeff_count; j++) {
      buffer[j] =
          Util::multiply_uint_mod(c1_data[j], secret_key_data[j], modulus);
      buffer[j] = Util::negate_uint_mod(buffer[j], modulus);
      buffer[j] = Util::add_uint_mod(buffer[j], noise_data[j], modulus);
    }
    // std::cout << "\n";
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count,
                                       rns_base_id, true);
    c0->write_limb(std::move(limb), rns_base_id);
  }
  return std::make_pair(c0, c1);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::encrypt_zero(const LimbVector &key_rns,
                            const std::vector<std::uint64_t> &rns_base_ids,
                            seal::MemoryPoolHandle pool) {

  size_t coeff_count = context_.n();
  auto num_limbs = rns_base_ids.size();

  // Sample a public seed for generating uniform randomness
  seal::prng_seed_type public_prng_seed;
  prng_->generate(seal::prng_seed_byte_count,
                  reinterpret_cast<seal::seal_byte *>(public_prng_seed.data()));

  // Set up a new default PRNG for expanding u from the seed sampled above
  auto ciphertext_prng =
      save_ciphertext_random_seed_
          ? seal::UniformRandomGeneratorFactory::DefaultFactory()->create(
                public_prng_seed)
          : prng_;
  // auto ciphertext_prng =
  // seal::UniformRandomGeneratorFactory::DefaultFactory()->create(public_prng_seed);

  // Sample e <-- chi
  RnsPolynomialPtr noise = sample_poly_cbd(prng_, rns_base_ids);

  // Generate ciphertext: (c[0], c[1]) = ([-(as+e)]_q, a)

  // Sample c1 uniformly at random
  RnsPolynomialPtr c1 =
      std::move(sample_poly_uniform(ciphertext_prng, rns_base_ids));

  auto c0 = std::make_shared<RnsPolynomial>();
  for (size_t i = 0; i < num_limbs; i++) {
    auto rns_base_id = rns_base_ids.at(i);
    // auto buffer = Util::allocate_uint(coeff_count, pool_);
    auto buffer = Util::allocate_uint2(coeff_count);
    auto modulus = context_.get_rns_modulus(rns_base_ids.at(i));
    auto c1_data = c1->at(rns_base_id)->data();
    Limb::Element_t *secret_key_data = nullptr;
    secret_key_data = key_rns.at(rns_base_ids.at(i))->data();
    auto noise_data = noise->at(rns_base_id)->data();
    for (size_t j = 0; j < coeff_count; j++) {
      buffer[j] =
          Util::multiply_uint_mod(c1_data[j], secret_key_data[j], modulus);
      buffer[j] = Util::negate_uint_mod(buffer[j], modulus);
      buffer[j] = Util::add_uint_mod(buffer[j], noise_data[j], modulus);
    }
    // std::cout << "\n";
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count,
                                       rns_base_id, true);
    c0->write_limb(std::move(limb), rns_base_id);
  }
  return std::make_pair(c0, c1);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::encrypt_internal(const std::shared_ptr<RnsPolynomial> &message,
                                const std::vector<std::uint64_t> &rns_base_ids,
                                seal::MemoryPoolHandle pool) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (rns_base_ids.empty()) {
    throw std::invalid_argument("rns_base_ids can't be empty");
  }
  if (rns_base_ids.size() != message->size()) {
    throw std::invalid_argument("rns_base_ids don't match message limbs");
  }
#endif

  auto ct = encrypt_zero(rns_base_ids, pool);
  auto &ct1 = std::get<1>(ct);
  auto &ct0 = std::get<0>(ct);

  auto num_limbs = rns_base_ids.size();
  auto coeff_count = context_.n();

  for (size_t i = 0; i < num_limbs; i++) {
    auto rns_base_id = rns_base_ids[i];
    auto modulus = context_.get_rns_modulus(rns_base_id);
    auto ct0_data = ct0->at(rns_base_id)->data();
    auto message_data = message->at(rns_base_id)->data();
    for (size_t j = 0; j < coeff_count; j++) {
      ct0_data[j] = Util::add_uint_mod(ct0_data[j], message_data[j], modulus);
    }
  }

  return std::make_pair(ct0, ct1);
}

std::shared_ptr<RnsPolynomial> CKKSEncryptor::decrypt_internal(
    const std::pair<RnsPolynomialPtr, RnsPolynomialPtr> &ciphertext,
    const std::vector<std::uint64_t> &rns_base_ids,
    seal::MemoryPoolHandle pool) {

  auto &ct1 = std::get<1>(ciphertext);
  auto &ct0 = std::get<0>(ciphertext);

#ifdef CINNAMON_EUMLATOR_DEBUG
  if (rns_base_ids.empty()) {
    throw std::invalid_argument("rns_base_ids can't be empty");
  }
  if (rns_base_ids.size() != ct1->size()) {
    throw std::invalid_argument("rns_base_ids don't match ciphertext limbs");
  }
  if (rns_base_ids.size() != ct0->size()) {
    throw std::invalid_argument("rns_base_ids don't match ciphertext limbs");
  }
#endif

  auto num_limbs = rns_base_ids.size();
  auto coeff_count = context_.n();

  auto decrypted_message = std::make_shared<RnsPolynomial>();
  for (size_t i = 0; i < num_limbs; i++) {
    auto rns_base_id = rns_base_ids[i];
    auto modulus = context_.get_rns_modulus(rns_base_id);
    // auto buffer = Util::allocate_uint(coeff_count, pool_);
    auto buffer = Util::allocate_uint2(coeff_count);
    auto secret_key_data = secret_key_rns_.at(rns_base_id)->data();
    auto ct0_data = ct0->at(rns_base_id)->data();
    auto ct1_data = ct1->at(rns_base_id)->data();
    for (size_t j = 0; j < coeff_count; j++) {
      buffer[j] = Util::multiply_add_uint_mod(ct1_data[j], secret_key_data[j],
                                              ct0_data[j], modulus);
    }
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count,
                                       rns_base_ids.at(i), true);
    decrypted_message->write_limb(std::move(limb), rns_base_id);
  }

  return std::move(decrypted_message);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr> CKKSEncryptor::generate_evalkey(
    const CKKSEncryptor::LimbVector &new_key, const std::uint64_t level,
    const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool, bool use_ephemeral_key) {
  if (use_ephemeral_key) {
    return generate_evalkey(ephemeral_key_rns_, new_key, level, extension_size,
                            digit_partition, pool);
  }
  return generate_evalkey(secret_key_rns_, new_key, level, extension_size,
                          digit_partition, pool);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr> CKKSEncryptor::generate_evalkey(
    const CKKSEncryptor::LimbVector &old_key,
    const CKKSEncryptor::LimbVector &new_key, const std::uint64_t level,
    const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool) {
  std::vector<std::uint64_t> key_rns_base_ids;
  std::vector<seal::Modulus> Q_modulii;
  std::vector<seal::Modulus> P_modulii;
  std::vector<seal::Modulus> Q_partition_modulii;

  for (size_t i = 0; i < level; i++) {
    key_rns_base_ids.push_back(i);
    Q_modulii.push_back(context_.get_rns_modulus(i));
  }

  auto max_level = context_.num_rns_bases();

  for (size_t i = 0; i < extension_size; i++) {
    key_rns_base_ids.push_back(max_level - 1 - i);
    P_modulii.push_back(context_.get_rns_modulus(max_level - 1 - i));
  }

  for (size_t i = 0; i < digit_partition.size(); i++) {
    Q_partition_modulii.push_back(
        context_.get_rns_modulus(digit_partition.at(i)));
  }

  seal::util::RNSBase Q_base(Q_modulii, pool), P_base(P_modulii, pool),
      Q_partition_base(Q_partition_modulii, pool);

  // seal::util::Pointer<uint64_t> base_conv_factor;
  Util::Pointer<uint64_t> base_conv_factor;
  size_t base_conv_factor_size = 0;
  base_conv_factor_size = P_base.size();
  // base_conv_factor = seal::util::allocate_uint(base_conv_factor_size,pool);
  base_conv_factor = Util::allocate<uint64_t>(base_conv_factor_size);
  seal::util::set_uint(P_base.base_prod(), base_conv_factor_size,
                       base_conv_factor.get());

  auto coeff_count = context_.n();

  auto ct = encrypt_zero(old_key, key_rns_base_ids, pool);
  auto c0 = std::get<0>(ct);
  auto c1 = std::get<1>(ct);

  for (size_t i = 0; i < digit_partition.size(); i++) {
    auto rns_base_id = digit_partition.at(i);
#ifdef CINNAMON_EUMLATOR_DEBUG
    if (new_key[i]->rns_base_id() != rns_base_id) {
      throw std::invalid_argument("new_key rns base is missing / not in order");
    }
#endif

    auto modulus = context_.get_rns_modulus(rns_base_id);
    auto base_conv_factor_mod =
        Util::safe_cast<Limb::Element_t>(seal::util::modulo_uint(
            base_conv_factor.get(), base_conv_factor_size, modulus));
    auto c0_data = c0->at(rns_base_id)->data();
    auto new_key_data = new_key[i]->data();
    for (size_t j = 0; j < coeff_count; j++) {
      // std::cout <<
      // Util::multiply_uint_mod(new_key_data[j],base_conv_factor_mod,modulus)
      // << ", ";
      c0_data[j] = Util::multiply_add_uint_mod(
          new_key_data[j], base_conv_factor_mod, c0_data[j], modulus);
    }
    // std::cout << "\n";
  }

  return std::make_pair(c0, c1);
}
std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::generate_relin_evalkey(
    const std::uint64_t level, const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool) {
  LimbVector new_key;
  new_key.reserve(digit_partition.size());
  for (size_t i = 0; i < digit_partition.size(); i++) {
    auto rns_base_id = digit_partition.at(i);
    new_key.push_back(secret_key_squared_rns_.at(rns_base_id));
  }
  return generate_evalkey(new_key, level, extension_size, digit_partition,
                          pool);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::generate_rotation_evalkey(
    const std::int32_t rotation_amount, const std::uint64_t level,
    const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool) {
  LimbVector new_key;

  auto coeff_count = context_.n();
  auto galois_tool = context_.galois_tool();
  auto galois_elt = galois_tool->get_elt_from_step(rotation_amount);

  new_key.reserve(digit_partition.size());
  for (size_t i = 0; i < digit_partition.size(); i++) {
    auto rns_base_id = digit_partition.at(i);
    auto buffer = Util::allocate_uint2(coeff_count);
    galois_tool->apply_galois_ntt(secret_key_rns_.at(rns_base_id)->data(),
                                  coeff_count, galois_elt, buffer.get());
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count,
                                       rns_base_id, true);
    new_key.push_back(std::move(limb));
  }

  return generate_evalkey(new_key, level, extension_size, digit_partition,
                          pool);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::generate_rotation_inv_evalkey(
    const std::int32_t rotation_amount, const std::uint64_t level,
    const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool) {
  LimbVector rotated_key;
  LimbVector key_rns;

  auto coeff_count = context_.n();
  auto galois_tool = context_.galois_tool();
  auto galois_elt = galois_tool->get_elt_from_step(rotation_amount);

  rotated_key.reserve(secret_key_rns_.size());
  for (size_t i = 0; i < secret_key_rns_.size(); i++) {
    auto rns_base_id = secret_key_rns_.at(i)->rns_base_id();
    auto buffer = Util::allocate_uint2(coeff_count);
    galois_tool->apply_galois_ntt(secret_key_rns_.at(i)->data(), coeff_count,
                                  galois_elt, buffer.get());
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count,
                                       rns_base_id, true);
    rotated_key.push_back(std::move(limb));
  }
  key_rns.reserve(digit_partition.size());
  for (size_t i = 0; i < digit_partition.size(); i++) {
    auto rns_base_id = digit_partition.at(i);
    auto buffer = Util::allocate_uint2(coeff_count);
    Util::set_uint(secret_key_rns_.at(rns_base_id)->data(), coeff_count,
                   buffer.get());
    // galois_tool->apply_galois_ntt(secret_key_rns_.at(rns_base_id)->data(),
    // coeff_count, galois_elt, buffer.get());
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count,
                                       rns_base_id, true);
    key_rns.push_back(std::move(limb));
  }

  return generate_evalkey(rotated_key, key_rns, level, extension_size,
                          digit_partition, pool);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::generate_conjugation_evalkey(
    const std::uint64_t level, const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool) {
  return generate_rotation_evalkey(0, level, extension_size, digit_partition,
                                   pool);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::generate_bootstrap_evalkey(
    const std::uint64_t level, const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool) {
  LimbVector new_key;

  auto coeff_count = context_.n();
  auto p = context_.get_rns_modulus(level - 1).value();

  new_key.reserve(digit_partition.size());
  for (size_t i = 0; i < digit_partition.size(); i++) {
    auto rns_base_id = digit_partition.at(i);
    // auto buffer = Util::allocate_uint(coeff_count, pool);
    auto buffer = Util::allocate_uint2(coeff_count);
    auto &modulus = context_.get_rns_modulus(rns_base_id);
    auto p_mod = Util::safe_cast<Limb::Element_t>(
        seal::util::barrett_reduce_64(p, modulus));
    auto secret_key_data = secret_key_rns_.at(rns_base_id)->data();
    for (size_t j = 0; j < coeff_count; j++) {
      buffer[j] = Util::multiply_uint_mod(secret_key_data[j], p_mod, modulus);
    }
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count,
                                       rns_base_id, true);
    new_key.push_back(std::move(limb));
  }

  return generate_evalkey(new_key, level, extension_size, digit_partition,
                          pool);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::generate_ephemeral_evalkey(
    const std::uint64_t level, const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool) {
  LimbVector new_key;
  new_key.reserve(digit_partition.size());
  for (size_t i = 0; i < digit_partition.size(); i++) {
    auto rns_base_id = digit_partition.at(i);
    new_key.push_back(secret_key_rns_.at(rns_base_id));
  }
  return generate_evalkey(new_key, level, extension_size, digit_partition, pool,
                          true);
}

std::pair<RnsPolynomialPtr, RnsPolynomialPtr>
CKKSEncryptor::generate_bootstrap_evalkey2(
    const std::uint64_t level, const std::uint64_t extension_size,
    const std::vector<std::uint64_t> &digit_partition,
    seal::MemoryPoolHandle pool) {
  LimbVector new_key;

  auto coeff_count = context_.n();
  auto p = context_.get_rns_modulus(level - 1).value();

  new_key.reserve(digit_partition.size());
  for (size_t i = 0; i < digit_partition.size(); i++) {
    auto rns_base_id = digit_partition.at(i);
    // auto buffer = Util::allocate_uint(coeff_count, pool);
    auto buffer = Util::allocate_uint2(coeff_count);
    auto &modulus = context_.get_rns_modulus(rns_base_id);
    auto p_mod = Util::safe_cast<Limb::Element_t>(
        seal::util::barrett_reduce_64(p, modulus));
    auto secret_key_data = ephemeral_key_rns_.at(rns_base_id)->data();
    for (size_t j = 0; j < coeff_count; j++) {
      buffer[j] = Util::multiply_uint_mod(secret_key_data[j], p_mod, modulus);
    }
    auto limb = std::make_shared<Limb>(std::move(buffer), coeff_count,
                                       rns_base_id, true);
    new_key.push_back(std::move(limb));
  }

  return generate_evalkey(new_key, level, extension_size, digit_partition,
                          pool);
}

} // namespace Cinnamon::Emulator

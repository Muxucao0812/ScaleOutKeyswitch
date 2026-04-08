// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include <seal/seal.h>
#include <seal/util/mempool.h>
#include <seal/util/polyarithsmallmod.h>
#include <seal/util/rns.h>
#include <seal/util/uintarithmod.h>
#include <thread>
#include <tuple>

#include "config.h"
#include "evaluator.h"
#include "util.h"

namespace Cinnamon::Emulator {
LimbPtr Evaluator::add(const LimbPtr &operand1, const LimbPtr &operand2,
                       const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->rns_base_id() != operand2->rns_base_id()) {
    throw std::invalid_argument(
        "Operands must have the same RNS base id for addition");
  }
  if (operand1->is_ntt_form() != true) {
    throw std::invalid_argument("NTT Form mismatch");
  }
  if (operand2->is_ntt_form() != true) {
    throw std::invalid_argument("NTT Form mismatch");
  }
#endif

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand1_data = operand1->data();
  auto operand2_data = operand2->data();
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);
  for (size_t i = 0; i < coeff_count_; i++) {
    destination_data[i] =
        Util::add_uint_mod(operand1_data[i], operand2_data[i], modulus);
  }

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand1->is_ntt_form());
  return std::move(destination);
}

LimbPtr Evaluator::add(const LimbPtr &operand1, const Limb::Element_t &operand2,
                       const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->rns_base_id() != operand2->rns_base_id()) {
    throw std::invalid_argument(
        "Operands must have the same RNS base id for addition");
  }
  if (operand1->is_ntt_form() != true) {
    throw std::invalid_argument("NTT Form mismatch");
  }
  if (operand2->is_ntt_form() != true) {
    throw std::invalid_argument("NTT Form mismatch");
  }
#endif

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand1_data = operand1->data();
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);
  for (size_t i = 0; i < coeff_count_; i++) {
    destination_data[i] =
        Util::add_uint_mod(operand1_data[i], operand2, modulus);
  }

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand1->is_ntt_form());
  return std::move(destination);
}

LimbPtr Evaluator::get_zero() {
  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  for (size_t i = 0; i < coeff_count_; i++) {
    destination_data[i] = 0;
  }
  auto destination = std::make_shared<Limb>(std::move(destination_data),
                                            coeff_count_, -1, false);
  return std::move(destination);
}

LimbPtr Evaluator::subtract(const LimbPtr &operand1, const LimbPtr &operand2,
                            const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->rns_base_id() != operand2->rns_base_id()) {
    throw std::invalid_argument(
        "Operands must have the same RNS base id for addition");
  }
  if (operand1->is_ntt_form() != operand2->is_ntt_form()) {
    throw std::invalid_argument("NTT Form mismatch");
  }
#endif

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand1_data = operand1->data();
  auto operand2_data = operand2->data();
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);
  for (size_t i = 0; i < coeff_count_; i++) {
    destination_data[i] =
        Util::subtract_uint_mod(operand1_data[i], operand2_data[i], modulus);
  }

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand1->is_ntt_form());
  return std::move(destination);
}

LimbPtr Evaluator::multiply(const LimbPtr &operand1, const LimbPtr &operand2,
                            const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->rns_base_id() != operand2->rns_base_id()) {
    throw std::invalid_argument(
        "Operands must have the same RNS base id for addition");
  }
  if (operand1->is_ntt_form() != true) {
    throw std::invalid_argument("Operand 1 must be in NTT Form");
  }
  if (operand2->is_ntt_form() != true) {
    throw std::invalid_argument("Operand 2 must be in NTT Form");
  }
#endif

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand1_data = operand1->data();
  auto operand2_data = operand2->data();
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);
  for (size_t i = 0; i < coeff_count_; i++) {
    destination_data[i] =
        Util::multiply_uint_mod(operand1_data[i], operand2_data[i], modulus);
  }

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand1->is_ntt_form());
  return std::move(destination);
}

LimbPtr Evaluator::subtract(const LimbPtr &operand1,
                            const Limb::Element_t &operand2,
                            const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->rns_base_id() != operand2->rns_base_id()) {
    throw std::invalid_argument(
        "Operands must have the same RNS base id for addition");
  }
  if (operand1->is_ntt_form() != operand2->is_ntt_form()) {
    throw std::invalid_argument("NTT Form mismatch");
  }
#endif

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand1_data = operand1->data();
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);
  for (size_t i = 0; i < coeff_count_; i++) {
    destination_data[i] =
        Util::subtract_uint_mod(operand1_data[i], operand2, modulus);
  }

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand1->is_ntt_form());
  return std::move(destination);
}

LimbPtr Evaluator::multiply(const LimbPtr &operand1,
                            const Limb::Element_t &operand2,
                            const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->rns_base_id() != rns_base_id) {
    throw std::invalid_argument(
        "Operands must have the same RNS base id for multiplication");
  }
#endif

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand1_data = operand1->data();
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);
  for (size_t i = 0; i < coeff_count_; i++) {
    destination_data[i] =
        Util::multiply_uint_mod(operand1_data[i], operand2, modulus);
  }

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand1->is_ntt_form());
  return std::move(destination);
}

LimbPtr Evaluator::rotate(const LimbPtr &operand1,
                          const int32_t rotation_amount,
                          const uint64_t rns_base_id) {

#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->is_ntt_form() != true) {
    throw std::invalid_argument("Operand not in NTT form");
  }
#endif

  if (rotation_amount == 0) {
    throw std::invalid_argument("rotation_amount can't be zero");
  }

  auto galois_tool = context_.galois_tool();
  auto galois_elt = galois_tool->get_elt_from_step(rotation_amount);

  // auto destination_data = Util::allocate_uint(coeff_count_, pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  galois_tool->apply_galois_ntt(operand1->data(), coeff_count_, galois_elt,
                                destination_data.get());
  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand1->is_ntt_form());
  return std::move(destination);
}

LimbPtr Evaluator::conjugate(const LimbPtr &operand1,
                             const uint64_t rns_base_id) {

  auto galois_tool = context_.galois_tool();
  auto galois_elt = galois_tool->get_elt_from_step(0 /* For conjugation */);

  // auto destination_data = Util::allocate_uint(coeff_count_, pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  galois_tool->apply_galois_ntt(operand1->data(), coeff_count_, galois_elt,
                                destination_data.get());
  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand1->is_ntt_form());
  return std::move(destination);
}

void Evaluator::ntt_inplace(LimbPtr &operand, const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand->is_ntt_form() == true) {
    throw std::invalid_argument("operand not in NTT Form");
  }
#endif

  // destination.rns_base_id() = limb1.rns_base_id();
  // destination.is_ntt_form() = false;
  // destination.coeff_modulus_size() = limb1.coeff_modulus_size();
  // destination.resize(limb1.size());

  auto operand_data = operand->data();
  auto &ntt_tables = context_.ntt_tables()[rns_base_id];
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);

  // Inplace NTT
  Util::ntt_negacyclic_harvey(operand_data, ntt_tables);
  operand->form_ = Limb::Form::NTT;
}

LimbPtr Evaluator::ntt(const LimbPtr &operand,
                       const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand->is_ntt_form() == true) {
    throw std::invalid_argument("operand not in NTT Form");
  }
#endif

  // destination.rns_base_id() = limb1.rns_base_id();
  // destination.is_ntt_form() = false;
  // destination.coeff_modulus_size() = limb1.coeff_modulus_size();
  // destination.resize(limb1.size());

  auto operand_data = operand->data();
  auto &ntt_tables = context_.ntt_tables()[rns_base_id];
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);
  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  Util::set_uint(operand_data, coeff_count_, destination_data.get());

  // Inplace NTT
  Util::ntt_negacyclic_harvey(destination_data.get(), ntt_tables);

  auto destination = std::make_shared<Limb>(std::move(destination_data),
                                            coeff_count_, rns_base_id, true);
  return std::move(destination);
}

LimbPtr Evaluator::ntt(BaseConverter &src_base_converter,
                       const std::uint64_t src_base_converter_output_index,
                       const std::uint64_t rns_base_id) {
  auto t0 = src_base_converter.read(src_base_converter_output_index);
  ntt_inplace(t0, rns_base_id);
  return std::move(t0);
}

LimbPtr Evaluator::negate(const LimbPtr &operand,
                          const std::uint64_t rns_base_id) {

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand_data = operand->data();
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);
  for (size_t i = 0; i < coeff_count_; i++) {
    destination_data[i] = Util::negate_uint_mod(operand_data[i], modulus);
  }

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand->is_ntt_form());
  return std::move(destination);
}
LimbPtr Evaluator::copy(const LimbPtr &operand,
                        const std::uint64_t rns_base_id) {

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand_data = operand->data();
  Util::set_uint(operand_data, coeff_count_, destination_data.get());

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, operand->is_ntt_form());
  return std::move(destination);
}

LimbPtr Evaluator::inverse_ntt(const LimbPtr &operand,
                               const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand->is_ntt_form() != true) {
    throw std::invalid_argument("operand not in NTT Form");
  }
#endif

  // destination.rns_base_id() = limb1.rns_base_id();
  // destination.is_ntt_form() = false;
  // destination.coeff_modulus_size() = limb1.coeff_modulus_size();
  // destination.resize(limb1.size());

  auto operand_data = operand->data();
  auto &ntt_tables = context_.ntt_tables()[rns_base_id];
  seal::Modulus modulus = context_.get_rns_modulus(rns_base_id);

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  Util::set_uint(operand_data, coeff_count_, destination_data.get());
  Util::inverse_ntt_negacyclic_harvey(destination_data.get(), ntt_tables);

  auto destination = std::make_shared<Limb>(std::move(destination_data),
                                            coeff_count_, rns_base_id, false);
  return std::move(destination);
}

void Evaluator::inverse_ntt_inplace(LimbPtr &operand,
                                    const std::uint64_t rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand->is_ntt_form() != true) {
    throw std::invalid_argument("operand not in NTT Form");
  }
#endif

  auto operand_data = operand->data();
  auto &ntt_tables = context_.ntt_tables()[rns_base_id];
  auto &modulus = context_.get_rns_modulus(rns_base_id);

  Util::inverse_ntt_negacyclic_harvey(operand_data, ntt_tables);
  operand->form_ = Limb::Form::COEF;
}

LimbPtr
Evaluator::subtract_and_divide_modulus(const LimbPtr &operand1,
                                       const LimbPtr &operand2,
                                       const std::uint64_t rns_base_id) {

#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->is_ntt_form() != true) {
    throw std::invalid_argument("limb1 not in NTT Form");
  }
  if (operand2->is_ntt_form() != false) {
    throw std::invalid_argument("limb2 in NTT Form");
  }
#endif

  // auto temp_data = Util::allocate_uint(coeff_count_,pool_);
  auto temp_data = Util::allocate_uint2(coeff_count_);
  auto operand1_data = operand1->data();
  auto operand2_data = operand2->data();
  seal::Modulus modulus1 = context_.get_rns_modulus(rns_base_id);
  seal::Modulus modulus2 = context_.get_rns_modulus(operand2->rns_base_id());
  std::uint64_t modulus2_inverse = 0;

  if (!seal::util::try_invert_uint_mod(modulus2.value(), modulus1,
                                       modulus2_inverse)) {
    throw std::invalid_argument("modular inverse failed");
  }

  auto modulus1_value = Util::safe_cast<Limb::Element_t>(modulus1.value());

  for (size_t i = 0; i < coeff_count_; i++) {
    if (operand2_data[i] >= modulus1_value) {
      temp_data[i] = operand2_data[i] - modulus1_value;
    } else {
      temp_data[i] = operand2_data[i];
    }
  }

  Util::ntt_negacyclic_harvey(temp_data.get(),
                              context_.ntt_tables()[rns_base_id]);

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);

  for (size_t i = 0; i < coeff_count_; i++) {
    std::uint64_t x = seal::util::sub_uint_mod(
        static_cast<uint64_t>(operand1_data[i]), temp_data[i], modulus1);
    auto y = seal::util::multiply_uint_mod(x, modulus2_inverse, modulus1);
    destination_data[i] = Util::safe_cast<Limb::Element_t>(y);
  }

  auto destination = std::make_shared<Limb>(std::move(destination_data),
                                            coeff_count_, rns_base_id, true);
  return std::move(destination);
}

// LimbPtr Evaluator::subtract_and_divide_modulus(const LimbPtr & operand1,
// BaseConverter & src_base_converter, const std::uint64_t
// src_base_converter_output_index, const std::uint64_t rns_base_id){

LimbPtr Evaluator::subtract_and_divide_modulus(
    const LimbPtr &operand1, const LimbPtr &operand2,
    const std::uint64_t rns_base_id,
    const std::vector<uint64_t> &divide_base_ids) {

#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->is_ntt_form() != true) {
    throw std::invalid_argument("limb1 not in NTT Form");
  }
#endif
  // auto t0 = src_base_converter.read(src_base_converter_output_index);

  // ntt_inplace(t0,rns_base_id);
  auto temp = ntt(operand2, rns_base_id);
  auto t2 = subtract(operand1, temp, rns_base_id);
  // Multiply with scalar

  seal::util::Pointer<seal::util::RNSBase> input_rns_bases_;
  std::vector<seal::Modulus> input_rns_modulii;
  for (auto &id : divide_base_ids) {
    input_rns_modulii.push_back(context_.get_rns_modulus(id));
  }

  input_rns_bases_ = seal::util::allocate<seal::util::RNSBase>(
      pool_, input_rns_modulii, pool_);

  auto input_bases_prod = input_rns_bases_->base_prod();
  auto input_bases_size = input_rns_bases_->size();
  auto modulus = context_.get_rns_modulus(rns_base_id);
  auto input_bases_prod_mod =
      seal::util::modulo_uint(input_bases_prod, input_bases_size, modulus);
  std::uint64_t input_bases_prod_mod_inv;
  if (!seal::util::try_invert_uint_mod(input_bases_prod_mod, modulus,
                                       input_bases_prod_mod_inv)) {
    throw std::invalid_argument("modular inverse failed");
  }
  auto t3 =
      multiply(t2, Util::safe_cast<Limb::Element_t>(input_bases_prod_mod_inv),
               rns_base_id);
  return std::move(t3);
}

LimbPtr Evaluator::subtract_and_divide_modulus(
    const LimbPtr &operand1, BaseConverter &src_base_converter,
    const std::uint64_t src_base_converter_output_index,
    const std::uint64_t rns_base_id) {

#ifdef CINNAMON_EUMLATOR_DEBUG
  if (operand1->is_ntt_form() != true) {
    throw std::invalid_argument("limb1 not in NTT Form");
  }
#endif
  auto t0 = src_base_converter.read(src_base_converter_output_index);

  ntt_inplace(t0, rns_base_id);
  auto t2 = subtract(operand1, t0, rns_base_id);
  // Multiply with scalar
  auto input_bases_prod = src_base_converter.input_bases_product();
  auto input_bases_size = src_base_converter.input_bases_size();
  auto modulus = context_.get_rns_modulus(rns_base_id);
  auto input_bases_prod_mod =
      seal::util::modulo_uint(input_bases_prod, input_bases_size, modulus);
  std::uint64_t input_bases_prod_mod_inv;
  if (!seal::util::try_invert_uint_mod(input_bases_prod_mod, modulus,
                                       input_bases_prod_mod_inv)) {
    throw std::invalid_argument("modular inverse failed");
  }
  auto t3 =
      multiply(t2, Util::safe_cast<Limb::Element_t>(input_bases_prod_mod_inv),
               rns_base_id);
  return std::move(t3);
}

void Evaluator::pl1(const LimbPtr &operand1, BaseConverter &base_converter,
                    const std::uint64_t rns_base_id) {
  auto intt = inverse_ntt(operand1, rns_base_id);
  base_converter.write_inplace(rns_base_id, intt);
}

void Evaluator::bcw(const LimbPtr &operand1, BaseConverter &base_converter,
                    const std::uint64_t rns_base_id) {
  base_converter.write(rns_base_id, operand1);
}

void Evaluator::pl1_inplace(LimbPtr &operand1, BaseConverter &base_converter,
                            const std::uint64_t rns_base_id) {
  inverse_ntt_inplace(operand1, rns_base_id);
  base_converter.write_inplace(rns_base_id, operand1);
}

LimbPtr Evaluator::pl2(const LimbPtr &operand1,
                       BaseConverter &src_base_converter,
                       const std::uint64_t src_base_converter_output_index,
                       BaseConverter &dest_base_converter,
                       const std::uint64_t rns_base_id) {
  auto t0 = src_base_converter.read(src_base_converter_output_index);
  ntt_inplace(t0, rns_base_id);
  auto t1 = multiply(operand1, t0, rns_base_id);
  inverse_ntt_inplace(t1, rns_base_id);
  dest_base_converter.write_inplace(rns_base_id, t1);
  return std::move(t0);
}

void Evaluator::pl3(const LimbPtr &operand1, const LimbPtr &operand2,
                    BaseConverter &dest_base_converter,
                    const std::uint64_t rns_base_id) {
  auto t1 = multiply(operand1, operand2, rns_base_id);
  inverse_ntt_inplace(t1, rns_base_id);
  dest_base_converter.write_inplace(rns_base_id, t1);
}

LimbPtr Evaluator::pl4(const LimbPtr &operand1, const LimbPtr &operand2,
                       BaseConverter &src_base_converter,
                       const std::uint64_t src_base_converter_output_index,
                       const std::uint64_t rns_base_id) {
  auto t0 = src_base_converter.read(src_base_converter_output_index);
  ntt_inplace(t0, rns_base_id);
  // Multiply with scalar
  auto input_bases_prod = src_base_converter.input_bases_product();
  auto input_bases_size = src_base_converter.input_bases_size();
  auto &modulus = context_.get_rns_modulus(rns_base_id);
  auto input_bases_prod_mod =
      seal::util::modulo_uint(input_bases_prod, input_bases_size, modulus);
  std::uint64_t input_bases_prod_mod_inv_64;
  if (!seal::util::try_invert_uint_mod(input_bases_prod_mod, modulus,
                                       input_bases_prod_mod_inv_64)) {
    throw std::invalid_argument("modular inverse failed");
  }
  auto input_bases_prod_mod_inv =
      Util::safe_cast<Limb::Element_t>(input_bases_prod_mod_inv_64);

  auto operand1_data = operand1->data();
  auto operand2_data = operand2->data();
  auto t0_data = t0->data();

  for (size_t i = 0; i < coeff_count_; i++) {
    auto x =
        Util::multiply_uint_mod(operand1_data[i], operand2_data[i], modulus);
    x = Util::subtract_uint_mod(x, t0_data[i], modulus);
    t0_data[i] = Util::multiply_uint_mod(x, input_bases_prod_mod_inv, modulus);
  }
  return std::move(t0);
}

// Don't optimize this function. There's some weird bug caused by the optimizer
// that causes
#ifdef __clang__
[[clang::optnone]]
#elif defined __clang__
__attribute__((optimize("O1")))
#endif


    
    void  Evaluator::resolve_write(const LimbPtr & operand, const std::vector<LimbPtr> & dests, const std::vector<std::uint64_t> & dest_rns_base_ids, const std::uint64_t rns_base_id){
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (dests.size() != dest_rns_base_ids.size()) {
    throw std::invalid_argument("dests");
  }
  if (operand->is_ntt_form() != false) {
    throw std::invalid_argument("input");
  }
#endif
  std::size_t input_modulus_pos = -1;
  std::vector<seal::Modulus> modulii;
  seal::Modulus modulus;
  for (auto i = 0; i < dest_rns_base_ids.size(); i++) {
    modulii.push_back(context_.get_rns_modulus(dest_rns_base_ids[i]));
    if (dest_rns_base_ids[i] == rns_base_id) {
      input_modulus_pos = i;
      modulus = modulii.at(i);
    }
  }
  if (modulus.is_zero()) {
    throw std::out_of_range("input");
  }
  seal::util::RNSBase rns_base(modulii, pool_);
  seal::util::StrideIter<const uint64_t *> ibase_punctured_prod_array(
      rns_base.punctured_prod_array(), rns_base.size());
  auto punctured_product = ibase_punctured_prod_array[input_modulus_pos];
  auto inv_punctured_product_modop =
      rns_base.inv_punctured_prod_mod_base_array()[input_modulus_pos];
  auto rns_base_prod = rns_base.base_prod();

  constexpr auto size_ratio = sizeof(uint64_t) / sizeof(Limb::Element_t);

  // auto accumulated_data =
  // seal::util::allocate<Limb::Element_t>(size_ratio*rns_base.size(),pool_);
  auto accumulated_data =
      Util::allocate<Limb::Element_t>(size_ratio * rns_base.size());
  // auto temp_data = seal::util::allocate_uint(rns_base.size(),pool_);
  auto temp_data = Util::allocate<uint64_t>(rns_base.size());
  auto operand_data = operand->data();

  for (size_t i = 0; i < coeff_count_; i++) {
    auto temp_prod =
        seal::util::multiply_uint_mod(static_cast<uint64_t>(operand_data[i]),
                                      inv_punctured_product_modop, modulus);
    seal::util::set_zero_uint(
        rns_base.size(), reinterpret_cast<uint64_t *>(accumulated_data.get()));
    seal::util::set_zero_uint(rns_base.size(), temp_data.get());
    seal::util::multiply_uint(punctured_product, rns_base.size(), temp_prod,
                              rns_base.size(), temp_data.get());

    for (size_t j = 0; j < rns_base.size(); j++) {
      accumulated_data[j] = dests[j]->data()[i];
      // std::cout << std::hex << accumulated_data[i] << std::dec;
    }

    seal::util::add_uint_uint_mod(
        reinterpret_cast<uint64_t *>(accumulated_data.get()), temp_data.get(),
        rns_base_prod, rns_base.size(),
        reinterpret_cast<uint64_t *>(accumulated_data.get()));

    for (size_t j = 0; j < rns_base.size(); j++) {
      dests[j]->data()[i] = accumulated_data[j];
    }
  }
}
// #if 0
std::uint64_t barrett_reduce_128_test(const uint64_t *input,
                                      const seal::Modulus &modulus) {
  // Reduces input using base 2^64 Barrett reduction
  // input allocation size must be 128 bits

  unsigned long long tmp1, tmp2[2], tmp3, carry;
  const std::uint64_t *const_ratio = modulus.const_ratio().data();

  // Multiply input and const_ratio
  // Round 1
  seal::util::multiply_uint64_hw64(input[0], const_ratio[0], &carry);

  seal::util::multiply_uint64(input[0], const_ratio[1], tmp2);
  tmp3 = tmp2[1] + seal::util::add_uint64(tmp2[0], carry, &tmp1);

  // Round 2
  seal::util::multiply_uint64(input[1], const_ratio[0], tmp2);
  carry = tmp2[1] + seal::util::add_uint64(tmp1, tmp2[0], &tmp1);

  // This is all we care about
  tmp1 = input[1] * const_ratio[1] + tmp3 + carry;

  // Barrett subtraction
  tmp3 = input[0] - tmp1 * modulus.value();

  // One more subtraction is enough
  return SEAL_COND_SELECT(tmp3 >= modulus.value(), tmp3 - modulus.value(),
                          tmp3);
}

std::uint64_t modulo_uint_test(const std::uint64_t *value,
                               std::size_t value_uint64_count,
                               const seal::Modulus &modulus) {
  if (value_uint64_count == 1) {
    printf("not this case\n");
  }

  // Temporary space for 128-bit reductions
  uint64_t temp[2]{0, value[value_uint64_count - 1]};
  for (size_t k = value_uint64_count - 1; k--;) {
    temp[0] = value[k];
    temp[1] = barrett_reduce_128_test(temp, modulus);
  }

  // Save the result modulo i-th prime
  return temp[1];
}
// #endif
LimbPtr Evaluator::mod(const std::vector<LimbPtr> &inputs,
                       const std::uint64_t rns_base_id) {

  if (inputs.size() == 0) {
    throw std::invalid_argument("inputs");
  }
  auto modulus = context_.get_rns_modulus(rns_base_id);
  auto num_limbs = inputs.size();

  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  // auto temp = seal::util::allocate_zero_uint(num_limbs,pool_);
  auto temp = Util::allocate<uint64_t>(num_limbs);
  std::fill_n(temp.get(), num_limbs, 0);
  auto temp_ptr = reinterpret_cast<Limb::Element_t *>(temp.get());
  SEAL_ITERATE(seal::util::iter(std::size_t(0), destination_data.get()),
               coeff_count_, [&](const auto &I) {
                 SEAL_ITERATE(seal::util::iter(inputs, temp_ptr), num_limbs,
                              [&](const auto &J) {
                                auto i = std::get<0>(I);
                                // std::get<1>(J) =
                                // (static_cast<uint64_t>(std::get<0>(J)->data()[2*i])<<32)
                                // | (std::get<0>(J)->data()[2*i+1]);
                                std::get<1>(J) = std::get<0>(J)->data()[i];
                              });
                 // std::get<1>(I) =
                 // Util::safe_cast<Limb::Element_t>(seal::util::modulo_uint(temp.get(),num_limbs,modulus));
                 // printf("temp[0] = %llu; temp[1] = %llu\n",temp[0], temp[1]);
                 std::get<1>(I) = Util::safe_cast<Limb::Element_t>(
                     modulo_uint_test(temp.get(), num_limbs, modulus));
               });

  // std::cout << destination_data[0] << "\n";
  auto destination = std::make_shared<Limb>(std::move(destination_data),
                                            coeff_count_, rns_base_id, false);
  return std::move(destination);
}

void BaseConverter::write(std::uint64_t rns_base_id, const LimbPtr &limb) {
  // auto destination_data = Util::allocate_uint(coeff_count_,pool_);
  auto destination_data = Util::allocate_uint2(coeff_count_);
  auto operand_data = limb->data();
  Util::set_uint(operand_data, coeff_count_, destination_data.get());

  auto destination =
      std::make_shared<Limb>(std::move(destination_data), coeff_count_,
                             rns_base_id, limb->is_ntt_form());
  write_inplace(rns_base_id, destination);
}

void BaseConverter::write_inplace(std::uint64_t rns_base_id, LimbPtr &limb) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (rns_base_id != limb->rns_base_id()) {
    throw std::invalid_argument("base_converter_input_index");
  }
  if (limb->is_ntt_form() != false) {
    throw std::invalid_argument("limb.is_ntt_form()");
  }
#endif
  int base_converter_input_index = -1;
  for (size_t i = 0; i < input_rns_base_ids_.size(); i++) {
    if (rns_base_id == input_rns_base_ids_[i]) {
      base_converter_input_index = i;
    }
  }
  if (base_converter_input_index == -1) {
    throw std::runtime_error("Invalid RNS base");
  }

  input_limbs_[base_converter_input_index] = std::move(limb);

  writes_completed_++;
}

void BaseConverter::convert() {

  if (converted_) {
    return;
  }

  buffer_ = Util::allocate<Util::Pointer<Limb::Element_t>>(
      output_rns_base_ids_.size());
  SEAL_ITERATE(buffer_.get(), output_rns_base_ids_.size(),
               [&](auto &I) { I = Util::allocate_uint2(coeff_count_); });

  size_t TILE_SIZE = 64;
  TILE_SIZE = std::min(coeff_count_, TILE_SIZE);
  uint32_t limbs_written = 0;
  for (size_t k = 0; k < input_rns_base_ids_.size(); k++) {
    auto &limb = input_limbs_[k];
    auto input_data = limb->data();
    for (size_t i = 0; i < coeff_count_; i += TILE_SIZE) {
      const auto &base_conversion_factors = base_change_matrix_[k];
      for (size_t j = 0; j < output_rns_base_ids_.size(); j++) {
        for (size_t ii = 0; ii < TILE_SIZE; ii++) {
          auto modulus = context_.get_rns_modulus(output_rns_base_ids_[j]);
          if (k == 0) {
            buffer_[j][i + ii] =
                Util::safe_cast<Limb::Element_t>(seal::util::multiply_uint_mod(
                    input_data[i + ii], base_conversion_factors[j], modulus));
          } else {
            buffer_[j][i + ii] = Util::safe_cast<Limb::Element_t>(
                seal::util::multiply_add_uint_mod(input_data[i + ii],
                                                  base_conversion_factors[j],
                                                  buffer_[j][i + ii], modulus));
          }
        }
      }
    }
  }

  input_limbs_.clear();
  converted_ = true;
}

void BaseConverter::parallel_convert() {

  if (converted_) {
    return;
  }

  buffer_ = Util::allocate<Util::Pointer<Limb::Element_t>>(
      output_rns_base_ids_.size());
  SEAL_ITERATE(buffer_.get(), output_rns_base_ids_.size(),
               [&](auto &I) { I = Util::allocate_uint2(coeff_count_); });

  size_t TILE_SIZE = 64;
  TILE_SIZE = std::min(coeff_count_, TILE_SIZE);
  uint32_t limbs_written = 0;

  auto NUM_OUTPUT_LIMBS = output_rns_base_ids_.size();
  size_t NUM_THREADS = (NUM_OUTPUT_LIMBS / 4) + 1;
  auto thread_fn = [&](size_t tid) {
    for (size_t k = 0; k < input_rns_base_ids_.size(); k++) {
      auto &limb = input_limbs_[k];
      auto input_data = limb->data();
      const auto &base_conversion_factors = base_change_matrix_[k];
      for (size_t i = 0; i < coeff_count_; i += TILE_SIZE) {
        for (size_t j = tid; j < NUM_OUTPUT_LIMBS; j += NUM_THREADS) {
          auto &modulus = context_.get_rns_modulus(output_rns_base_ids_[j]);
          for (size_t ii = 0; ii < TILE_SIZE; ii++) {
            if (k == 0) {
              buffer_[j][i + ii] = Util::safe_cast<Limb::Element_t>(
                  seal::util::multiply_uint_mod(
                      input_data[i + ii], base_conversion_factors[j], modulus));
            } else {
              buffer_[j][i + ii] = Util::safe_cast<Limb::Element_t>(
                  seal::util::multiply_add_uint_mod(
                      input_data[i + ii], base_conversion_factors[j],
                      buffer_[j][i + ii], modulus));
            }
          }
        }
      }
    }
  };

  std::vector<std::thread> threads;
  for (size_t t = 0; t < NUM_THREADS; t++) {
    threads.push_back(std::thread(thread_fn, t));
  }

  for (size_t t = 0; t < NUM_THREADS; t++) {
    threads[t].join();
  }

  input_limbs_.clear();
  converted_ = true;
}

void BaseConverter::init(
    const std::vector<std::uint64_t> &input_rns_base_ids,
    const std::vector<std::uint64_t> &output_rns_base_ids) {
  input_rns_base_ids_ = input_rns_base_ids;
  output_rns_base_ids_ = output_rns_base_ids;

  std::vector<seal::Modulus> input_rns_modulii;
  for (auto &id : input_rns_base_ids_) {
    input_rns_modulii.push_back(context_.get_rns_modulus(id));
  }

  input_rns_bases_ = seal::util::allocate<seal::util::RNSBase>(
      pool_, input_rns_modulii, pool_);
  base_change_matrix_ =
      std::vector<Util::Pointer<seal::util::MultiplyUIntModOperand>>(
          input_rns_base_ids_.size());
  seal::util::StrideIter<const uint64_t *> ibase_punctured_prod_array(
      input_rns_bases_->punctured_prod_array(), input_rns_bases_->size());

  SEAL_ITERATE(
      seal::util::iter(base_change_matrix_, ibase_punctured_prod_array,
                       input_rns_bases_->inv_punctured_prod_mod_base_array(),
                       input_rns_bases_->base()),
      input_rns_base_ids_.size(), [&](const auto &I) {
        std::get<0>(I) = Util::allocate<seal::util::MultiplyUIntModOperand>(
            output_rns_base_ids_.size());

        // multiply by 1 to convert from MutliplyOperand to std::uint64_t
        const std::uint64_t inv_punctured_product =
            seal::util::multiply_uint_mod(1, std::get<2>(I), std::get<3>(I));

        SEAL_ITERATE(
            seal::util::iter(std::get<0>(I).get(), output_rns_base_ids_),
            output_rns_base_ids_.size(), [&](const auto &J) {
              const seal::Modulus &output_modulus =
                  context_.get_rns_modulus(std::get<1>(J));
              const std::uint64_t x = seal::util::modulo_uint(
                  std::get<1>(I), input_rns_base_ids_.size(), output_modulus);
              auto y = seal::util::multiply_uint_mod(x, inv_punctured_product,
                                                     output_modulus);
              seal::util::MultiplyUIntModOperand operand;
              operand.set(y, output_modulus);
              std::get<0>(J) = operand;
            });
      });

  writes_completed_ = 0;
  converted_ = false;
  input_limbs_.clear();
  input_limbs_.resize(input_rns_base_ids_.size());
}

LimbPtr BaseConverter::read(std::uint64_t base_converter_output_index) {
#ifdef CINNAMON_EUMLATOR_DEBUG
  if (base_converter_output_index >= output_rns_base_ids_.size()) {
    throw std::invalid_argument("base_converter_output_index");
  }
  if (writes_completed_ != input_rns_base_ids_.size()) {
    throw std::logic_error("premature read");
  }
  if (buffer_[base_converter_output_index].get() == nullptr) {
    throw std::logic_error("read after read");
  }
#endif
  if (!converted_) {
    // convert();
    parallel_convert();
  }
  auto destination = std::make_shared<Limb>(
      std::move(buffer_[base_converter_output_index]), context_.n(),
      output_rns_base_ids_.at(base_converter_output_index), false);
  return destination;
}

} // namespace Cinnamon::Emulator

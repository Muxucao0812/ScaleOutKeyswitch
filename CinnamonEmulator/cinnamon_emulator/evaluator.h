// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <seal/seal.h>
#include <seal/util/mempool.h>
#include <seal/util/rns.h>

#include "context.h"
#include "limb.h"

namespace Cinnamon::Emulator {

class BaseConverter {

public:
  BaseConverter() = delete;
  BaseConverter(const Context &context)
      : context_(context), pool_(seal::MemoryManager::GetPool()), size_(0),
        coeff_count_(context_.n()), writes_completed_(0), converted_(false) {}
  void init(const std::vector<std::uint64_t> &input_rns_base_ids,
            const std::vector<std::uint64_t> &output_rns_base_ids);
  void write(const std::uint64_t rns_base_id, const LimbPtr &limb);
  void write_inplace(const std::uint64_t rns_base_id, LimbPtr &limb);
  [[nodiscard]] LimbPtr read(std::uint64_t base_coverter_output_index);

  inline auto input_bases_product() { return input_rns_bases_->base_prod(); }
  inline auto input_bases_size() { return input_rns_bases_->size(); }

private:
  const Context &context_;
  std::vector<std::uint64_t> input_rns_base_ids_;
  seal::util::Pointer<seal::util::RNSBase> input_rns_bases_;
  std::vector<std::uint64_t> output_rns_base_ids_;
  std::uint64_t writes_completed_;
  seal::MemoryPoolHandle pool_;
  Util::Pointer<Util::Pointer<Limb::Element_t>> buffer_;
  std::vector<Util::Pointer<seal::util::MultiplyUIntModOperand>>
      base_change_matrix_;
  std::size_t size_;
  std::size_t coeff_count_;

  bool converted_;
  std::vector<LimbPtr> input_limbs_;

  void convert();
  void parallel_convert();
};

class Evaluator {

public:
  Evaluator() = delete;
  Evaluator(const Context &context)
      : context_(context), pool_(seal::MemoryManager::GetPool()),
        coeff_count_(context.n()) {}

  LimbPtr add(const LimbPtr &operand1, const LimbPtr &operand2,
              const uint64_t rns_base_id);
  LimbPtr add(const LimbPtr &operand1, const Limb::Element_t &operand2,
              const uint64_t rns_base_id);
  LimbPtr subtract(const LimbPtr &operand1, const LimbPtr &operand2,
                   const uint64_t rns_base_id);
  LimbPtr subtract(const LimbPtr &operand1, const Limb::Element_t &operand2,
                   const uint64_t rns_base_id);
  LimbPtr multiply(const LimbPtr &operand1, const LimbPtr &operand2,
                   const uint64_t rns_base_id);
  LimbPtr multiply(const LimbPtr &operand1, const Limb::Element_t &operand2,
                   const uint64_t rns_base_id);
  LimbPtr negate(const LimbPtr &operand, const uint64_t rns_base_id);
  LimbPtr copy(const LimbPtr &operand, const uint64_t rns_base_id);
  LimbPtr rotate(const LimbPtr &operand1, const int32_t rotation_amount,
                 const uint64_t rns_base_id);
  LimbPtr conjugate(const LimbPtr &operand1, const uint64_t rns_base_id);
  LimbPtr ntt(const LimbPtr &operand1, const uint64_t rns_base_id);
  void ntt_inplace(LimbPtr &operand1, const uint64_t rns_base_id);
  LimbPtr ntt(BaseConverter &src_base_converter,
              const std::uint64_t src_base_converter_output_index,
              const std::uint64_t rns_base_id);
  LimbPtr inverse_ntt(const LimbPtr &operand1, const uint64_t rns_base_id);
  void inverse_ntt_inplace(LimbPtr &operand1, const uint64_t rns_base_id);
  LimbPtr subtract_and_divide_modulus(const LimbPtr &operand1,
                                      const LimbPtr &operand2,
                                      const uint64_t rns_base_id);
  LimbPtr subtract_and_divide_modulus(
      const LimbPtr &operand1, BaseConverter &src_base_converter,
      const std::uint64_t src_base_converter_output_index,
      const std::uint64_t rns_base_id);
  LimbPtr
  subtract_and_divide_modulus(const LimbPtr &operand1, const LimbPtr &operand2,
                              const std::uint64_t rns_base_id,
                              const std::vector<uint64_t> &divide_base_ids);
  void bcw(const LimbPtr &operand1, BaseConverter &base_converter,
           const std::uint64_t rns_base_id);
  void pl1(const LimbPtr &operand1, BaseConverter &base_converter,
           const std::uint64_t rns_base_id);
  void pl1_inplace(LimbPtr &operand1, BaseConverter &base_converter,
                   const std::uint64_t rns_base_id);
  LimbPtr pl2(const LimbPtr &operand1, BaseConverter &src_base_converter,
              const std::uint64_t src_base_converter_output_index,
              BaseConverter &dest_base_converter,
              const std::uint64_t rns_base_id);
  void pl3(const LimbPtr &operand1, const LimbPtr &operand2,
           BaseConverter &dest_base_converter, const std::uint64_t rns_base_id);
  LimbPtr pl4(const LimbPtr &operand1, const LimbPtr &operand2,
              BaseConverter &src_base_converter,
              const std::uint64_t src_base_converter_output_index,
              const std::uint64_t rns_base_id);

  LimbPtr get_zero();
  void resolve_write(const LimbPtr &operand, const std::vector<LimbPtr> &dests,
                     const std::vector<std::uint64_t> &dest_rns_base_ids,
                     const std::uint64_t rns_base_id);
  LimbPtr mod(const std::vector<LimbPtr> &inputs,
              const std::uint64_t rns_base_id);

private:
  const Context &context_;
  seal::MemoryPoolHandle pool_;
  std::size_t coeff_count_;
};

} // namespace Cinnamon::Emulator

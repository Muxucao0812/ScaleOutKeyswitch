// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <seal/seal.h>
#include <seal/util/mempool.h>
#include <seal/util/rns.h>

#include "pointer.h"

class Context;

namespace Cinnamon::Emulator {
template <template <typename> typename T> class LimbBase {

public:
  enum Form { NTT, COEF };
  using Element_t = std::uint32_t;

  static_assert(std::is_same<Element_t, std::uint32_t>::value ||
                    std::is_same<Element_t, std::uint64_t>::value,
                "unsupported Element_t");
  // static_assert(std::is_same<T, Util::Pointer>::value , "unsupported T");
  LimbBase() = delete;
  LimbBase(const LimbBase &limb) = delete;
  LimbBase(LimbBase &&limb) = default;
  // Limb(const Context & context) : context_(context), rns_base_id_(0) ,
  // is_ntt_form_(true) ,pool_(seal::MemoryManager::GetPool()) {};
  LimbBase(const std::vector<Element_t> &values, std::uint64_t rns_base_id,
           bool is_ntt_form)
      : rns_base_id_(rns_base_id) {
#ifdef CINNAMON_EUMLATOR_DEBUG
    // if (values.size() != context.n()) {
    //     throw std::invalid_argument("Invalid number entries in values");
    // }
#endif
    size_ = values.size();
    data_ = T<Element_t>(size_);
    data_.copy(values, size_);

    if (is_ntt_form) {
      form_ = Form::NTT;
    } else {
      form_ = Form::COEF;
    }
  }
  LimbBase(T<Element_t> &&data, std::size_t size, std::uint64_t rns_base_id,
           bool is_ntt_form)
      : rns_base_id_(rns_base_id), data_(std::move(data)), size_(size) {
    if (is_ntt_form) {
      form_ = Form::NTT;
    } else {
      form_ = Form::COEF;
    }
  }

  std::uint64_t rns_base_id() const { return rns_base_id_; }

  bool is_ntt_form() const { return form_ == Form::NTT; }

  std::size_t size() const { return size_; }

  std::size_t &size() { return size_; }

  const Element_t *data() const { return data_.get(); }

  // XXX: I'm not sure whether this function is a good idea?
  Element_t *data() { return data_.get(); }

  LimbBase<Util::Pointer> move_to_host() {
    Util::Pointer host_pointer = data_.move_to_host();
    return LimbBase<Util::Pointer>(std::move(host_pointer), size_, rns_base_id_,
                                   form_ == Form::NTT);
  }

  std::shared_ptr<LimbBase<Util::Pointer>> move_to_host_ptr() {
    Util::Pointer host_pointer = data_.move_to_host();
    return std::make_shared<LimbBase<Util::Pointer>>(
        std::move(host_pointer), size_, rns_base_id_, form_ == Form::NTT);
  }

  void set_form_ntt() { form_ = Form::NTT; }

  void set_form_coef() { form_ = Form::COEF; }

  void set_rns_base_id(const uint64_t rns_base_id) {
    rns_base_id_ = rns_base_id;
  }

private:
  std::uint64_t rns_base_id_;
  Form form_;
  T<Element_t> data_;
  std::size_t size_;

  friend class Evaluator;
};

using Limb = LimbBase<Util::Pointer>;
using LimbPtr = std::shared_ptr<Limb>;

} // namespace Cinnamon::Emulator

// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include "config.h"

#define CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)                                 \
  template <typename T,                                                        \
            typename = std::enable_if_t<                                       \
                std::is_same<std::remove_cv_t<T>, std::uint64_t>::value ||     \
                std::is_same<std::remove_cv_t<T>, std::uint32_t>::value>>

namespace Cinnamon::Emulator {

namespace Util {

/** Wrapper Class around std::shared_ptr for Cinnamon::Emulator. */
template <typename T> class Pointer {

public:
  Pointer() {}

  Pointer(const size_t n) : n_(n) { ptr_.reset(new T[n]); }

  Pointer(Pointer<T> &&assign) noexcept : n_(assign.n_) {
    ptr_ = std::move(assign.ptr_);
  }

  inline T *get() { return ptr_.get(); }

  inline T *get() const { return ptr_.get(); }

  inline T *operator->() const noexcept { return ptr_.get(); }

  inline T &operator*() const { return *ptr_; }

  inline T &operator[](size_t index) { return ptr_[index]; }

  inline const T &operator[](size_t index) const { return ptr_[index]; }

  inline auto &operator=(Pointer<T> &&assign) noexcept {
    ptr_ = std::move(assign.ptr_);
    return *this;
  }

  inline auto n() const { return n_; }

  void copy(const std::vector<T> &src, const std::size_t count) {
    for (size_t i = 0; i < count; i++) {
      ptr_[i] = src[i];
    }
  }

  Util::Pointer<T> move_to_host() { return std::move(*this); }

private:
  std::shared_ptr<T[]> ptr_;
  std::size_t n_;
  inline auto &operator=(Pointer<T> &assign) = delete;
};
} // namespace Util
} // namespace Cinnamon::Emulator
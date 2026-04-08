// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include <stdexcept>
#include <string>
#include <cstdint>

namespace Cinnamon {
namespace Frontend {

#define CINNAMON_TYPES                                                         \
  X(Undef, 0)                                                                  \
  X(Cipher, 1)                                                                 \
  X(Plain, 2)

enum class Type : std::int32_t {
#define X(type, code) type = code,
  CINNAMON_TYPES
#undef X
};

inline std::string getTypeName(Type type) {
  switch (type) {
#define X(type, code)                                                          \
  case Type::type:                                                             \
    return #type;
    CINNAMON_TYPES
#undef X
  default:
    throw std::runtime_error("Invalid type");
  }
}

} // namespace Frontend
} // namespace Cinnamon

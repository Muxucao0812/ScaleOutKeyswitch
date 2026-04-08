// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "cinnamon/frontend/attribute_list.h"
#include <cstdint>
#include <string>

namespace Cinnamon {
namespace Frontend {

#define CINNAMON_ATTRIBUTES                                                    \
  X(RotationAttribute, std::int32_t)                                           \
  X(TypeAttribute, Type)                                                       \
  X(ModRaiseLevelAttribute, std::uint32_t)                                     \
  X(IsScalarAttribute, bool)                                                   \
  X(RotMulAccRotationAttribute, std::vector<int32_t>)                          \
  X(RotAccRotationAttribute, std::vector<int32_t>)                             \
  X(MultiRotationAttribute, std::vector<int32_t>)                              \
  X(NameAttribute, std::string)                                                \
  X(BsgsMulAccBabyStepAttribute, std::vector<int32_t>)                         \
  X(BsgsMulAccGiantStepAttribute, std::vector<int32_t>)                        \
  X(TermIdxInVec, std::uint32_t)                                               \
  X(PartitionSizeAttribute, std::uint32_t)                                     \
  X(PartitionIdAttribute, std::uint32_t) //\

namespace detail {
enum AttributeIndex {
  RESERVE_EMPTY_ATTRIBUTE_KEY = 0,
#define X(name, type) name##Index,
  CINNAMON_ATTRIBUTES
#undef X
};
} // namespace detail

#define X(name, type) using name = Attribute<detail::name##Index, type>;
CINNAMON_ATTRIBUTES
#undef X

bool isValidAttribute(AttributeKey k, const AttributeValue &v);

std::string getAttributeName(AttributeKey k);

} // namespace Frontend
} // namespace Cinnamon

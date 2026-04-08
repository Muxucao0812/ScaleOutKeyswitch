// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#include "cinnamon/frontend/attributes.h"
#include <stdexcept>

using namespace std;

namespace Cinnamon {
namespace Frontend {

#define X(name, type) name::isValid(k, v) ||
bool isValidAttribute(AttributeKey k, const AttributeValue &v) {
  return CINNAMON_ATTRIBUTES false;
}
#undef X

#define X(name, type)                                                          \
  case detail::name##Index:                                                    \
    return #name;
string getAttributeName(AttributeKey k) {
  switch (k) {
    CINNAMON_ATTRIBUTES
  default:
    throw runtime_error("Unknown attribute key");
  }
}
#undef X

} // namespace Frontend
} // namespace Cinnamon

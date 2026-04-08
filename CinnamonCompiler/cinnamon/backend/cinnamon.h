// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "cinnamon/frontend/program.h"
#include <cassert>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>

namespace Cinnamon {
namespace Backend {

void cinnamonCompile(Frontend::Program &program, const uint32_t levels,
                     const uint8_t mod_partitions, const uint64_t num_vregs,
                     const std::string &output_prefix, const bool use_cinnamon_keyswitching=true);
void keyswitchPass(Frontend::Program &program);

} // namespace Backend
} // namespace Cinnamon

// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#include "cinnamon/util/program_traversal.h"
#include "cinnamon/util/logging.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cinnamon/backend/cinnamon.h"
#include "cinnamon/backend/cinnamon_compiler.h"
#include "cinnamon/backend/keyswitch_pass.h"

using namespace std;

namespace Cinnamon {
  namespace Backend {

void cinnamonCompile(Frontend::Program &program, const uint32_t levels, const uint8_t num_partitions, const uint64_t num_vregs, const std::string & output_prefix, const bool use_cinnamon_keyswitching) {
  ProgramTraversalNew programTraverse(program, true);
  auto cinnamonCompiler = CinnamonCompiler(program, levels, num_partitions, num_vregs, output_prefix, use_cinnamon_keyswitching);
  programTraverse.forwardPass(cinnamonCompiler);
  cinnamonCompiler.compile();
}

void keyswitchPass(Frontend::Program &program) {
  std::cout << "Running Cinnamon Keyswitch Pass\n";
  ProgramTraversalNew programTraverseForward(program, true);
  auto receiveEliminatorPass = CinnamonP::CommonReceiveEliminatorPass(program);
  programTraverseForward.forwardPass(receiveEliminatorPass);
  ProgramTraversal programTraverse(program, false);
  auto hoistInputBroadcastPass = CinnamonP::HoistInputBroadcastPass(program);
  programTraverse.backwardPass(hoistInputBroadcastPass);
  auto fuseAggregationPass = CinnamonP::FuseAggregationPass(program);
  programTraverse.backwardPass(fuseAggregationPass);
}
  } // namespace Backend
} // namespace Cinnamon

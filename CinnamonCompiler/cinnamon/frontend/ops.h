// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include <stdexcept>
#include <string>

namespace Cinnamon {
namespace Frontend {

#define CINNAMON_OPS                                                           \
  X(Undef, 0)                                                                  \
  X(Input, 1)                                                                  \
  X(Output, 2)                                                                 \
  X(Constant, 3)                                                               \
  X(Receive, 4)                                                                \
  X(Send, 5)                                                                   \
  X(Negate, 10)                                                                \
  X(Add, 11)                                                                   \
  X(Sub, 12)                                                                   \
  X(Mul, 13)                                                                   \
  X(RotateLeftConst, 14)                                                       \
  X(RotateRightConst, 15)                                                      \
  X(Relinearize, 20)                                                           \
  X(ModSwitch, 21)                                                             \
  X(Rescale, 22)                                                               \
  X(BootstrapModRaise, 31)                                                     \
  X(Conjugate, 32)                                                             \
  X(RotMulAcc, 33)                                                             \
  X(MulRotAcc, 34)                                                             \
  X(RotAcc, 35)                                                                \
  X(BsgsMulAcc, 36)                                                            \
  X(TermInCiphertextVec, 38)                                                   \
  X(ToEphemeral, 39)                                                           \
  X(Relinearize2, 50)                                                          \
  X(Partition, 40)                                                             \
  X(Nop, 49)                                                                   \
  X(RotAcc2, 59)                                                               \
  X(HoistInpBroadcast, 60) //\

enum class Op {
#define X(op, code) op = code,
  CINNAMON_OPS
#undef X
};

inline bool isValidOp(Op op) {
  switch (op) {
#define X(op, code) case Op::op:
    CINNAMON_OPS
#undef X
    return true;
  default:
    return false;
  }
}

inline std::string getOpName(Op op) {
  switch (op) {
#define X(op, code)                                                            \
  case Op::op:                                                                 \
    return #op;
    CINNAMON_OPS
#undef X
  default:
    throw std::runtime_error("Invalid op");
  }
}

} // namespace Frontend
} // namespace Cinnamon

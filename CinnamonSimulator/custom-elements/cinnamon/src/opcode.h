// Copyright (c) Siddharth Jayashankar. All rights reserved.
#ifndef _H_SST_CINNAMON_OPCODE
#define _H_SST_CINNAMON_OPCODE

#include "sst/core/interfaces/stdMem.h"
#include <sst/core/component.h>

namespace SST {
namespace Cinnamon {

enum class CinnamonInstructionOpCode {
    LoadV,
    LoadS,
    Store,
    Spill,
    EvkGen,
    Dis,
    Rcv,
    Joi,
    Add,
    Sub,
    Neg,
    Mul,
    Div,
    Rot,
    Con,
    Ntt,
    Int,
    Mov,
    Pl1,
    SuD,
    Bci,
    BcW,
    BcR,
    Nop,
    Rsi,
    Rsv,
    Mod,
    NUM_OPCODES
};

std::string getOpCodeString(CinnamonInstructionOpCode opCode);

} // Namespace Cinnamon
} // Namespace SST

#endif //_H_SST_CINNAMON_OPCODE
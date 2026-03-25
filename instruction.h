#ifndef _SST_TUTORIAL_INSTRUCTION_H
#define _SST_TUTORIAL_INSTRUCTION_H

#include <cstdint>
#include <string>
#include <vector>

namespace Tutorial {

struct Instruction {
    enum class Opcode : uint32_t {
        PCIE_H2D = 0,
        PCIE_D2H = 1,
        HBM_LOAD = 2,
        HBM_STORE = 3,
        MOD_ADD = 4,
        MOD_SUB = 5,
        MOD_MUL = 6,
        MAC = 7,
        AUTOMORPHISM = 8,
        NTT_STAGE = 9,
        INTT_STAGE = 10,
        BARRIER = 11,
    };

    uint64_t id = 0;
    Opcode opcode = Opcode::MOD_MUL;

    uint64_t host_address = 0;
    uint64_t hbm_address = 0;
    uint64_t spm_address = 0;

    uint64_t src0_address = 0;
    uint64_t src1_address = 0;
    uint64_t dst_address = 0;
    uint64_t acc_address = 0;
    uint64_t twiddle_address = 0;

    uint64_t bytes = 0;
    uint32_t element_count = 0;
    uint32_t lane_count = 256;
    uint64_t modulus = 65537;

    uint32_t stage = 0;
    uint32_t poly_degree = 0;
    uint32_t rotation = 0;
    uint64_t dependency_mask = 0;
    bool barrier = false;

    std::string raw_text;

    static Opcode parseOpcode(const std::string& token);
    static const char* opcodeName(Opcode opcode);
    static bool isPcieMovementOpcode(Opcode opcode);
    static bool isHbmTransferOpcode(Opcode opcode);
    static bool isComputeOpcode(Opcode opcode);
    static bool isBarrierOpcode(Opcode opcode);
};

std::string trim(const std::string& text);
std::vector<std::string> split(const std::string& text, char delimiter);
uint64_t parseInteger(const std::string& value);
Instruction parseInstruction(
    uint64_t id,
    const std::string& text,
    uint32_t default_lanes,
    uint32_t default_poly_degree,
    uint32_t default_element_bytes);
std::vector<Instruction> parseProgram(
    const std::string& program_text,
    const std::string& program_file,
    uint32_t generated_instruction_count,
    uint64_t base_src_address,
    uint64_t base_dst_address,
    uint32_t default_lanes,
    uint64_t default_modulus,
    uint32_t default_poly_degree,
    uint32_t default_element_bytes);

} // namespace Tutorial

#endif

#include "instruction.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Tutorial {

namespace {

uint32_t ceilDivide(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0) {
        return 0;
    }
    return static_cast<uint32_t>((numerator + denominator - 1) / denominator);
}

std::string normalizeProgramText(const std::string& text)
{
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), ';', '\n');
    return normalized;
}

std::vector<Instruction> parseProgramFromText(
    const std::string& text,
    uint64_t& next_id,
    uint32_t default_lanes,
    uint32_t default_poly_degree,
    uint32_t default_element_bytes)
{
    std::vector<Instruction> program;
    std::stringstream stream(normalizeProgramText(text));
    std::string line;

    while (std::getline(stream, line)) {
        const std::size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        const std::string cleaned = trim(line);
        if (cleaned.empty()) {
            continue;
        }

        program.push_back(
            parseInstruction(
                next_id,
                cleaned,
                default_lanes,
                default_poly_degree,
                default_element_bytes));
        ++next_id;
    }

    return program;
}

uint64_t defaultBytesForInstruction(const Instruction& instruction, uint32_t default_element_bytes)
{
    if (instruction.bytes > 0) {
        return instruction.bytes;
    }

    const uint32_t bytes_per_element = std::max<uint32_t>(1, default_element_bytes);
    const uint32_t elements = std::max<uint32_t>(1, instruction.element_count);
    return static_cast<uint64_t>(elements) * bytes_per_element;
}

} // namespace

Instruction::Opcode Instruction::parseOpcode(const std::string& token)
{
    if (token == "PCIE_H2D") {
        return Opcode::PCIE_H2D;
    }
    if (token == "PCIE_D2H") {
        return Opcode::PCIE_D2H;
    }
    if (token == "HBM_LOAD" || token == "DMA_LOAD") {
        return Opcode::HBM_LOAD;
    }
    if (token == "HBM_STORE" || token == "DMA_STORE") {
        return Opcode::HBM_STORE;
    }
    if (token == "MOD_ADD") {
        return Opcode::MOD_ADD;
    }
    if (token == "MOD_SUB") {
        return Opcode::MOD_SUB;
    }
    if (token == "MOD_MUL") {
        return Opcode::MOD_MUL;
    }
    if (token == "MAC") {
        return Opcode::MAC;
    }
    if (token == "AUTOMORPHISM") {
        return Opcode::AUTOMORPHISM;
    }
    if (token == "NTT_STAGE") {
        return Opcode::NTT_STAGE;
    }
    if (token == "INTT_STAGE") {
        return Opcode::INTT_STAGE;
    }
    if (token == "BARRIER") {
        return Opcode::BARRIER;
    }

    throw std::invalid_argument("unsupported opcode: " + token);
}

const char* Instruction::opcodeName(Opcode opcode)
{
    switch (opcode) {
    case Opcode::PCIE_H2D:
        return "PCIE_H2D";
    case Opcode::PCIE_D2H:
        return "PCIE_D2H";
    case Opcode::HBM_LOAD:
        return "HBM_LOAD";
    case Opcode::HBM_STORE:
        return "HBM_STORE";
    case Opcode::MOD_ADD:
        return "MOD_ADD";
    case Opcode::MOD_SUB:
        return "MOD_SUB";
    case Opcode::MOD_MUL:
        return "MOD_MUL";
    case Opcode::MAC:
        return "MAC";
    case Opcode::AUTOMORPHISM:
        return "AUTOMORPHISM";
    case Opcode::NTT_STAGE:
        return "NTT_STAGE";
    case Opcode::INTT_STAGE:
        return "INTT_STAGE";
    case Opcode::BARRIER:
        return "BARRIER";
    }

    return "UNKNOWN";
}

bool Instruction::isPcieMovementOpcode(Opcode opcode)
{
    return opcode == Opcode::PCIE_H2D || opcode == Opcode::PCIE_D2H;
}

bool Instruction::isHbmTransferOpcode(Opcode opcode)
{
    return opcode == Opcode::HBM_LOAD || opcode == Opcode::HBM_STORE;
}

bool Instruction::isComputeOpcode(Opcode opcode)
{
    return opcode == Opcode::MOD_ADD || opcode == Opcode::MOD_SUB ||
           opcode == Opcode::MOD_MUL || opcode == Opcode::MAC ||
           opcode == Opcode::AUTOMORPHISM || opcode == Opcode::NTT_STAGE ||
           opcode == Opcode::INTT_STAGE;
}

bool Instruction::isBarrierOpcode(Opcode opcode)
{
    return opcode == Opcode::BARRIER;
}

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, char delimiter)
{
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;

    while (std::getline(stream, part, delimiter)) {
        parts.push_back(trim(part));
    }

    return parts;
}

uint64_t parseInteger(const std::string& value)
{
    return std::stoull(value, nullptr, 0);
}

Instruction parseInstruction(
    uint64_t id,
    const std::string& text,
    uint32_t default_lanes,
    uint32_t default_poly_degree,
    uint32_t default_element_bytes)
{
    Instruction instruction;
    instruction.id = id;
    instruction.lane_count = default_lanes;
    instruction.raw_text = trim(text);

    if (instruction.raw_text.empty()) {
        throw std::invalid_argument("instruction string is empty");
    }

    std::stringstream parser(instruction.raw_text);
    std::string opcode_token;
    parser >> opcode_token;
    instruction.opcode = Instruction::parseOpcode(opcode_token);

    std::string token;
    while (parser >> token) {
        const std::size_t separator = token.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = token.substr(0, separator);
        const std::string value = token.substr(separator + 1);

        if (key == "host" || key == "host_address") {
            instruction.host_address = parseInteger(value);
        } else if (key == "hbm" || key == "dram" || key == "hbm_address") {
            instruction.hbm_address = parseInteger(value);
        } else if (key == "spm" || key == "spm_address") {
            instruction.spm_address = parseInteger(value);
        } else if (key == "src" || key == "src0") {
            instruction.src0_address = parseInteger(value);
        } else if (key == "src1") {
            instruction.src1_address = parseInteger(value);
        } else if (key == "dst") {
            instruction.dst_address = parseInteger(value);
        } else if (key == "acc") {
            instruction.acc_address = parseInteger(value);
        } else if (key == "twiddle") {
            instruction.twiddle_address = parseInteger(value);
        } else if (key == "bytes") {
            instruction.bytes = parseInteger(value);
        } else if (key == "elements" || key == "element_count") {
            instruction.element_count = static_cast<uint32_t>(parseInteger(value));
        } else if (key == "lanes" || key == "lane_count") {
            instruction.lane_count = static_cast<uint32_t>(parseInteger(value));
        } else if (key == "modulus" || key == "mod") {
            instruction.modulus = parseInteger(value);
        } else if (key == "stage") {
            instruction.stage = static_cast<uint32_t>(parseInteger(value));
        } else if (key == "degree" || key == "poly_degree") {
            instruction.poly_degree = static_cast<uint32_t>(parseInteger(value));
        } else if (key == "rot" || key == "rotation" || key == "imm" || key == "immediate") {
            instruction.rotation = static_cast<uint32_t>(parseInteger(value));
        } else if (key == "dep" || key == "dependency" || key == "dependency_mask") {
            instruction.dependency_mask = parseInteger(value);
        }
    }

    if (instruction.lane_count == 0) {
        instruction.lane_count = 1;
    }

    if (instruction.element_count == 0) {
        if (instruction.bytes > 0) {
            instruction.element_count = ceilDivide(
                instruction.bytes,
                std::max<uint32_t>(1, default_element_bytes));
        } else if (instruction.opcode == Instruction::Opcode::NTT_STAGE ||
                   instruction.opcode == Instruction::Opcode::INTT_STAGE) {
            if (instruction.poly_degree > 0) {
                instruction.element_count = instruction.poly_degree;
            } else {
                instruction.element_count = std::max<uint32_t>(1, default_poly_degree);
            }
        } else {
            instruction.element_count = instruction.lane_count;
        }
    }

    if ((instruction.opcode == Instruction::Opcode::NTT_STAGE ||
         instruction.opcode == Instruction::Opcode::INTT_STAGE) &&
        instruction.poly_degree == 0) {
        instruction.poly_degree = std::max<uint32_t>(1, default_poly_degree);
    }

    if (instruction.bytes == 0) {
        instruction.bytes = defaultBytesForInstruction(instruction, default_element_bytes);
    }

    if (instruction.opcode == Instruction::Opcode::HBM_LOAD) {
        if (instruction.dst_address == 0 && instruction.spm_address != 0) {
            instruction.dst_address = instruction.spm_address;
        }
        if (instruction.spm_address == 0 && instruction.dst_address != 0) {
            instruction.spm_address = instruction.dst_address;
        }
    }

    if (instruction.opcode == Instruction::Opcode::HBM_STORE) {
        if (instruction.src0_address == 0 && instruction.spm_address != 0) {
            instruction.src0_address = instruction.spm_address;
        }
        if (instruction.spm_address == 0 && instruction.src0_address != 0) {
            instruction.spm_address = instruction.src0_address;
        }
    }

    if (instruction.opcode == Instruction::Opcode::AUTOMORPHISM) {
        if (instruction.spm_address == 0 && instruction.src0_address != 0) {
            instruction.spm_address = instruction.src0_address;
        }
    }

    instruction.barrier = (instruction.opcode == Instruction::Opcode::BARRIER);
    return instruction;
}

std::vector<Instruction> parseProgram(
    const std::string& program_text,
    const std::string& program_file,
    uint32_t generated_instruction_count,
    uint64_t base_src_address,
    uint64_t base_dst_address,
    uint32_t default_lanes,
    uint64_t default_modulus,
    uint32_t default_poly_degree,
    uint32_t default_element_bytes)
{
    std::vector<Instruction> program;
    uint64_t next_id = 0;

    if (!trim(program_file).empty()) {
        std::ifstream input(program_file);
        if (!input.is_open()) {
            throw std::runtime_error("failed to open program file: " + program_file);
        }

        std::stringstream buffer;
        buffer << input.rdbuf();
        std::vector<Instruction> parsed = parseProgramFromText(
            buffer.str(),
            next_id,
            default_lanes,
            default_poly_degree,
            default_element_bytes);
        program.insert(program.end(), parsed.begin(), parsed.end());
        if (!program.empty()) {
            return program;
        }
    }

    if (!trim(program_text).empty()) {
        std::vector<Instruction> parsed = parseProgramFromText(
            program_text,
            next_id,
            default_lanes,
            default_poly_degree,
            default_element_bytes);
        program.insert(program.end(), parsed.begin(), parsed.end());
        if (!program.empty()) {
            return program;
        }
    }

    for (uint32_t i = 0; i < generated_instruction_count; ++i) {
        const uint64_t host_base0 = base_src_address + static_cast<uint64_t>(i) * 0x20000;
        const uint64_t host_base1 = host_base0 + 0x8000;
        const uint64_t host_out = base_dst_address + static_cast<uint64_t>(i) * 0x20000;

        const uint64_t hbm_base0 = static_cast<uint64_t>(i) * 0x20000;
        const uint64_t hbm_base1 = hbm_base0 + 0x8000;
        const uint64_t hbm_out = hbm_base0 + 0x10000;

        const uint64_t spm_src0 = static_cast<uint64_t>(i) * 0x10000;
        const uint64_t spm_src1 = spm_src0 + 0x4000;
        const uint64_t spm_dst = spm_src0 + 0x8000;

        Instruction h2d0;
        h2d0.id = program.size();
        h2d0.opcode = Instruction::Opcode::PCIE_H2D;
        h2d0.host_address = host_base0;
        h2d0.hbm_address = hbm_base0;
        h2d0.bytes = default_lanes * static_cast<uint64_t>(std::max<uint32_t>(1, default_element_bytes));
        h2d0.element_count = default_lanes;
        h2d0.lane_count = default_lanes;
        h2d0.modulus = default_modulus;
        h2d0.raw_text = "PCIE_H2D host=" + std::to_string(h2d0.host_address) +
                        " hbm=" + std::to_string(h2d0.hbm_address) +
                        " bytes=" + std::to_string(h2d0.bytes);
        program.push_back(h2d0);

        Instruction h2d1 = h2d0;
        h2d1.id = program.size();
        h2d1.host_address = host_base1;
        h2d1.hbm_address = hbm_base1;
        h2d1.raw_text = "PCIE_H2D host=" + std::to_string(h2d1.host_address) +
                        " hbm=" + std::to_string(h2d1.hbm_address) +
                        " bytes=" + std::to_string(h2d1.bytes);
        program.push_back(h2d1);

        Instruction load0;
        load0.id = program.size();
        load0.opcode = Instruction::Opcode::HBM_LOAD;
        load0.hbm_address = hbm_base0;
        load0.spm_address = spm_src0;
        load0.dst_address = spm_src0;
        load0.bytes = h2d0.bytes;
        load0.element_count = default_lanes;
        load0.lane_count = default_lanes;
        load0.modulus = default_modulus;
        load0.raw_text = "HBM_LOAD hbm=" + std::to_string(load0.hbm_address) +
                         " spm=" + std::to_string(load0.spm_address) +
                         " elements=" + std::to_string(load0.element_count);
        program.push_back(load0);

        Instruction load1 = load0;
        load1.id = program.size();
        load1.hbm_address = hbm_base1;
        load1.spm_address = spm_src1;
        load1.dst_address = spm_src1;
        load1.raw_text = "HBM_LOAD hbm=" + std::to_string(load1.hbm_address) +
                         " spm=" + std::to_string(load1.spm_address) +
                         " elements=" + std::to_string(load1.element_count);
        program.push_back(load1);

        Instruction mul;
        mul.id = program.size();
        mul.opcode = Instruction::Opcode::MOD_MUL;
        mul.src0_address = spm_src0;
        mul.src1_address = spm_src1;
        mul.dst_address = spm_dst;
        mul.bytes = h2d0.bytes;
        mul.element_count = default_lanes;
        mul.lane_count = default_lanes;
        mul.modulus = default_modulus;
        mul.raw_text = "MOD_MUL src0=" + std::to_string(mul.src0_address) +
                       " src1=" + std::to_string(mul.src1_address) +
                       " dst=" + std::to_string(mul.dst_address) +
                       " elements=" + std::to_string(mul.element_count);
        program.push_back(mul);

        Instruction store;
        store.id = program.size();
        store.opcode = Instruction::Opcode::HBM_STORE;
        store.spm_address = spm_dst;
        store.src0_address = spm_dst;
        store.hbm_address = hbm_out;
        store.bytes = h2d0.bytes;
        store.element_count = default_lanes;
        store.lane_count = default_lanes;
        store.modulus = default_modulus;
        store.raw_text = "HBM_STORE spm=" + std::to_string(store.spm_address) +
                         " hbm=" + std::to_string(store.hbm_address) +
                         " elements=" + std::to_string(store.element_count);
        program.push_back(store);

        Instruction d2h;
        d2h.id = program.size();
        d2h.opcode = Instruction::Opcode::PCIE_D2H;
        d2h.host_address = host_out;
        d2h.hbm_address = hbm_out;
        d2h.bytes = h2d0.bytes;
        d2h.element_count = default_lanes;
        d2h.lane_count = default_lanes;
        d2h.modulus = default_modulus;
        d2h.raw_text = "PCIE_D2H hbm=" + std::to_string(d2h.hbm_address) +
                       " host=" + std::to_string(d2h.host_address) +
                       " bytes=" + std::to_string(d2h.bytes);
        program.push_back(d2h);

        Instruction barrier;
        barrier.id = program.size();
        barrier.opcode = Instruction::Opcode::BARRIER;
        barrier.barrier = true;
        barrier.raw_text = "BARRIER";
        program.push_back(barrier);
    }

    return program;
}

} // namespace Tutorial

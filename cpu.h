#ifndef _SST_TUTORIAL_CPU_H
#define _SST_TUTORIAL_CPU_H

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include <sst/core/component.h>
#include <sst/core/output.h>

#include "context.h"
#include "instruction.h"

namespace Tutorial {

class AcceleratorEvent;

class CPU : public SST::Component {
public:
    SST_ELI_REGISTER_COMPONENT(
        CPU,
        "tutorial",
        "CPU",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "CPU front-end that emits accelerator instructions toward PCIe control path",
        COMPONENT_CATEGORY_UNCATEGORIZED)

    SST_ELI_DOCUMENT_PARAMS(
        {"clock", "Clock frequency for issuing instructions", "2GHz"},
        {"issue_width", "Number of instructions issued per cycle", "1"},
        {"max_inflight", "Maximum number of outstanding instructions", "32"},
        {"default_lanes", "Default active lane count for generated instructions", "256"},
        {"default_modulus", "Default modulus used for generated modular arithmetic instructions", "65537"},
        {"program", "Program text with semicolon/newline separated instructions", ""},
        {"program_file", "Optional workload file path. If provided and readable, it overrides `program`", ""},
        {"instructions_to_generate", "How many fallback kernels to auto-generate when program is empty", "1"},
        {"default_src_address", "Base source address used for generated instructions", "0x10000000"},
        {"default_dst_address", "Base destination address used for generated instructions", "0x10080000"},
        {"context.n", "Crypto polynomial degree N", "8192"},
        {"context.data_width_bits", "Ciphertext limb width in bits", "32"},
        {"context.limbs", "Number of limbs per element", "1"},
        {"verbose", "Verbose log level (0 disables logs)", "0"})

    SST_ELI_DOCUMENT_PORTS(
        {"ctrl", "Bidirectional control path to PCIe", {"Tutorial::AcceleratorEvent"}})

    CPU(SST::ComponentId_t id, SST::Params& params);
    ~CPU() override = default;

private:
    void handleControlEvent(SST::Event* event);
    bool tick(SST::Cycle_t cycle);
    void signalCompletionIfDone();

    SST::Link* ctrl_link_;
    std::vector<Instruction> program_;
    std::size_t next_instruction_;

    uint32_t issue_width_;
    uint32_t max_inflight_;
    uint32_t default_lanes_;
    CryptoContext context_;

    uint64_t next_txn_id_;
    uint64_t issued_count_;
    uint64_t inflight_count_;
    uint64_t completed_count_;

    bool waiting_for_barrier_;
    uint64_t barrier_seq_;
    std::unordered_set<uint64_t> completed_instruction_ids_;

    bool completion_signaled_;
    SST::Output output_;
};

} // namespace Tutorial

#endif

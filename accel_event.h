#ifndef _SST_TUTORIAL_ACCEL_EVENT_H
#define _SST_TUTORIAL_ACCEL_EVENT_H

#include <cstdint>
#include <string>

#include <sst/core/event.h>

#include "instruction.h"

namespace Tutorial {

enum class EventType : uint32_t {
    Command = 0,
    MemRequest = 1,
    MemResponse = 2,
    Completion = 3,
};

enum class ComponentType : uint32_t {
    Unknown = 0,
    CPU = 1,
    PCIe = 2,
    HBM = 3,
    ComputeUnit = 4,
};

enum class EventStatus : uint32_t {
    Ok = 0,
    WaitData = 1,
    InvalidAddress = 2,
    ResourceBusy = 3,
    Error = 4,
};

class AcceleratorEvent : public SST::Event {
public:
    uint64_t txn_id = 0;
    uint64_t parent_instruction_id = 0;
    uint64_t seq_no = 0;

    uint32_t event_type = static_cast<uint32_t>(EventType::Command);
    uint32_t src_component = static_cast<uint32_t>(ComponentType::Unknown);
    uint32_t dst_component = static_cast<uint32_t>(ComponentType::Unknown);
    uint32_t status = static_cast<uint32_t>(EventStatus::Ok);

    bool completion = false;

    uint32_t opcode = static_cast<uint32_t>(Instruction::Opcode::MOD_MUL);
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
    uint32_t lane_count = 0;
    uint64_t modulus = 0;

    uint32_t stage = 0;
    uint32_t poly_degree = 0;
    uint32_t rotation = 0;
    uint64_t dependency_mask = 0;
    bool barrier = false;

    uint64_t enqueue_cycle = 0;
    uint64_t ready_cycle_hint = 0;
    std::string raw_text;

    AcceleratorEvent() = default;

    explicit AcceleratorEvent(const Instruction& instruction, uint64_t transaction_id = 0) :
        txn_id(transaction_id),
        parent_instruction_id(instruction.id),
        seq_no(instruction.id),
        event_type(static_cast<uint32_t>(EventType::Command)),
        src_component(static_cast<uint32_t>(ComponentType::CPU)),
        dst_component(static_cast<uint32_t>(ComponentType::PCIe)),
        status(static_cast<uint32_t>(EventStatus::Ok)),
        completion(false),
        opcode(static_cast<uint32_t>(instruction.opcode)),
        host_address(instruction.host_address),
        hbm_address(instruction.hbm_address),
        spm_address(instruction.spm_address),
        src0_address(instruction.src0_address),
        src1_address(instruction.src1_address),
        dst_address(instruction.dst_address),
        acc_address(instruction.acc_address),
        twiddle_address(instruction.twiddle_address),
        bytes(instruction.bytes),
        element_count(instruction.element_count),
        lane_count(instruction.lane_count),
        modulus(instruction.modulus),
        stage(instruction.stage),
        poly_degree(instruction.poly_degree),
        rotation(instruction.rotation),
        dependency_mask(instruction.dependency_mask),
        barrier(instruction.barrier),
        enqueue_cycle(0),
        ready_cycle_hint(0),
        raw_text(instruction.raw_text)
    {
    }

    Instruction toInstruction() const
    {
        Instruction instruction;
        instruction.id = parent_instruction_id;
        instruction.opcode = static_cast<Instruction::Opcode>(opcode);
        instruction.host_address = host_address;
        instruction.hbm_address = hbm_address;
        instruction.spm_address = spm_address;
        instruction.src0_address = src0_address;
        instruction.src1_address = src1_address;
        instruction.dst_address = dst_address;
        instruction.acc_address = acc_address;
        instruction.twiddle_address = twiddle_address;
        instruction.bytes = bytes;
        instruction.element_count = element_count;
        instruction.lane_count = lane_count;
        instruction.modulus = modulus;
        instruction.stage = stage;
        instruction.poly_degree = poly_degree;
        instruction.rotation = rotation;
        instruction.dependency_mask = dependency_mask;
        instruction.barrier = barrier;
        instruction.raw_text = raw_text;
        return instruction;
    }

    void serialize_order(SST::Core::Serialization::serializer& ser) override
    {
        SST::Event::serialize_order(ser);
        SST_SER(txn_id);
        SST_SER(parent_instruction_id);
        SST_SER(seq_no);
        SST_SER(event_type);
        SST_SER(src_component);
        SST_SER(dst_component);
        SST_SER(status);
        SST_SER(completion);

        SST_SER(opcode);
        SST_SER(host_address);
        SST_SER(hbm_address);
        SST_SER(spm_address);
        SST_SER(src0_address);
        SST_SER(src1_address);
        SST_SER(dst_address);
        SST_SER(acc_address);
        SST_SER(twiddle_address);

        SST_SER(bytes);
        SST_SER(element_count);
        SST_SER(lane_count);
        SST_SER(modulus);

        SST_SER(stage);
        SST_SER(poly_degree);
        SST_SER(rotation);
        SST_SER(dependency_mask);
        SST_SER(barrier);

        SST_SER(enqueue_cycle);
        SST_SER(ready_cycle_hint);
        SST_SER(raw_text);
    }

    ImplementSerializable(Tutorial::AcceleratorEvent);
};

} // namespace Tutorial

#endif

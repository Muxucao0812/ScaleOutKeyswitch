#include <sst/core/sst_config.h>

#include "pcie.h"

#include <algorithm>

#include "accel_event.h"

namespace {

uint64_t ceilDivide(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0) {
        return 0;
    }
    return (numerator + denominator - 1) / denominator;
}

} // namespace

namespace Tutorial {

PCIe::PCIe(SST::ComponentId_t id, SST::Params& params) :
    SST::Component(id),
    cpu_ctrl_link_(nullptr),
    fabric_link_(nullptr),
    command_active_valid_(false),
    dma_active_valid_(false),
    completion_active_valid_(false),
    doorbell_setup_cycles_(params.find<uint32_t>("doorbell_setup_cycles", 4)),
    dma_setup_cycles_(params.find<uint32_t>("dma_setup_cycles", 12)),
    protocol_overhead_cycles_(params.find<uint32_t>("protocol_overhead_cycles", 4)),
    effective_bytes_per_cycle_(params.find<uint32_t>("effective_bytes_per_cycle", 64)),
    completion_return_cycles_(params.find<uint32_t>("completion_return_cycles", 2))
{
    const uint32_t verbose = params.find<uint32_t>("verbose", 0);
    output_.init("[PCIe] ", verbose, 0, SST::Output::STDOUT);

    cpu_ctrl_link_ = configureLink(
        "cpu_ctrl",
        new SST::Event::Handler2<PCIe, &PCIe::handleCpuEvent>(this));
    sst_assert(cpu_ctrl_link_ != nullptr, CALL_INFO, -1, "PCIe requires the 'cpu_ctrl' port to be connected\n");

    fabric_link_ = configureLink(
        "fabric",
        new SST::Event::Handler2<PCIe, &PCIe::handleFabricEvent>(this));
    sst_assert(fabric_link_ != nullptr, CALL_INFO, -1, "PCIe requires the 'fabric' port to be connected\n");

    const std::string clock = params.find<std::string>("clock", "1.0GHz");
    registerClock(clock, new SST::Clock::Handler2<PCIe, &PCIe::tick>(this));
}

void PCIe::handleCpuEvent(SST::Event* event)
{
    AcceleratorEvent* request = dynamic_cast<AcceleratorEvent*>(event);
    sst_assert(request != nullptr, CALL_INFO, -1, "PCIe received unexpected event type from CPU\n");

    request->src_component = static_cast<uint32_t>(ComponentType::PCIe);
    request->dst_component = static_cast<uint32_t>(ComponentType::HBM);
    request->enqueue_cycle = getCurrentSimCycle();

    const Instruction::Opcode opcode = static_cast<Instruction::Opcode>(request->opcode);
    if (Instruction::isPcieMovementOpcode(opcode)) {
        request->event_type = static_cast<uint32_t>(EventType::MemRequest);
        dma_waiting_queue_.push_back({request, estimateDmaCycles(request->bytes)});
        output_.verbose(
            CALL_INFO,
            1,
            0,
            "Enqueue DMA: inst=%llu txn=%llu opcode=%s bytes=%llu dma_q=%zu\n",
            static_cast<unsigned long long>(request->parent_instruction_id),
            static_cast<unsigned long long>(request->txn_id),
            Instruction::opcodeName(opcode),
            static_cast<unsigned long long>(request->bytes),
            dma_waiting_queue_.size());
        return;
    }

    request->event_type = static_cast<uint32_t>(EventType::Command);
    command_waiting_queue_.push_back({request, std::max<uint32_t>(1, doorbell_setup_cycles_)});
    output_.verbose(
        CALL_INFO,
        1,
        0,
        "Enqueue CMD: inst=%llu txn=%llu opcode=%s cmd_q=%zu\n",
        static_cast<unsigned long long>(request->parent_instruction_id),
        static_cast<unsigned long long>(request->txn_id),
        Instruction::opcodeName(opcode),
        command_waiting_queue_.size());
}

void PCIe::handleFabricEvent(SST::Event* event)
{
    AcceleratorEvent* response = dynamic_cast<AcceleratorEvent*>(event);
    sst_assert(response != nullptr, CALL_INFO, -1, "PCIe received unexpected event type from fabric\n");

    response->event_type = static_cast<uint32_t>(EventType::Completion);
    response->src_component = static_cast<uint32_t>(ComponentType::PCIe);
    response->dst_component = static_cast<uint32_t>(ComponentType::CPU);
    response->completion = true;

    completion_waiting_queue_.push_back({response, std::max<uint32_t>(1, completion_return_cycles_)});
    output_.verbose(
        CALL_INFO,
        1,
        0,
        "Completion queued to CPU: inst=%llu txn=%llu status=%u completion_q=%zu\n",
        static_cast<unsigned long long>(response->parent_instruction_id),
        static_cast<unsigned long long>(response->txn_id),
        response->status,
        completion_waiting_queue_.size());
}

bool PCIe::tick(SST::Cycle_t cycle)
{
    (void)cycle;

    tryLaunchCommand();
    tryLaunchDma();
    tryLaunchCompletion();

    return false;
}

void PCIe::tryLaunchCommand()
{
    if (!command_active_valid_ && !command_waiting_queue_.empty()) {
        command_active_ = command_waiting_queue_.front();
        command_waiting_queue_.pop_front();
        command_active_valid_ = true;
    }

    if (!command_active_valid_) {
        return;
    }

    if (command_active_.remaining_cycles > 0) {
        --command_active_.remaining_cycles;
    }

    if (command_active_.remaining_cycles == 0) {
        command_active_.event->ready_cycle_hint = getCurrentSimCycle();
        output_.verbose(
            CALL_INFO,
            2,
            0,
            "Dispatch CMD to HBM: inst=%llu txn=%llu opcode=%s\n",
            static_cast<unsigned long long>(command_active_.event->parent_instruction_id),
            static_cast<unsigned long long>(command_active_.event->txn_id),
            Instruction::opcodeName(static_cast<Instruction::Opcode>(command_active_.event->opcode)));
        fabric_link_->send(command_active_.event);
        command_active_valid_ = false;
    }
}

void PCIe::tryLaunchDma()
{
    if (!dma_active_valid_ && !dma_waiting_queue_.empty()) {
        dma_active_ = dma_waiting_queue_.front();
        dma_waiting_queue_.pop_front();
        dma_active_valid_ = true;
    }

    if (!dma_active_valid_) {
        return;
    }

    if (dma_active_.remaining_cycles > 0) {
        --dma_active_.remaining_cycles;
    }

    if (dma_active_.remaining_cycles == 0) {
        dma_active_.event->ready_cycle_hint = getCurrentSimCycle();
        output_.verbose(
            CALL_INFO,
            2,
            0,
            "Dispatch DMA to HBM: inst=%llu txn=%llu opcode=%s bytes=%llu\n",
            static_cast<unsigned long long>(dma_active_.event->parent_instruction_id),
            static_cast<unsigned long long>(dma_active_.event->txn_id),
            Instruction::opcodeName(static_cast<Instruction::Opcode>(dma_active_.event->opcode)),
            static_cast<unsigned long long>(dma_active_.event->bytes));
        fabric_link_->send(dma_active_.event);
        dma_active_valid_ = false;
    }
}

void PCIe::tryLaunchCompletion()
{
    if (!completion_active_valid_ && !completion_waiting_queue_.empty()) {
        completion_active_ = completion_waiting_queue_.front();
        completion_waiting_queue_.pop_front();
        completion_active_valid_ = true;
    }

    if (!completion_active_valid_) {
        return;
    }

    if (completion_active_.remaining_cycles > 0) {
        --completion_active_.remaining_cycles;
    }

    if (completion_active_.remaining_cycles == 0) {
        output_.verbose(
            CALL_INFO,
            2,
            0,
            "Return completion to CPU: inst=%llu txn=%llu status=%u\n",
            static_cast<unsigned long long>(completion_active_.event->parent_instruction_id),
            static_cast<unsigned long long>(completion_active_.event->txn_id),
            completion_active_.event->status);
        cpu_ctrl_link_->send(completion_active_.event);
        completion_active_valid_ = false;
    }
}

uint64_t PCIe::estimateDmaCycles(uint64_t bytes) const
{
    const uint64_t payload_cycles = ceilDivide(bytes, std::max<uint32_t>(1, effective_bytes_per_cycle_));
    return static_cast<uint64_t>(std::max<uint32_t>(1, dma_setup_cycles_)) +
           static_cast<uint64_t>(std::max<uint32_t>(1, protocol_overhead_cycles_)) +
           std::max<uint64_t>(1, payload_cycles);
}

} // namespace Tutorial

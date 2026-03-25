#include <sst/core/sst_config.h>

#include "cpu.h"

#include <exception>
#include <string>

#include "accel_event.h"

namespace Tutorial {

CPU::CPU(SST::ComponentId_t id, SST::Params& params) :
    SST::Component(id),
    ctrl_link_(nullptr),
    next_instruction_(0),
    issue_width_(params.find<uint32_t>("issue_width", 1)),
    max_inflight_(params.find<uint32_t>("max_inflight", 32)),
    default_lanes_(params.find<uint32_t>("default_lanes", 256)),
    context_(loadCryptoContext(params, 8192, 32, 1)),
    next_txn_id_(1),
    issued_count_(0),
    inflight_count_(0),
    completed_count_(0),
    waiting_for_barrier_(false),
    barrier_seq_(0),
    completion_signaled_(false)
{
    const uint32_t verbose = params.find<uint32_t>("verbose", 0);
    output_.init("[CPU] ", verbose, 0, SST::Output::STDOUT);

    ctrl_link_ = configureLink(
        "ctrl",
        new SST::Event::Handler2<CPU, &CPU::handleControlEvent>(this));
    sst_assert(ctrl_link_ != nullptr, CALL_INFO, -1, "CPU requires the 'ctrl' port to be connected\n");

    const std::string program_text = params.find<std::string>("program", "");
    const std::string program_file = params.find<std::string>("program_file", "");
    const uint32_t generated_count = params.find<uint32_t>("instructions_to_generate", 1);
    const uint64_t base_src_address = params.find<uint64_t>("default_src_address", 0x10000000);
    const uint64_t base_dst_address = params.find<uint64_t>("default_dst_address", 0x10080000);
    const uint64_t default_modulus = params.find<uint64_t>("default_modulus", 65537);
    const uint32_t default_element_bytes = static_cast<uint32_t>(context_.elementBytes());

    try {
        program_ = parseProgram(
            program_text,
            program_file,
            generated_count,
            base_src_address,
            base_dst_address,
            default_lanes_,
            default_modulus,
            context_.poly_degree_n,
            default_element_bytes);
    } catch (const std::exception& ex) {
        sst_assert(false, CALL_INFO, -1, "Failed to parse CPU program: %s\n", ex.what());
    }

    output_.verbose(
        CALL_INFO,
        1,
        0,
        "Loaded %zu instructions (issue_width=%u, max_inflight=%u)\n",
        program_.size(),
        issue_width_,
        max_inflight_);
    output_.verbose(
        CALL_INFO,
        1,
        0,
        "Crypto context: N=%u data_width_bits=%u limbs=%u element_bytes=%llu\n",
        context_.poly_degree_n,
        context_.data_width_bits,
        context_.limbs,
        static_cast<unsigned long long>(context_.elementBytes()));

    const std::string clock = params.find<std::string>("clock", "2GHz");
    registerClock(clock, new SST::Clock::Handler2<CPU, &CPU::tick>(this));

    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();
}

void CPU::handleControlEvent(SST::Event* event)
{
    AcceleratorEvent* response = dynamic_cast<AcceleratorEvent*>(event);
    sst_assert(response != nullptr, CALL_INFO, -1, "CPU received an unexpected event type on 'ctrl'\n");

    if (inflight_count_ > 0) {
        --inflight_count_;
    }

    if (response->completion) {
        ++completed_count_;
        completed_instruction_ids_.insert(response->parent_instruction_id);
        output_.verbose(
            CALL_INFO,
            1,
            0,
            "Completion: inst=%llu txn=%llu status=%u inflight=%llu\n",
            static_cast<unsigned long long>(response->parent_instruction_id),
            static_cast<unsigned long long>(response->txn_id),
            response->status,
            static_cast<unsigned long long>(inflight_count_));
    }

    if (waiting_for_barrier_ && completed_instruction_ids_.find(barrier_seq_) != completed_instruction_ids_.end()) {
        waiting_for_barrier_ = false;
        output_.verbose(
            CALL_INFO,
            1,
            0,
            "Barrier %llu completed, resume issuing\n",
            static_cast<unsigned long long>(barrier_seq_));
    }

    delete response;
    signalCompletionIfDone();
}

bool CPU::tick(SST::Cycle_t cycle)
{
    (void)cycle;

    if (completion_signaled_) {
        return true;
    }

    if (!waiting_for_barrier_) {
        uint32_t issue_budget = issue_width_;
        while (issue_budget > 0 && next_instruction_ < program_.size() && inflight_count_ < max_inflight_) {
            const Instruction& instruction = program_[next_instruction_];
            AcceleratorEvent* request = new AcceleratorEvent(instruction, next_txn_id_++);
            request->event_type = static_cast<uint32_t>(EventType::Command);
            request->src_component = static_cast<uint32_t>(ComponentType::CPU);
            request->dst_component = static_cast<uint32_t>(ComponentType::PCIe);
            request->status = static_cast<uint32_t>(EventStatus::Ok);
            request->enqueue_cycle = getCurrentSimCycle();
            request->seq_no = instruction.id;
            request->completion = false;

            ctrl_link_->send(request);
            output_.verbose(
                CALL_INFO,
                1,
                0,
                "Issue: inst=%llu txn=%llu opcode=%s inflight=%llu\n",
                static_cast<unsigned long long>(instruction.id),
                static_cast<unsigned long long>(request->txn_id),
                Instruction::opcodeName(instruction.opcode),
                static_cast<unsigned long long>(inflight_count_ + 1));

            ++next_instruction_;
            ++issued_count_;
            ++inflight_count_;
            --issue_budget;

            if (instruction.opcode == Instruction::Opcode::BARRIER) {
                waiting_for_barrier_ = true;
                barrier_seq_ = instruction.id;
                output_.verbose(
                    CALL_INFO,
                    1,
                    0,
                    "Barrier issued: inst=%llu (CPU issue paused until completion)\n",
                    static_cast<unsigned long long>(barrier_seq_));
                break;
            }
        }
    }

    signalCompletionIfDone();
    return completion_signaled_;
}

void CPU::signalCompletionIfDone()
{
    if (completion_signaled_) {
        return;
    }

    const bool no_more_to_issue = next_instruction_ >= program_.size();
    const bool no_more_inflight = inflight_count_ == 0;
    if (no_more_to_issue && no_more_inflight) {
        completion_signaled_ = true;
        output_.verbose(
            CALL_INFO,
            1,
            0,
            "Program done: issued=%llu completed=%llu\n",
            static_cast<unsigned long long>(issued_count_),
            static_cast<unsigned long long>(completed_count_));
        primaryComponentOKToEndSim();
    }
}

} // namespace Tutorial

// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <assert.h>
#include <deque>
#include <iostream>
#include <list>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#include "cinnamon/backend/isa_instruction.h"
#include "cinnamon/backend/limb_instruction.h"

namespace Cinnamon {
namespace Backend {

std::map<TermIndexType, std::set<LimbIndexType>> io_limbs;

class RegisterAllocator {

  using LimbMapType = LimbMap<Register>;
  using RegisterType = Register;
  using FreeListType = std::deque<RegisterType>;
  using BCUFreeListType = std::deque<BaseConversionUnit>;

  std::list<std::shared_ptr<ISAInstruction>> register_instructions;
  using InstructionIterator =
      std::list<std::shared_ptr<ISAInstruction>>::iterator;
  LimbMapType limb_map_table;
  RegisterMapType<InstructionIterator> register_last_write;
  RegisterMapType<InstructionIterator> register_last_read;
  RegisterMapType<InstructionIterator> register_last_use;
  FreeListType vector_free_list;
  FreeListType scalar_free_list;

  bool use_lru = false;
  bool use_dead_value_eviction = true;

  BCUFreeListType bcu_free_list;
  std::unordered_map<TermIndexType, BaseConversionUnit>
      base_conversion_map_table;

  std::unordered_map<TermIndexType, PolynomialRegisterGroup>
      polynomial_map_table;

  using LimbInstructionVec = std::vector<std::shared_ptr<LimbInstruction>>;

  uint64_t instructions_;

  std::map<TermIndexType, std::set<LimbIndexType>> io_limbs_local;

public:
  RegisterAllocator(uint64_t NUM_REGS) : instructions_(0) {
    // uint64_t NUM_REGS = 1024;
    uint64_t NUM_SCALAR_REGS = 32;
    uint64_t NUM_BCU = 2;
    for (auto i = 0; i < NUM_REGS; i++) {
      vector_free_list.push_back(Register(i, Register::RegType::Vector));
    }
    for (auto i = 0; i < NUM_SCALAR_REGS; i++) {
      scalar_free_list.push_back(Register(i, Register::RegType::Scalar));
    }
    for (auto i = 0; i < NUM_BCU; i++) {
      bcu_free_list.push_back(BaseConversionUnit(i));
    }
  }

  void evict(Register::RegType evict_reg_type) {
    if (evict_reg_type == Register::RegType::Scalar) {
      return evict_scalar();
    } else if (evict_reg_type == Register::RegType::Vector) {
      if (!use_lru) {
        return evict_vector();
      } else {
        return evict_vector_lru();
      }
    }

    throw std::invalid_argument("Invalid Register Type");
  }

  void evict_scalar() {

    Register::RegType evict_reg_type = Register::RegType::Scalar;
    uint64_t max_next_use = 0;
    Limb evict_limb;
    RegisterType evict_reg;
    for (auto &i : limb_map_table) {
      if (i.second.reg_type() != evict_reg_type) {
        continue;
      }
      assert(!i.first.is_dead());
      auto next_use = i.first.next_use();
      if (next_use > max_next_use) {
        max_next_use = next_use;
        evict_limb = i.first;
        evict_reg = i.second;
      }
    }
    assert(evict_reg.reg_type() != Register::RegType::None);
    limb_map_table.erase(evict_limb);
    CL_LOG("Evicting: {} with next use : {}", evict_limb, max_next_use);
    if (!evict_limb.is_input() && !evict_limb.is_output()) {
      auto it = register_last_write[evict_reg];
      // TODO: Add check to delete redundant loads
      evict_limb.set_dead(false);
      auto store_instruction = std::make_shared<StoreISAInstruction>(
          evict_limb, evict_reg, true /*is spill*/);
      CL_LOG("Inserting spill instruction: {}", store_instruction->ppOp());
      it = register_instructions.insert(std::next(it), store_instruction);
      register_last_write[evict_reg] = it;
      register_last_use[evict_reg] = it;
    }
    assert(evict_reg_type == evict_reg.reg_type());
    if (evict_reg_type == Register::RegType::Vector) {
      vector_free_list.push_back(evict_reg);
    } else if (evict_reg_type == Register::RegType::Scalar) {
      scalar_free_list.push_back(evict_reg);
    } else {
      assert(0);
    }
  }

  void evict_vector() {
    Register::RegType evict_reg_type = Register::RegType::Vector;
    uint64_t max_evg_next_use = 0;
    Limb evict_evg_candidate;
    RegisterType evict_evg_reg;
    uint64_t max_input_next_use = 0;
    Limb evict_input_candidate;
    RegisterType evict_input_reg;
    uint64_t max_next_use = 0;
    Limb evict_limb_candidate;
    RegisterType evict_limb_reg;
    for (auto &i : limb_map_table) {
      assert(!i.first.is_dead());
      if (i.second.reg_type() != evict_reg_type) {
        continue;
      }
      auto next_use = i.first.next_use();
      if (next_use > max_next_use) {
        max_next_use = next_use;
        evict_limb_candidate = i.first;
        evict_limb_reg = i.second;
      }
      if (i.first.is_eval_key() && i.first.eval_key_poly_num() == 1 &&
          next_use > max_evg_next_use) {
        max_evg_next_use = next_use;
        evict_evg_candidate = i.first;
        evict_evg_reg = i.second;
      }
      if (i.first.is_input() && next_use > max_input_next_use) {
        max_input_next_use = next_use;
        evict_input_candidate = i.first;
        evict_input_reg = i.second;
      }
    }

    RegisterType evict_reg;
    Limb evict_limb;

    uint64_t INPUT_WEIGHT = 3;
    uint64_t EVG_WEIGHT = 50;
    uint64_t LIMB_WEIGHT = 1;

    if (max_evg_next_use > 0) {
      max_evg_next_use -= instructions_;
    }
    if (max_input_next_use > 0) {
      max_input_next_use -= instructions_;
    }
    max_next_use -= instructions_;
    if (max_evg_next_use * EVG_WEIGHT > max_input_next_use * INPUT_WEIGHT &&
        max_evg_next_use * EVG_WEIGHT > max_next_use * LIMB_WEIGHT) {
      evict_limb = evict_evg_candidate;
      evict_reg = evict_evg_reg;
    } else if (max_input_next_use * INPUT_WEIGHT > max_next_use * LIMB_WEIGHT) {
      evict_limb = evict_input_candidate;
      evict_reg = evict_input_reg;
    } else {
      evict_limb = evict_limb_candidate;
      evict_reg = evict_limb_reg;
    }

    assert(evict_reg.reg_type() != Register::RegType::None);
    limb_map_table.erase(evict_limb);
    // CL_LOG("Evicting: {} with next use : {}",evict_limb, max_next_use);
    CL_LOG("Evicting: {} with next use : {}", evict_limb, max_next_use);
    if (!evict_limb.is_input() && !evict_limb.is_output()) {
      auto it = register_last_write[evict_reg];
      // TODO: Add check to delete redundant loads
      evict_limb.set_dead(false);
      auto store_instruction = std::make_shared<StoreISAInstruction>(
          evict_limb, evict_reg, true /*is spill*/);
      CL_LOG("Inserting spill instruction: {}", store_instruction->ppOp());
      it = register_instructions.insert(std::next(it), store_instruction);
      register_last_write[evict_reg] = it;
      register_last_use[evict_reg] = it;
    }
    assert(evict_reg_type == evict_reg.reg_type());
    if (evict_reg_type == Register::RegType::Vector) {
      vector_free_list.push_back(evict_reg);
    } else if (evict_reg_type == Register::RegType::Scalar) {
      scalar_free_list.push_back(evict_reg);
    } else {
      assert(0);
    }

    // TODO: Check if the last use has to updated. I.e.
  }

  void evict_vector_lru() {
    Register::RegType evict_reg_type = Register::RegType::Vector;
    uint64_t min_last_use = -1;
    Limb evict_limb_candidate;
    RegisterType evict_limb_reg;
    for (auto &i : limb_map_table) {
      // assert(!i.first.is_dead());
      if (i.second.reg_type() != evict_reg_type) {
        continue;
      }
      if (i.first.next_use() == instructions_) {
        continue;
      }
      auto last_use = i.first.last_use();
      // auto last_use = i.first.next_use();
      if (last_use < min_last_use) {
        min_last_use = last_use;
        evict_limb_candidate = i.first;
        evict_limb_reg = i.second;
      }
    }

    RegisterType evict_reg = evict_limb_reg;
    Limb evict_limb = evict_limb_candidate;

    assert(evict_reg.reg_type() != Register::RegType::None);
    limb_map_table.erase(evict_limb);
    // CL_LOG("Evicting: {} with next use : {}",evict_limb, max_next_use);
    CL_LOG2("Evicting: {} with last use : {} and next use {}", evict_limb,
            min_last_use, evict_limb.next_use());
    if (!evict_limb.is_input() && !evict_limb.is_output() &&
        !evict_limb.is_dead()) {
      // auto it = register_last_write[evict_reg];
      auto store_instruction = std::make_shared<StoreISAInstruction>(
          evict_limb, evict_reg, true /*is spill*/);
      CL_LOG2("Inserting spill instruction: {}", store_instruction->ppOp());
      register_instructions.push_back(store_instruction);
      auto it = register_instructions.end();
      --it;
      register_last_write[evict_reg] = it;
      register_last_use[evict_reg] = it;
      // it = register_instructions.insert(
      //     std::next(it), store_instruction);
      // register_last_write[evict_reg] = it;
      // register_last_use[evict_reg] = it;
    }
    assert(evict_reg_type == evict_reg.reg_type());
    if (evict_reg_type == Register::RegType::Vector) {
      vector_free_list.push_back(evict_reg);
    } else if (evict_reg_type == Register::RegType::Scalar) {
      scalar_free_list.push_back(evict_reg);
    } else {
      assert(0);
    }
  }

  void add_reg_to_free_list(Limb *limb, Register reg) {

    if (reg.reg_type() == Register::RegType::Vector) {
      if (use_lru && !use_dead_value_eviction) {
        limb_map_table[*limb] = reg;
        return;
      }
      vector_free_list.push_back(reg);
    } else if (reg.reg_type() == Register::RegType::Scalar) {
      scalar_free_list.push_back(reg);
    } else {
      assert(0);
    }
  }

  void process_instruction(std::shared_ptr<LimbInstruction> &instruction) {

    CL_LOG("Processing Instruction {}", instruction->ppOp());

    for (auto src : instruction->srcs()) {
      auto base = instruction->limb_idx();
      if (src->is_bcor()) {
        continue;
      }
      if (limb_map_table.find(*src) == limb_map_table.end()) {
        FreeListType *free_list = nullptr;
        Register::RegType evict_reg_type = Register::RegType::None;
        // find a register from the free list
        if (src->is_scalar()) {
          free_list = &scalar_free_list;
          evict_reg_type = Register::RegType::Scalar;
        } else {
          free_list = &vector_free_list;
          evict_reg_type = Register::RegType::Vector;
        }
        if (free_list->empty()) {
          CL_LOG("Free List Empty {}", "");
          evict(evict_reg_type);
        }
        auto free_register = free_list->front();
        free_list->pop_front();
        assert(free_register.id() != -1);
        Limb load_val = *src;
        load_val.set_dead(false);
        load_val.set_next_use(instructions_);
        load_val.set_last_use(instructions_);

        if (src->is_input()) {
          if (src->is_eval_key()) {
            if (io_limbs.find(src->term_idx()) == io_limbs.end()) {
              src->term_share()->update_evalkey_term_symbol();
            }
          }
          io_limbs[src->term_idx()].insert(src->limb_idx());
          io_limbs_local[src->term_idx()].insert(src->limb_idx());
        }

        std::shared_ptr<ISAInstruction> load_instruction;
        using OpCode = ISAInstruction::OpCode;
        if (src->is_eval_key() && src->eval_key_poly_num() == 1) {
          // If the evalkey can be generated, then generate it, don't load it
          load_instruction = std::make_shared<LoadISAInstruction>(
              OpCode::EvkGen, free_register, load_val, false);
          CL_LOG("Inserting evkgen instruction: {}", load_instruction->ppOp());
        } else {
          load_instruction = std::make_shared<LoadISAInstruction>(
              OpCode::Load, free_register, load_val, src->is_scalar());
          CL_LOG("Inserting load instruction: {}", load_instruction->ppOp());
        }

        RegisterAllocator::InstructionIterator it;
        if (register_last_use.find(free_register) != register_last_use.end()) {
          // it = register_instructions.insert(
          //     std::next(register_last_use[free_register]), load_instruction);
          register_instructions.push_back(load_instruction);
          it = register_instructions.end();
          --it;
        } else {
          register_instructions.push_back(load_instruction);
          it = register_instructions.end();
          --it;
        }
        register_last_write[free_register] = it;
        register_last_use[free_register] = it;
        limb_map_table[load_val] = free_register;
      }
    }

    const auto &dests = instruction->dests();
    for (auto dst : dests) {
      if (dst->is_bcor()) {
        continue;
      }
      Register::RegType free_reg_type = Register::RegType::None;
      FreeListType *free_list = nullptr;
      if (dst->is_scalar()) {
        free_list = &scalar_free_list;
        free_reg_type = Register::RegType::Scalar;
      } else {
        free_list = &vector_free_list;
        free_reg_type = Register::RegType::Vector;
      }
      if (free_list->empty()) {
        evict(free_reg_type);
      }
      auto free_reg = free_list->front();
      assert(free_reg.id() != -1);
      limb_map_table[*dst] = free_reg;
      free_list->pop_front();
    }

    const Polynomial *base_conversion_dest =
        instruction->base_conversion_dest();

    if (base_conversion_dest != nullptr) {
      TermIndexType base_conversion_dest_term_index =
          base_conversion_dest->term_idx();
      if (base_conversion_map_table.find(base_conversion_dest_term_index) ==
          base_conversion_map_table.end()) {
        assert(bcu_free_list.size() != 0);
        BaseConversionUnit free_bcu = bcu_free_list.front();
        free_bcu.clear();
        free_bcu.initialise(base_conversion_dest->limbs(),
                            base_conversion_dest->shares());
        // auto bcu_initialise_instruction =
        // std::make_shared<BaseConversionUnitInitialiseISAInstruction>(free_bcu,base_conversion_dest->limbs(),base_conversion_dest->shares());
        auto bcu_initialise_instruction =
            std::make_shared<BaseConversionUnitInitialiseISAInstruction>(
                free_bcu, base_conversion_dest->limbs());
        register_instructions.push_back(bcu_initialise_instruction);
        base_conversion_map_table[base_conversion_dest_term_index] = free_bcu;
        bcu_free_list.pop_front();
      }
      base_conversion_map_table.at(base_conversion_dest_term_index)
          .write(instruction->limb_idx());
    }

    const std::vector<Polynomial *> polynomial_dests =
        instruction->polynomial_dests();
    for (auto &polynomial_dest : polynomial_dests) {
      if (polynomial_dest != nullptr) {
        TermIndexType polynomial_dest_term_index = polynomial_dest->term_idx();
        if (polynomial_map_table.find(polynomial_dest_term_index) ==
            polynomial_map_table.end()) {
          // assert(bcu_free_list.size() != 0);
          // TODO: This might change?
          while (vector_free_list.size() < polynomial_dest->limbs().size()) {
            evict(Register::RegType::Vector);
          }
          std::vector<RegisterType> polynomial_registers;
          for (int i = 0; i < polynomial_dest->limbs().size(); i++) {
            auto free_reg = vector_free_list.front();
            assert(free_reg.id() != -1);
            polynomial_registers.push_back(free_reg);
            vector_free_list.pop_front();
          }

          PolynomialRegisterGroup prg(std::move(polynomial_registers));
          auto resolve_initialise_instruction =
              std::make_shared<ResolveInitialiseISAInstruction>(prg);
          register_instructions.push_back(resolve_initialise_instruction);
          polynomial_map_table[polynomial_dest_term_index] = prg;
        }
        auto prg = polynomial_map_table.at(polynomial_dest_term_index);
        prg.set_next_use(polynomial_dest->next_use());
      }
    }

    register_instructions.push_back(instruction->get_register_instruction(
        limb_map_table, base_conversion_map_table, polynomial_map_table));
    auto latest_instruction_it = register_instructions.end();
    --latest_instruction_it;

    for (auto src : instruction->srcs()) {
      if (src->is_bcor()) {
        BaseConversionUnit bcu = base_conversion_map_table.at(src->term_idx());
        assert(src->is_dead());
        bcu.free_register(*src);
        if (bcu.is_free()) {
          base_conversion_map_table.erase(src->term_idx());
          bcu.clear();
          bcu_free_list.push_back(bcu);
        }
        continue;
      }
      auto reg = limb_map_table.at(*src);
      limb_map_table.erase(*src);
      if (!src->is_dead()) {
        limb_map_table[*src] = reg;
      } else {
        assert(reg.id() != -1);
        add_reg_to_free_list(src, reg);
      }
      register_last_read[reg] = latest_instruction_it;
      register_last_use[reg] = latest_instruction_it;
    }

    for (auto dst : instruction->dests()) {
      auto reg = limb_map_table[*dst];
      limb_map_table.erase(*dst);
      if (!dst->is_dead()) {
        limb_map_table[*dst] = reg;
      } else {
        add_reg_to_free_list(dst, reg);
      }

      if (dst->is_output()) {

        io_limbs[dst->term_idx()].insert(dst->limb_idx());
        io_limbs_local[dst->term_idx()].insert(dst->limb_idx());
        auto store_instruction =
            std::make_shared<StoreISAInstruction>(*dst, reg);
        // CL_LOG("Inserting store instruction: {}",store_instruction->ppOp());
        register_instructions.push_back(store_instruction);
      }
      register_last_write[reg] = latest_instruction_it;
      register_last_use[reg] = latest_instruction_it;
    }

    for (auto &polynomial_dest : polynomial_dests) {
      auto prg = polynomial_map_table.at(polynomial_dest->term_idx());
      auto registers = prg.registers();
      if (polynomial_dest->is_dead()) {
        for (auto &reg : registers) {
          vector_free_list.push_back(reg);
        }
        polynomial_map_table.erase(polynomial_dest->term_idx());
      } else {
        prg.set_next_use(polynomial_dest->next_use());
      }
      for (auto &reg : registers) {
        register_last_write[reg] = latest_instruction_it;
        register_last_use[reg] = latest_instruction_it;
      }
    }

    const std::vector<Polynomial *> polynomial_srcs =
        instruction->polynomial_srcs();
    for (auto &polynomial_src : polynomial_srcs) {
      auto prg = polynomial_map_table.at(polynomial_src->term_idx());
      auto registers = prg.registers();
      if (polynomial_src->is_dead()) {
        for (auto &reg : registers) {
          vector_free_list.push_back(reg);
        }
        polynomial_map_table.erase(polynomial_src->term_idx());
      } else {
        prg.set_next_use(polynomial_src->next_use());
      }
      for (auto &reg : registers) {
        register_last_read[reg] = latest_instruction_it;
        register_last_use[reg] = latest_instruction_it;
      }
    }
    instructions_++;
  }

  void write_program_inputs(
      const std::string inputs_file_name, const uint16_t partition_id,
      const std::map<TermIndexType, std::shared_ptr<Term>> &terms) {
    std::stringstream input_stream, output_stream;

    for (auto it = io_limbs_local.begin(); it != io_limbs_local.end(); it++) {
      const std::shared_ptr<Term> &term = terms.at(it->first);
      std::stringstream s;
      s << term->name() << " | " << term->symbol() << " | [";
      int count = 0;
      for (const LimbIndexType limb : it->second) {
        if (count != 0) {
          s << ",";
        }
        s << limb;
        count++;
      }
      s << "]\n";
      if (term->is_input()) {
        input_stream << s.str();
      } else if (term->is_output()) {
        output_stream << s.str();
      } else {
        assert(0);
      }
    }
    std::ofstream program_inputs_file(inputs_file_name +
                                      std::to_string(partition_id));
    assert(program_inputs_file.is_open());
    program_inputs_file << "Input Stream:\n" << input_stream.str() << ";\n";
    program_inputs_file << "Output Stream:\n" << output_stream.str() << ";\n";
  }

  void finish(const std::string instruction_file_name,
              const uint16_t partition_id) {
    RegisterMapType<bool> liveValues;
    LimbSet loaded_values;
    for (auto it = register_instructions.rbegin();
         it != register_instructions.rend(); it++) {
      auto instruction = *it;
      const auto &dests = instruction->dests();
      for (auto jt = dests.rbegin(); jt != dests.rend(); jt++) {
        RegisterType *dest = *jt;
        if (dest->is_bcor()) {
          continue;
        }
        liveValues.erase(*dest);
      }
      const auto &srcs = instruction->srcs();
      for (auto jt = srcs.rbegin(); jt != srcs.rend(); jt++) {
        RegisterType *src = *jt;
        if (src->is_bcor()) {
          continue;
        }
        if (liveValues.find(*src) == liveValues.end()) {
          src->mark_dead();
          liveValues[*src] = true;
        }
      }
      auto load_instruction =
          std::dynamic_pointer_cast<LoadISAInstruction>(instruction);
      if (load_instruction) {
        auto load_src = load_instruction->load_src();
        if (!load_src->is_output()) {
          // Don't free outputs from memory
          if (loaded_values.find(*load_src) == loaded_values.end()) {
            load_instruction->set_free_load_val_from_memory();
          }
          loaded_values.insert(*load_src);
        }
      }
    }
    std::ofstream instruction_file(instruction_file_name +
                                   std::to_string(partition_id));
    assert(instruction_file.is_open());
    instruction_file << "Instruction Stream " << partition_id << ":\n";
    for (auto inst : register_instructions) {
      instruction_file << inst->ppOp() << "\n";
    }
  }
};

} // namespace Backend
} // namespace Cinnamon

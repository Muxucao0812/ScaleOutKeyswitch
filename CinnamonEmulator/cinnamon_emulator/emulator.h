// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <chrono>
#include <map>
#include <regex>
#include <string>
#include <unordered_map>
#include <variant>

#include "ckks-encoder.h"
#include "ckks-encryptor.h"
#include "context.h"
#include "evaluator.h"
#include "limb.h"

// helper type for the visitor #4
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

namespace Cinnamon::Emulator {
class Emulator {

public:
  using LimbT = Limb;
  using LimbPtrT = LimbPtr;
  using MessageType = std::variant<std::vector<double>,
                                   std::vector<std::complex<double>>, double>;
  // (Vector[double] | Vector[complex] | double , scale)
  using RawInputType = std::pair<MessageType, double>;

  using RegisterFileType = std::vector<std::shared_ptr<LimbT>>;
  using ScalarRegisterFileType = std::vector<LimbT::Element_t>;

  Emulator(const Context &context)
      : context_(context), evaluator_(Evaluator(context_)) {
    bcw_regex = std::regex("B([0-1]+): (r[0-9]+(\\[X\\])?) \\| ([0-9]+)");
    pl1_regex = std::regex("B([0-1]+): (r[0-9]+(\\[X\\])?) \\| ([0-9]+)");
    pl2_regex = std::regex("B([0-1]+), r([0-9]+): b([0-9]+)\\{([0-9]+)\\}, "
                           "(r[0-9]+(\\[X\\])?) \\| ([0-9]+)");
    pl3_regex = std::regex(
        "B([0-1]+): (r[0-9]+(\\[X\\])?), (r[0-9]+(\\[X\\])?) \\| ([0-9]+)");
    pl4_regex =
        std::regex("r([0-9]+): b([0-9]+)\\{([0-9]+)\\}, (r[0-9]+(\\[X\\])?), "
                   "(r[0-9]+(\\[X\\])?) \\| ([0-9]+)");
    rot_regex =
        std::regex("(-?[0-9]+) r([0-9]+): (r[0-9]+(\\[X\\])?) \\| ([0-9]+)");
    ntt_regex =
        std::regex("r([0-9]+): ((r[0-9]+(\\[X\\])?)|b([0-9]+)\\{([0-9]+)\\}) "
                   "\\| ([0-9]+)");
    unop_regex = std::regex("r([0-9]+): (r[0-9]+(\\[X\\])?) \\| ([0-9]+)");
    sud_regex = std::regex(
        "r([0-9]+): (r[0-9]+(\\[X\\])?), "
        "((r[0-9]+(\\[X\\])?)|b([0-9]+)\\{([0-9]+)\\}) \\| ([0-9]+)");
    rsi_regex = std::regex("\\{(r([0-9]+), )*r([0-9]+)\\}");
    rsv_regex =
        std::regex("\\{(.*)\\}: (r[0-9]+(\\[X\\])?): \\[(.*)\\] \\| ([0-9]+)");
    mod_regex = std::regex("r([0-9]+): \\{(.*)\\} \\| ([0-9]+)");
    load_regex = std::regex("r([0-9]+): (.+\\([0-9]+\\))(\\{F\\})?");
    loas_regex = std::regex("s([0-9]+): (.+\\([0-9]+\\))(\\{F\\})?");
    dis_regex = std::regex("@ ([0-9]+):([0-9]+) : (r[0-9]+(\\[X\\])?)");
    rcv_regex = std::regex("@ ([0-9]+):([0-9]+) r([0-9]+):");
    joi_regex = std::regex(
        "@ ([0-9]+):([0-9]+) (r([0-9]+))?: (r[0-9]+(\\[X\\])?) \\| ([0-9]+)");
  }
  // void generate_inputs(const std::string &inputs_file_name_, const
  // std::unordered_map<std::string, RawInputType> &raw_inputs, CKKSEncryptor
  // &encryptor);
  void generate_inputs(
      const std::string &inputs_file_name,
      const std::string &evalkeys_file_name,
      const std::unordered_map<std::string, RawInputType> &raw_inputs,
      CKKSEncryptor &encryptor);
  // void run_program(const std::string &instruction_file, const uint8_t
  // partitions, const uint32_t registers);
  void run_program_multithread(const std::string &instruction_file,
                               const uint8_t partitions,
                               const uint32_t registers);

  void generate_and_serialize_evalkeys(const std::string &output_file_name,
                                       const std::string &input_file_name,
                                       CKKSEncryptor &encryptor);
  std::unordered_map<std::string, std::pair<RnsPolynomialPtr, RnsPolynomialPtr>>
  deserialize_evalkeys(const std::string &evalkey_file_name);

  // CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
  template <typename T>
  std::map<std::string, std::vector<T>>
  get_decrypted_outputs(CKKSEncryptor &encryptor,
                        std::unordered_map<std::string, double> output_scales);
  void decrypt_and_print_outputs(
      CKKSEncryptor &encryptor,
      std::unordered_map<std::string, double> output_scales);
  void set_program_memory(
      const std::unordered_map<std::string, std::shared_ptr<LimbT>> &memory) {
    program_memory_ = memory;
  }
  auto get_program_memory() { return program_memory_; }

private:
  struct EvalkeyInfo {
    enum KeyType { Mul, Rot, Con, Boot, Ephemeral, Boot2 } key_type;
    uint32_t level;
    uint32_t extension_size;
    std::vector<uint64_t> digit_partition;
    int32_t rotation_amount;
    uint8_t ct_number;
    std::string id;

    EvalkeyInfo(std::string key_info);
  };
  using EvalKeyEntryType =
      std::tuple<std::string, EvalkeyInfo, std::vector<std::size_t>>;

  const Context &context_;
  Evaluator evaluator_;
  // std::string inputs_file_name_;
  std::unordered_map<std::string, std::shared_ptr<LimbT>> program_memory_;
  std::unordered_map<std::string, LimbT::Element_t> program_memory_scalar_;
  std::unordered_map<
      std::string, std::tuple<std::string, std::string, std::vector<uint64_t>>>
      program_outputs_;
  std::vector<std::vector<std::unique_ptr<BaseConverter>>>
      base_conversion_units_;
  std::chrono::steady_clock::time_point begin_;
  std::chrono::steady_clock::time_point end_;

  std::mutex sync_lock;
  std::mutex mem_lock;

  std::regex mod_regex;
  std::regex rsv_regex;
  std::regex rsi_regex;
  std::regex sud_regex;
  std::regex unop_regex;
  std::regex ntt_regex;
  std::regex rot_regex;
  std::regex bcw_regex;
  std::regex pl1_regex;
  std::regex pl2_regex;
  std::regex pl3_regex;
  std::regex pl4_regex;
  std::regex load_regex;
  std::regex loas_regex;
  std::regex dis_regex;
  std::regex rcv_regex;
  std::regex joi_regex;

  void handle_evalkey_stream(std::ifstream &input_file,
                             CKKSEncryptor &encryptor);
  void handle_evalkey_stream(std::ifstream &input_file,
                             const std::string &evalkeys_file_name);
  EvalKeyEntryType parse_evalkey(std::string &line);
  std::string evalkey_header = "Cinnamon::Emulator::Evalkeys\n";

  std::string get_opcode(std::string &instruction);
  std::string get_dest(std::string &instruction);
  std::string get_src(std::string &instruction);
  int get_rns_id(std::string &instruction);
  LimbPtrT get_register(const std::string &reg_str_,
                        Emulator::RegisterFileType &rf);
  LimbT::Element_t get_register_scalar(const std::string &reg_str_,
                                       Emulator::ScalarRegisterFileType &srf);
  void handle_load(const std::string &instruction,
                   Emulator::RegisterFileType &rf);
  void handle_loas(const std::string &instruction,
                   Emulator::ScalarRegisterFileType &srf);
  void handle_evg(const std::string &instruction,
                  Emulator::RegisterFileType &rf);
  void handle_store(std::string &instruction, Emulator::RegisterFileType &rf);
  void handle_binop(const std::string &opcode, std::string &instruction,
                    Emulator::RegisterFileType &rf);
  void handle_binop_scalar(const std::string &opcode, std::string &instruction,
                           Emulator::RegisterFileType &rf,
                           Emulator::ScalarRegisterFileType &srf);
  void handle_unop(const std::string &opcode, std::string &instruction,
                   Emulator::RegisterFileType &rf);
  void handle_mov(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf);
  void handle_rot(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf);
  void handle_ntt(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf,
                  std::vector<std::unique_ptr<BaseConverter>> &bcu);
  void handle_sud(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf,
                  std::vector<std::unique_ptr<BaseConverter>> &bcu);
  void handle_rsi(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf);
  void handle_rsv(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf);
  void handle_mod(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf);
  void handle_bci(const std::string &opcode, std::string &instruction,
                  std::vector<std::unique_ptr<BaseConverter>> &bcu);
  void handle_bcw(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf,
                  std::vector<std::unique_ptr<BaseConverter>> &bcu);
  void handle_pl1(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf,
                  std::vector<std::unique_ptr<BaseConverter>> &bcu);
  void handle_pl3(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf,
                  std::vector<std::unique_ptr<BaseConverter>> &bcu);
  void handle_pl4(const std::string &opcode, const std::string &instruction,
                  Emulator::RegisterFileType &rf,
                  std::vector<std::unique_ptr<BaseConverter>> &bcu);
};
} // namespace Cinnamon::Emulator
// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include "emulator.h"

#include <fstream>
#include <iomanip>

#include <iostream>
#include <regex>
#include <string>

#include <assert.h>
#include <chrono>
#include <optional>
#include <thread>

// #define CINNAMON_EUMLATOR_INTERPRETER_VERBOSE

namespace Cinnamon::Emulator {

Emulator::EvalkeyInfo::EvalkeyInfo(std::string key_info) {

  std::vector<std::string> split;
  size_t pos = 0;
  while ((pos = key_info.find(":")) != std::string::npos) {
    split.push_back(key_info.substr(0, pos));
    key_info.erase(0, pos + 1);
  }
  split.push_back(key_info);
  if (split.size() != 7 || split[0] != "K") {
    throw std::runtime_error("Invalid Evalkey Entry" + key_info);
  }

  auto evk_type = std::stoi(split[1]);
  switch (evk_type) {
  case 0:
    key_type = KeyType::Mul;
    break;
  case 1:
    key_type = KeyType::Rot;
    break;
  case 2:
    key_type = KeyType::Con;
    break;
  case 3:
    key_type = KeyType::Boot;
    break;
  case 4:
    key_type = KeyType::Ephemeral;
    break;
  case 5:
    key_type = KeyType::Boot2;
    break;
  default:
    throw std::runtime_error("Invalid Evalkey type: " + split[1]);
  }

  id = split[1] + ":" + split[2] + ":" + split[3] + ":" + split[4] + ":" +
       split[6];

  level = std::stoi(split[2]);
  extension_size = std::stoi(split[3]);
  rotation_amount = std::stoi(split[4]);

  split[5][0] = '0';
  ct_number = std::stoi(split[5]);

  auto &digit_partition_str = split[6];
  digit_partition_str[0] = ' ';

  pos = 0;
  while ((pos = digit_partition_str.find(",")) != std::string::npos) {
    digit_partition.push_back(std::stoul(digit_partition_str.substr(0, pos)));
    digit_partition_str.erase(0, pos + 1);
  }
  pos = digit_partition_str.length();
  digit_partition.push_back(std::stoul(digit_partition_str.substr(0, pos - 1)));
}

Emulator::EvalKeyEntryType Emulator::parse_evalkey(std::string &line) {
  size_t lpos = 0, rpos = 0;
  rpos = line.find('|');
  if (rpos == std::string::npos) {
    throw std::runtime_error("Invalid line: " + line);
  }
  // -1 to account for the space
  auto term = line.substr(lpos, rpos - lpos - 1);

  // +2 to account for the space
  lpos = rpos + 2;
  rpos = line.find('|', lpos);
  if (rpos == std::string::npos) {
    throw std::runtime_error("Invalid line: " + line);
  }

  auto var_type = line.substr(lpos, rpos - lpos - 1);

  EvalkeyInfo evk_info(var_type);

  // position of :
  // auto cpos = var_type.find(":");
  // auto var = var_type.substr(0,cpos);
  // auto type = var_type.substr(cpos+1);
  // std::cout << "var_type: " << var << "," << type << ":" << "\n";

  auto end = line.length();

  // strip away to spaces and the [ ]
  auto rns_base = line.substr(rpos + 3, end - rpos - 4);
  // std::cout << "rns_base: " << rns_base << ":"
  //           << "\n";

  lpos = 0;
  std::vector<uint64_t> rns_base_ids;
  while ((rpos = rns_base.find(",", lpos)) != std::string::npos) {
    auto rns_base_id = std::stoul(rns_base.substr(lpos, rpos - lpos));
    rns_base_ids.push_back(rns_base_id);
    // std::cout << rns_base_id << ";";
    lpos = rpos + 1;
  }
  auto rns_base_id = std::stoul(rns_base.substr(lpos));
  rns_base_ids.push_back(rns_base_id);
  // std::cout << rns_base_id << ";";
  // std::cout << "\n";
  return std::make_tuple(term, evk_info, rns_base_ids);
};

void Emulator::handle_evalkey_stream(std::ifstream &input_file,
                                     const std::string &evalkeys_file_name) {

  auto evalkeys = deserialize_evalkeys(evalkeys_file_name);

  std::string line;
  std::vector<std::string> evk_ids;
  std::unordered_map<std::string, std::vector<EvalKeyEntryType>>
      evalkey_entries;
  bool complete = false;
  while (std::getline(input_file, line)) {
    // std::cout << "\t" << line << "\n";
    if (line == ";") {
      complete = true;
      break;
    }
    auto parse = parse_evalkey(line);
    auto &evk_info = std::get<1>(parse);
    evalkey_entries[evk_info.id].push_back(parse);
  }

  for (auto &[k, v] : evalkey_entries) {
    evk_ids.push_back(k);
  }

  if (!complete) {
    throw std::logic_error("Unexpected EOF");
  }

  std::reverse(evk_ids.begin(), evk_ids.end());

  for (auto i = 0; i < evk_ids.size(); i++) {

    const auto &evk_entry = evalkey_entries.at(evk_ids.at(i));

    auto &parse = evk_entry.at(0);
    auto &evk_info = std::get<1>(parse);
    // std::cout << "\tEvalkey ID: " << evk_info.id << "\n";
    std::pair<RnsPolynomialPtr, RnsPolynomialPtr> evk;

    try {
      evk = evalkeys.at(evk_info.id);
    } catch (const std::out_of_range &err) {
      throw std::runtime_error("Evalkey not found: " + evk_info.id);
    }

    for (auto &parse_ : evk_entry) {
      auto &term = std::get<0>(parse_);
      auto &evk_info = std::get<1>(parse_);
      auto &rns_base_ids = std::get<2>(parse_);
      // auto & evk = evalkeys.at(evk_info.id);
      RnsPolynomialPtr poly;
      if (evk_info.ct_number == 0) {
        poly = std::get<0>(evk);
      } else if (evk_info.ct_number == 1) {
        poly = std::get<1>(evk);
      } else {
        throw std::runtime_error("Invalid CT Number");
      }

      for (auto &rns_base : rns_base_ids) {
        auto term_name = term + "(" + std::to_string(rns_base) + ")";
        program_memory_.insert({term_name, poly->at(rns_base)});
      }
    }
  }
};

void Emulator::generate_and_serialize_evalkeys(
    const std::string &output_file_name, const std::string &inputs_file_name,
    CKKSEncryptor &encryptor) {

  std::cout << "Generating Evalkeys\n";
  std::ifstream input_file(inputs_file_name, std::ios::in);
  std::string line;
  while (std::getline(input_file, line)) {
    size_t pos;
    if ((pos = line.find("Evalkey Stream")) != std::string::npos) {
      break;
    }
  }

  std::vector<std::string> evk_ids;
  std::unordered_map<std::string, EvalkeyInfo> evalkey_entries;
  std::unordered_map<std::string, std::pair<RnsPolynomialPtr, RnsPolynomialPtr>>
      evalkeys;
  bool complete = false;
  while (std::getline(input_file, line)) {
    // std::cout << "\t" << line << "\n";
    if (line == ";") {
      complete = true;
      break;
    }
    auto parse = parse_evalkey(line);
    auto &evk_info = std::get<1>(parse);
    evalkey_entries.insert({evk_info.id, evk_info});
  }

  if (!complete) {
    throw std::logic_error("Unexpected EOF");
  }

  for (auto &[k, v] : evalkey_entries) {
    evk_ids.push_back(k);
  }

  uint8_t NUM_THREADS = 16;
  std::vector<decltype(evalkeys)> thread_local_evalkeys(NUM_THREADS);
  auto process_evalkey = [&](int tid) {
    for (auto i = tid; i < evk_ids.size(); i += NUM_THREADS) {

      const auto &evk_info = evalkey_entries.at(evk_ids.at(i));

      // auto &parse = evk_entry;
      // auto &evk_info = std::get<1>(parse);
    //   std::cout << "\tEvalkey ID: " << evk_info.id << "\n";
      std::pair<RnsPolynomialPtr, RnsPolynomialPtr> evk;

      switch (evk_info.key_type) {
      case EvalkeyInfo::KeyType::Mul: {
        evk = encryptor.generate_relin_evalkey(
            evk_info.level, evk_info.extension_size, evk_info.digit_partition);
      }; break;
      case EvalkeyInfo::KeyType::Rot: {
        evk = encryptor.generate_rotation_evalkey(
            evk_info.rotation_amount, evk_info.level, evk_info.extension_size,
            evk_info.digit_partition);
      }; break;
      case EvalkeyInfo::KeyType::Con: {
        evk = encryptor.generate_conjugation_evalkey(
            evk_info.level, evk_info.extension_size, evk_info.digit_partition);
        // evalkeys[evk_info.id] = encryptor.generate_
        // (evk_info.level,evk_info.extension_size,evk_info.digit_partition);
      }; break;
      case EvalkeyInfo::KeyType::Boot: {
        evk = encryptor.generate_bootstrap_evalkey(
            evk_info.level, evk_info.extension_size, evk_info.digit_partition);
        // evalkeys[evk_info.id] = encryptor.generate_
        // (evk_info.level,evk_info.extension_size,evk_info.digit_partition);
      }; break;
      case EvalkeyInfo::KeyType::Ephemeral: {
        evk = encryptor.generate_ephemeral_evalkey(
            evk_info.level, evk_info.extension_size, evk_info.digit_partition);
      }; break;
      case EvalkeyInfo::KeyType::Boot2: {
        evk = encryptor.generate_bootstrap_evalkey2(
            evk_info.level, evk_info.extension_size, evk_info.digit_partition);
      }; break;
      default:
        throw std::runtime_error("Invalid Evalkey Type");
      }

      thread_local_evalkeys.at(tid)[evk_info.id] = evk;
    }
  };

  std::vector<std::thread> threads;
  for (size_t tid = 0; tid < NUM_THREADS; tid++) {
    threads.push_back(std::thread(process_evalkey, tid));
  }

  for (size_t tid = 0; tid < NUM_THREADS; tid++) {
    threads[tid].join();
  }

  // for (size_t tid = 0; tid < NUM_THREADS; tid++) {
  //     process_evalkey(tid);
  // }

  for (int tid = 0; tid < NUM_THREADS; tid++) {
    auto &local_evalkeys_ = thread_local_evalkeys.at(tid);
    evalkeys.insert(local_evalkeys_.begin(), local_evalkeys_.end());
  }
  thread_local_evalkeys.clear();

  std::ofstream output_file(output_file_name, std::ios::out | std::ios::binary);
  std::stringstream ss;
  output_file.write(evalkey_header.c_str(), evalkey_header.size() + 1);

  std::size_t context_n = context_.n();
  output_file.write(reinterpret_cast<const char *>(&context_n),
                    sizeof(context_n));
  std::size_t context_num_rns_bases = context_.num_rns_bases();
  output_file.write(reinterpret_cast<const char *>(&context_num_rns_bases),
                    sizeof(context_num_rns_bases));
  const auto &rns_bases = context_.rns_bases();
  // std::cout << "Context RNS Bases: " << rns_bases.at(0).value();
  for (int i = 0; i < rns_bases.size(); i++) {
    uint64_t modulus = rns_bases.at(i).value();
    output_file.write(reinterpret_cast<const char *>(&modulus),
                      sizeof(modulus));
  }

  std::size_t num_evalkeys = evalkeys.size();
  output_file.write(reinterpret_cast<const char *>(&num_evalkeys),
                    sizeof(num_evalkeys));

  for (auto &[k, v] : evalkeys) {
    auto evk_entry = evalkey_entries.at(k);
    std::size_t evk_entry_id_size = evk_entry.id.size() + 1;
    output_file.write(reinterpret_cast<const char *>(&evk_entry_id_size),
                      sizeof(evk_entry_id_size));
    output_file.write(evk_entry.id.c_str(), evk_entry_id_size);
    auto evk = evalkeys.at(k);
    auto evk0 = std::get<0>(evk);
    auto evk1 = std::get<1>(evk);

    std::size_t num_limbs = evk_entry.level + evk_entry.extension_size;

    output_file.write(reinterpret_cast<const char *>(&num_limbs),
                      sizeof(num_limbs));

    if (evk0->num_limbs() != num_limbs) {
      throw std::runtime_error("Unexpected Number of Limbs in Evalkey");
    }
    if (evk1->num_limbs() != num_limbs) {
      throw std::runtime_error("Unexpected Number of Limbs in Evalkey");
    }

    for (auto &[limb_id, limb] : evk0->limb_index_map()) {
      output_file.write(reinterpret_cast<const char *>(&limb_id),
                        sizeof(limb_id));
      if (limb->size() != context_n) {
        throw std::runtime_error("Unexpected Limb Size");
      }
      output_file.write(reinterpret_cast<const char *>(limb->data()),
                        context_n * sizeof(Limb::Element_t));
    }
    for (auto &[limb_id, limb] : evk1->limb_index_map()) {
      output_file.write(reinterpret_cast<const char *>(&limb_id),
                        sizeof(limb_id));
      if (limb->size() != context_n) {
        throw std::runtime_error("Unexpected Limb Size");
      }
      output_file.write(reinterpret_cast<const char *>(limb->data()),
                        context_n * sizeof(Limb::Element_t));
    }
  }
};

std::unordered_map<std::string, std::pair<RnsPolynomialPtr, RnsPolynomialPtr>>
Emulator::deserialize_evalkeys(const std::string &evalkey_file_name) {
  std::unordered_map<std::string, std::pair<RnsPolynomialPtr, RnsPolynomialPtr>>
      evalkeys;
  std::ifstream evalkey_file(evalkey_file_name,
                             std::ios::in | std::ios::binary);

  std::vector<char> evk_header_buf(evalkey_header.size() + 1);
  evalkey_file.read(evk_header_buf.data(), evalkey_header.size() + 1);
  std::string evk_header_read(evk_header_buf.data());
  if (evalkey_header != evk_header_read) {
    throw std::runtime_error("Unexpected Header");
  }

  std::size_t context_n = 0;
  evalkey_file.read(reinterpret_cast<char *>(&context_n), sizeof(context_n));
  if (context_n != context_.n()) {
    throw std::runtime_error("Unexpected Context Size");
  }
  std::size_t context_num_rns_bases = 0;
  evalkey_file.read(reinterpret_cast<char *>(&context_num_rns_bases),
                    sizeof(context_num_rns_bases));
  if (context_num_rns_bases != context_.num_rns_bases()) {
    throw std::runtime_error("Unexpected Number of RNS Bases");
  }
  const auto &rns_bases = context_.rns_bases();
  // std::cout << "Context RNS Bases: " << rns_bases.at(0).value();
  for (int i = 0; i < context_num_rns_bases; i++) {
    uint64_t modulus = 0;
    evalkey_file.read(reinterpret_cast<char *>(&modulus), sizeof(modulus));
    if (modulus != rns_bases.at(i).value()) {
      throw std::runtime_error("Unexpected RNS Modulus");
    }
  }

  std::size_t num_evalkeys = 0;
  evalkey_file.read(reinterpret_cast<char *>(&num_evalkeys),
                    sizeof(num_evalkeys));

  for (std::size_t i = 0; i < num_evalkeys; i++) {
    // auto evk_entry = evalkey_entries.at(k);
    std::size_t evk_entry_id_size = 0;
    evalkey_file.read(reinterpret_cast<char *>(&evk_entry_id_size),
                      sizeof(evk_entry_id_size));

    std::vector<char> evk_entry_id_buf(evk_entry_id_size);
    evalkey_file.read(evk_entry_id_buf.data(), evk_entry_id_size);
    std::string evk_entry_id(evk_entry_id_buf.data());

    std::size_t num_limbs = 0;
    evalkey_file.read(reinterpret_cast<char *>(&num_limbs), sizeof(num_limbs));

    auto evk0 = std::make_shared<RnsPolynomial>();
    auto evk1 = std::make_shared<RnsPolynomial>();

    for (std::size_t j = 0; j < num_limbs; j++) {
      std::uint64_t limb_id = 0;
      evalkey_file.read(reinterpret_cast<char *>(&limb_id), sizeof(limb_id));
      auto limb_data = Util::allocate_uint2(context_n);
      evalkey_file.read(reinterpret_cast<char *>(limb_data.get()),
                        context_n * sizeof(Limb::Element_t));
      auto limb = std::make_shared<Limb>(std::move(limb_data), context_n,
                                         limb_id, true);
      evk0->write_limb(std::move(limb), limb_id);
    }

    for (std::size_t j = 0; j < num_limbs; j++) {
      std::uint64_t limb_id = 0;
      evalkey_file.read(reinterpret_cast<char *>(&limb_id), sizeof(limb_id));
      auto limb_data = Util::allocate_uint2(context_n);
      evalkey_file.read(reinterpret_cast<char *>(limb_data.get()),
                        context_n * sizeof(Limb::Element_t));
      auto limb = std::make_shared<Limb>(std::move(limb_data), context_n,
                                         limb_id, true);
      evk1->write_limb(std::move(limb), limb_id);
    }

    evalkeys[evk_entry_id] = std::pair(evk0, evk1);
  }
  return std::move(evalkeys);
}

void Emulator::generate_inputs(
    const std::string &inputs_file_name, const std::string &evalkeys_file_name,
    const std::unordered_map<std::string, Emulator::RawInputType> &raw_inputs,
    CKKSEncryptor &encryptor) {

  std::cout << "Generating Program Inputs\n";
  std::ifstream input_file(inputs_file_name, std::ios::in);
  std::string line;

  int stream_count = 0;

  CKKSEncoder encoder(context_);

  std::unordered_map<std::string, std::pair<RnsPolynomialPtr, RnsPolynomialPtr>>
      encrypted_inputs;
  // std::unordered_map<std::string, std::pair<RnsPolynomialPtr,
  // RnsPolynomialPtr>> evalkeys;

  auto parse_io = [&](std::string &line) {
    size_t lpos = 0, rpos = 0;
    rpos = line.find('|');
    if (rpos == std::string::npos) {
      throw std::runtime_error("Invalid line: " + line);
    }
    // -1 to account for the space
    auto term = line.substr(lpos, rpos - lpos - 1);
    // std::cout << "term: " << term << ":"
    //           << "\n";

    // +2 to account for the space
    lpos = rpos + 2;
    rpos = line.find('|', lpos);
    if (rpos == std::string::npos) {
      throw std::runtime_error("Invalid line: " + line);
    }

    auto var_type = line.substr(lpos, rpos - lpos - 1);

    // position of :
    auto cpos = var_type.find(":");
    auto var = var_type.substr(0, cpos);
    auto type = var_type.substr(cpos + 1);
    // std::cout << "var_type: " << var << "," << type << ":"
    //           << "\n";

    auto end = line.length();

    // strip away to spaces and the [ ]
    auto rns_base = line.substr(rpos + 3, end - rpos - 4);
    // std::cout << "rns_base: " << rns_base << ":"
    //           << "\n";

    lpos = 0;
    std::vector<uint64_t> rns_base_ids;
    while ((rpos = rns_base.find(",", lpos)) != std::string::npos) {
      auto rns_base_id = std::stoul(rns_base.substr(lpos, rpos - lpos));
      rns_base_ids.push_back(rns_base_id);
      // std::cout << rns_base_id << ";";
      lpos = rpos + 1;
    }
    auto rns_base_id = std::stoul(rns_base.substr(lpos));
    rns_base_ids.push_back(rns_base_id);
    // std::cout << rns_base_id << ";"
    //  << "\n";
    return std::make_tuple(term, var, type, rns_base_ids);
  };

  auto handle_ciphertext_stream = [&]() {
    std::string line;
    while (std::getline(input_file, line)) {
      // std::cout << "\t" << line << "\n";
      if (line == ";") {
        return;
      }
      auto parse = parse_io(line);
      auto &term = std::get<0>(parse);
      auto &var = std::get<1>(parse);
      auto &type = std::get<2>(parse);
      auto &rns_base_ids = std::get<3>(parse);
      auto it = encrypted_inputs.find(var);
      if (it == encrypted_inputs.end()) {
        RawInputType raw_input;
        try {
          raw_input = raw_inputs.at(var);
        } catch (const std::out_of_range &err) {
          throw std::runtime_error("Raw Inputs key error: " + var);
        }
        auto &message = std::get<0>(raw_input);
        auto &scale = std::get<1>(raw_input);
        std::visit(
            overloaded{
                [&](auto arg) {
                  throw std::invalid_argument("Message: " + var);
                },
                [&](const double &arg) {
                  encrypted_inputs[var] =
                      encryptor.encode_and_encrypt(arg, scale, rns_base_ids);
                },
                [&](const std::vector<double> &arg) {
                  encrypted_inputs[var] =
                      encryptor.encode_and_encrypt(arg, scale, rns_base_ids);
                },
                [&](const std::vector<std::complex<double>> &arg) {
                  encrypted_inputs[var] =
                      encryptor.encode_and_encrypt(arg, scale, rns_base_ids);
                }},
            message);
        it = encrypted_inputs.find(var);
      }

      RnsPolynomialPtr ct;
      if (type == "c0") {
        ct = std::get<0>(it->second);
      } else if (type == "c1") {
        ct = std::get<1>(it->second);
      } else {
        throw std::runtime_error("Invalid Type: " + type);
      }

      for (auto &rns_base : rns_base_ids) {
        auto term_name = term + "(" + std::to_string(rns_base) + ")";
        program_memory_[term_name] = ct->at(rns_base);
      }
    }

    throw std::logic_error("Unexpected EOF");
  };

  auto handle_scalar_stream = [&]() {
    std::string line;
    size_t lpos = 0;
    size_t rpos = 0;

    std::vector<std::string> scalars_str;
    bool complete = false;
    while (std::getline(input_file, line)) {
    //   std::cout << "\t" << line << "\n";
      if (line == ";") {
        complete = true;
        break;
      }
      auto parse = parse_io(line);
      auto &var = std::get<1>(parse);
      if (raw_inputs.find(var) == raw_inputs.end()) {
        std::cerr << "ERROR: Raw Input key error: " << var << "\n"
                  << std::flush;
        throw std::runtime_error("Raw Inputs key error: " + var);
      }
      scalars_str.push_back(line);
    }

    if (!complete) {
      throw std::logic_error("Unexpected EOF");
    }

    std::reverse(scalars_str.begin(), scalars_str.end());

    auto process_scalar = [&]() {
      for (int i = 0; i < scalars_str.size(); i++) {
        auto line = scalars_str.at(i);
        // std::cout << "\t" << line << "\n";
        auto parse = parse_io(line);
        auto &term = std::get<0>(parse);
        auto &var = std::get<1>(parse);
        auto &type = std::get<2>(parse);
        auto &rns_base_ids = std::get<3>(parse);
        RawInputType raw_input;
        try {
          raw_input = raw_inputs.at(var);
        } catch (const std::out_of_range &err) {
          std::cerr << "ERROR: Raw Input key error: " << var << "\n"
                    << std::flush;
          throw std::runtime_error("Raw Inputs key error: " + var);
        }

        auto &message = std::get<0>(raw_input);
        auto &scale = std::get<1>(raw_input);
        if (type == "s") {
          std::map<uint32_t, Limb::Element_t> sc;
          std::visit(overloaded{[&](auto arg) {
                                  throw std::invalid_argument("Message" + var);
                                },
                                [&](const double &arg) {
                                  sc = encoder.encode_scalar(arg, scale,
                                                             rns_base_ids);
                                }},
                     message);
          for (auto &rns_base : rns_base_ids) {
            auto term_name = term + "(" + std::to_string(rns_base) + ")";
            program_memory_scalar_.insert({term_name, sc.at(rns_base)});
          }
        } else {
          std::cerr << "ERROR: Invalid Type For Scalars: " << type << "\n"
                    << std::flush;
          throw std::runtime_error("Invalid Type: " + type);
        }
      }
    };
    process_scalar();
  };

  auto handle_plaintext_stream = [&]() {
    std::string line;
    size_t lpos = 0;
    size_t rpos = 0;

    std::vector<std::string> plaintexts_str;
    bool complete = false;
    while (std::getline(input_file, line)) {
      // std::cout << "\t" << line << "\n";
      if (line == ";") {
        complete = true;
        break;
      }
      auto parse = parse_io(line);
      auto &var = std::get<1>(parse);
      if (raw_inputs.find(var) == raw_inputs.end()) {
        std::cerr << "ERROR: Raw Input key error: " << var << "\n"
                  << std::flush;
        throw std::runtime_error("Raw Inputs key error: " + var);
      }
      plaintexts_str.push_back(line);
    }

    if (!complete) {
      throw std::logic_error("Unexpected EOF");
    }

    std::reverse(plaintexts_str.begin(), plaintexts_str.end());

    int NUM_THREADS = 16;
    std::vector<decltype(program_memory_)> thread_local_program_memory_(
        NUM_THREADS);
    std::vector<decltype(program_memory_scalar_)>
        thread_local_program_memory_scalar_(NUM_THREADS);
    auto process_plaintext = [&](int tid) {
      auto &local_program_memory_ = thread_local_program_memory_.at(tid);
      auto &local_program_memory_scalar_ =
          thread_local_program_memory_scalar_.at(tid);
      for (int i = tid; i < plaintexts_str.size(); i += NUM_THREADS) {
        auto line = plaintexts_str.at(i);
        // std::cout << "\t" << line << "\n";
        auto parse = parse_io(line);
        auto &term = std::get<0>(parse);
        auto &var = std::get<1>(parse);
        auto &type = std::get<2>(parse);
        auto &rns_base_ids = std::get<3>(parse);
        RawInputType raw_input;
        try {
          raw_input = raw_inputs.at(var);
        } catch (const std::out_of_range &err) {
          std::cerr << "ERROR: Raw Input key error: " << var << "\n"
                    << std::flush;
          throw std::runtime_error("Raw Inputs key error: " + var);
        }

        auto &message = std::get<0>(raw_input);
        auto &scale = std::get<1>(raw_input);

        if (type == "p") {
          RnsPolynomialPtr pt;
          std::visit(
              overloaded{[&](auto arg) {
                           throw std::invalid_argument("Message" + var);
                         },
                         [&](const double &arg) {
                           pt = encoder.encode(arg, scale, rns_base_ids);
                         },
                         [&](const std::vector<double> &arg) {
                           pt = encoder.encode(arg, scale, rns_base_ids);
                         },
                         [&](const std::vector<std::complex<double>> &arg) {
                           pt = encoder.encode(arg, scale, rns_base_ids);
                         }},
              message);
          for (auto &rns_base : rns_base_ids) {
            auto term_name = term + "(" + std::to_string(rns_base) + ")";
            local_program_memory_.insert({term_name, pt->at(rns_base)});
          }
        } else {
          std::cerr << "ERROR: Invalid Type: " << type << "\n" << std::flush;
          throw std::runtime_error("Invalid Type: " + type);
        }
      }
    };

    std::vector<std::thread> threads;
    for (size_t tid = 0; tid < NUM_THREADS; tid++) {
      threads.push_back(std::thread(process_plaintext, tid));
    }

    for (size_t tid = 0; tid < NUM_THREADS; tid++) {
      threads[tid].join();
    }

    for (int tid = 0; tid < NUM_THREADS; tid++) {
      auto &local_program_memory_ = thread_local_program_memory_.at(tid);
      program_memory_.insert(local_program_memory_.begin(),
                             local_program_memory_.end());
      auto &local_program_memory_scalar_ =
          thread_local_program_memory_scalar_.at(tid);
      program_memory_scalar_.insert(local_program_memory_scalar_.begin(),
                                    local_program_memory_scalar_.end());
    }
    thread_local_program_memory_.clear();
    thread_local_program_memory_scalar_.clear();
  };

  auto handle_output_stream = [&]() {
    std::string line;
    while (std::getline(input_file, line)) {
    //   std::cout << "\t" << line << "\n";
      if (line == ";") {
        return;
      }
      auto parse = parse_io(line);
      auto &term = std::get<0>(parse);
      auto &var = std::get<1>(parse);
      auto &type = std::get<2>(parse);
      auto &rns_base_ids = std::get<3>(parse);

      auto &output = program_outputs_[var];
      if (type == "c0") {
        std::get<0>(output) = term;
        std::get<2>(output) = rns_base_ids;
      } else if (type == "c1") {
        std::get<1>(output) = term;
      } else {
        throw std::runtime_error("Invalid Type: " + type);
      }
    }

    throw std::logic_error("Unexpected EOF");
  };

  while (std::getline(input_file, line)) {
    // std::cout << line << "\n";
    size_t pos;
    if (stream_count == 0) {
      // Ciphertext Stream
      if ((pos = line.find("Ciphertext Stream")) == std::string::npos) {
        throw std::logic_error("Invalid Stream Header: " + line);
      } else {
        stream_count++;
        handle_ciphertext_stream();
      }
    } else if (stream_count == 1) {
      // Plaintext Stream
      if ((pos = line.find("Plaintext Stream")) == std::string::npos) {
        throw std::logic_error("Invalid Stream Heading: " + line);
      } else {
        stream_count++;
        handle_plaintext_stream();
        // // TODO: make a stream for scalars....
        // stream_count++;
      }
    } else if (stream_count == 2) {
      // Scalar Stream
      if ((pos = line.find("Scalar Stream")) == std::string::npos) {
        throw std::logic_error("Invalid Stream Heading: " + line);
      } else {
        stream_count++;
        handle_scalar_stream();
      }
    } else if (stream_count == 3) {
      // Output Stream
      if ((pos = line.find("Output Stream")) == std::string::npos) {
        throw std::logic_error("Invalid Stream Heading: " + line);
      } else {
        stream_count++;
        handle_output_stream();
      }
    } else if (stream_count == 4) {
      // Evalkey Stream
      if ((pos = line.find("Evalkey Stream")) == std::string::npos) {
        throw std::logic_error("Invalid Stream Heading: " + line);
      } else {
        stream_count++;
        // handle_evalkey_stream(input_file,encryptor);
        handle_evalkey_stream(input_file, evalkeys_file_name);
      }
    }
  }
}

std::string Emulator::get_opcode(std::string &instruction) {
  auto pos = instruction.find(" ");
  if (pos == std::string::npos) {
    throw std::runtime_error("Invalid Instruction: " + instruction);
  }
  auto opcode = instruction.substr(0, pos);
  instruction.erase(0, pos + 1);
  return opcode;
};

std::string Emulator::get_dest(std::string &instruction) {
  auto pos = instruction.find(":");
  if (pos == std::string::npos) {
    throw std::runtime_error("Invalid Instruction xx: " + instruction);
  }
  auto dest = instruction.substr(0, pos);
  instruction.erase(0, pos + 2);
  return dest;
};

std::string Emulator::get_src(std::string &instruction) {
  auto pos = instruction.find(",");
  auto src = instruction.substr(0, pos);
  instruction.erase(0, pos + 2);
  return src;
};

int Emulator::get_rns_id(std::string &instruction) {
  auto pos = instruction.find("|");
  if (pos == std::string::npos) {
    throw std::runtime_error("Invalid Instruction xx: " + instruction);
  }
  auto id = std::stoi(instruction.substr(pos + 2));
  instruction.erase(pos - 1);
  return id;
};

Emulator::LimbPtrT Emulator::get_register(const std::string &reg_str_,
                                          Emulator::RegisterFileType &rf) {
  std::string reg_str(reg_str_);
  auto pos = reg_str.find("[X]");
  bool dead = false;
  if (pos != std::string::npos) {
    dead = true;
    reg_str.erase(pos);
  }
  if (reg_str[0] != 'r') {
    throw std::runtime_error("Invalid Register: " + reg_str);
  }
  reg_str[0] = '0';
  size_t reg_id = std::stoul(reg_str);
  auto reg = rf[reg_id];
  if (dead) {
    rf[reg_id].reset();
  }
  return reg;
};

Emulator::LimbT::Element_t
Emulator::get_register_scalar(const std::string &reg_str_,
                              Emulator::ScalarRegisterFileType &srf) {
  std::string reg_str(reg_str_);
  auto pos = reg_str.find("[X]");
  bool dead = false;
  if (pos != std::string::npos) {
    dead = true;
    reg_str.erase(pos);
  }
  if (reg_str[0] != 's') {
    throw std::runtime_error("Invalid Scalar Register: " + reg_str);
  }
  reg_str[0] = '0';
  size_t reg_id = std::stoul(reg_str);
  auto scalar = srf[reg_id];
  return scalar;
};

void Emulator::handle_load(const std::string &instruction,
                           Emulator::RegisterFileType &rf) {

  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        load_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    auto dest_reg = std::stoul(match[1]);
    auto term = match[2];
    bool free_from_mem = false;
    if (match[3].length() != 0) {
      free_from_mem = true;
    }

    LimbPtrT src = nullptr;
    {
      std::lock_guard<std::mutex> lock(mem_lock);

      try {
        src = program_memory_.at(term);
      } catch (const std::out_of_range &err) {
        throw std::runtime_error("Load not found: " + std::string(term));
      }
      if (free_from_mem) {
        program_memory_.erase(term);
      }
    }
    if (free_from_mem) {
      rf[dest_reg] = std::move(src);
    } else {
      rf[dest_reg] = evaluator_.copy(src, src->rns_base_id());
    }
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    std::cout << "LOAD " << dest_reg << " : " << term << "\n";
#endif
  } else {
    throw std::runtime_error("Invalid Instruction for load: " + instruction);
  }
}

void Emulator::handle_loas(const std::string &instruction,
                           Emulator::ScalarRegisterFileType &srf) {

  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        loas_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    auto dest_reg = std::stoul(match[1]);
    auto term = match[2];
    bool free_from_mem = false;
    if (match[3].length() != 0) {
      free_from_mem = true;
    }

    LimbT::Element_t src = 0;
    {
      std::lock_guard<std::mutex> lock(mem_lock);
      src = program_memory_scalar_.at(term);
      if (free_from_mem) {
        program_memory_scalar_.erase(term);
      }
    }
    srf[dest_reg] = src;
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    std::cout << "LOAS " << dest_reg << " : " << term << "\n";
#endif
  } else {
    throw std::runtime_error("Invalid Instruction for load: " + instruction);
  }
}

void Emulator::handle_evg(const std::string &instruction,
                          Emulator::RegisterFileType &rf) {
  handle_load(instruction, rf);
};

void Emulator::handle_store(std::string &instruction,
                            Emulator::RegisterFileType &rf) {
  auto dest = get_dest(instruction);
  auto term = instruction;
  dest[0] = '0';
  auto reg = std::stoul(dest);
  auto src = rf[reg];
  auto copy = evaluator_.copy(src, src->rns_base_id());
  {
    std::lock_guard<std::mutex> lock(mem_lock);
    program_memory_[term] = std::move(copy);
  }
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
  std::cout << "STORE " << reg << " : " << term << "\n";
#endif
};

void Emulator::handle_binop(const std::string &opcode, std::string &instruction,
                            Emulator::RegisterFileType &rf) {
  auto dest = get_dest(instruction);
  auto rns_id = get_rns_id(instruction);
  dest[0] = '0';
  auto dest_reg = std::stoul(dest);
  auto src1 = get_src(instruction);
  auto src2 = get_src(instruction);
  auto reg1 = get_register(src1, rf);
  auto reg2 = get_register(src2, rf);
  if (opcode == "add") {
    rf[dest_reg] = evaluator_.add(reg1, reg2, rns_id);
  } else if (opcode == "sub") {
    rf[dest_reg] = evaluator_.subtract(reg1, reg2, rns_id);
  } else if (opcode == "mup") {
    rf[dest_reg] = evaluator_.multiply(reg1, reg2, rns_id);
  } else if (opcode == "mul") {
    rf[dest_reg] = evaluator_.multiply(reg1, reg2, rns_id);
  } else if (opcode == "sud") {
    // For now this is here
    rf[dest_reg] = evaluator_.subtract_and_divide_modulus(reg1, reg2, rns_id);
  }
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
  std::cout << opcode << dest_reg << " :" << src1 << ":" << src2 << ":"
            << rns_id << ":\n";
#endif
};

void Emulator::handle_binop_scalar(const std::string &opcode,
                                   std::string &instruction,
                                   Emulator::RegisterFileType &rf,
                                   Emulator::ScalarRegisterFileType &srf) {
  auto dest = get_dest(instruction);
  auto rns_id = get_rns_id(instruction);
  dest[0] = '0';
  auto dest_reg = std::stoul(dest);
  auto src1 = get_src(instruction);
  auto src2 = get_src(instruction);
  auto reg1 = get_register(src1, rf);
  auto sca2 = get_register_scalar(src2, srf);
  if (opcode == "ads") {
    rf[dest_reg] = evaluator_.add(reg1, sca2, rns_id);
  } else if (opcode == "sus") {
    rf[dest_reg] = evaluator_.subtract(reg1, sca2, rns_id);
  } else if (opcode == "mus") {
    rf[dest_reg] = evaluator_.multiply(reg1, sca2, rns_id);
  }
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
  std::cout << opcode << dest_reg << " :" << src1 << ":" << src2 << ":"
            << rns_id << ":\n";
#endif
};

void Emulator::handle_unop(const std::string &opcode, std::string &instruction,
                           Emulator::RegisterFileType &rf) {
  auto dest = get_dest(instruction);
  auto rns_id = get_rns_id(instruction);
  dest[0] = '0';
  auto dest_reg = std::stoul(dest);
  auto src1 = get_src(instruction);
  auto reg1 = get_register(src1, rf);
  if (opcode == "con") {
    rf[dest_reg] = evaluator_.conjugate(reg1, rns_id);
  } else if (opcode == "neg") {
    rf[dest_reg] = evaluator_.negate(reg1, rns_id);
  } else {
    throw std::runtime_error("Unimplemented");
  }
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
  std::cout << opcode << dest_reg << " :" << src1 << ":" << rns_id << ":\n";
#endif
};

void Emulator::handle_mov(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf) {
  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        unop_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    auto dest_reg = std::stoul(match[1]);
    auto reg1 = get_register(match[2], rf);
    auto rns_base_id = std::stoul(match[4]);
    if (match[2].length() == 0) {
      rf[dest_reg] = std::move(reg1);
    } else {
      rf[dest_reg] = evaluator_.copy(reg1, rns_base_id);
    }
  } else {
    throw std::runtime_error("Invalid Instruction for mov: " + instruction);
  }
};

void Emulator::handle_rot(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf) {
  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        rot_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    auto rotation_amount = std::stoi(match[1]);
    auto dest_reg = std::stoul(match[2]);
    auto reg1 = get_register(match[3], rf);
    auto rns_base_id = std::stoul(match[5]);

    rf[dest_reg] = evaluator_.rotate(reg1, rotation_amount, rns_base_id);
  } else {
    throw std::runtime_error("Invalid Instruction for pl2: " + instruction);
  }
};

void Emulator::handle_ntt(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf,
                          std::vector<std::unique_ptr<BaseConverter>> &bcu) {

  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        ntt_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    // auto dest_bcu_id = std::stoul(match[1]);
    auto dest_reg = std::stoul(match[1]);
    auto rns_base_id = std::stoul(match[7]);
    if (match[5].length() != 0) {
      auto src_bcu_id = std::stoul(match[5]);
      auto src_bcu_output_index = std::stoul(match[6]);
      if (opcode == "ntt") {
        rf[dest_reg] =
            evaluator_.ntt(*bcu[src_bcu_id], src_bcu_output_index, rns_base_id);
      } else {
        throw std::runtime_error("Invalid Instruction for ntt" + instruction);
      }
    } else if (match[3].length() != 0) {
      auto reg1 = get_register(match[3], rf);
      if (opcode == "ntt") {
        rf[dest_reg] = evaluator_.ntt(reg1, rns_base_id);
      } else if (opcode == "int") {
        rf[dest_reg] = evaluator_.inverse_ntt(reg1, rns_base_id);
      } else {
        throw std::runtime_error("Invalid Instruction for ntt/intt" +
                                 instruction);
      }
    } else {
      throw std::runtime_error("Invalid Instruction for ntt/intt" +
                               instruction);
    }

  } else {
    throw std::runtime_error("Invalid Instruction for ntt: " + instruction);
  }
};

void Emulator::handle_sud(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf,
                          std::vector<std::unique_ptr<BaseConverter>> &bcu) {

  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        sud_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    // auto dest_bcu_id = std::stoul(match[1]);
    auto dest_reg = std::stoul(match[1]);
    auto rns_base_id = std::stoul(match[9]);
    auto reg1 = get_register(match[2], rf);
    if (match[7].length() != 0) {
      auto src_bcu_id = std::stoul(match[7]);
      auto src_bcu_output_index = std::stoul(match[8]);
      rf[dest_reg] = evaluator_.subtract_and_divide_modulus(
          reg1, *bcu[src_bcu_id], src_bcu_output_index, rns_base_id);
    } else if (match[10].length() != 0) {

      std::string dest_rns_base_ids_str(match[10]);
      size_t pos = 0;
      std::vector<uint64_t> dest_rns_base_ids;
      while ((pos = dest_rns_base_ids_str.find(',')) != std::string::npos) {
        auto id = dest_rns_base_ids_str.substr(0, pos);
        dest_rns_base_ids.push_back(std::stoul(id));
        dest_rns_base_ids_str.erase(0, pos + 1);
      }
      dest_rns_base_ids.push_back(std::stoul(dest_rns_base_ids_str));

      auto reg2 = get_register(match[5], rf);
      rf[dest_reg] = evaluator_.subtract_and_divide_modulus(
          reg1, reg2, rns_base_id, dest_rns_base_ids);
    } else if (match[5].length() != 0) {
      auto reg2 = get_register(match[5], rf);
      rf[dest_reg] =
          evaluator_.subtract_and_divide_modulus(reg1, reg2, rns_base_id);
    } else {
      throw std::runtime_error("Invalid Instruction for ntt/intt" +
                               instruction);
    }

  } else {
    throw std::runtime_error("Invalid Instruction for ntt: " + instruction);
  }
};

void Emulator::handle_rsi(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf) {
  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        rsi_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    for (auto i = 2; i < match.size(); i += 2) {
      rf[std::stoul(match[i])] = evaluator_.get_zero();
    }
    rf[std::stoul(match[match.size() - 1])] = evaluator_.get_zero();

  } else {
    throw std::runtime_error("Invalid Instruction for pl2: " + instruction);
  }
};

void Emulator::handle_rsv(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf) {
  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        rsv_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    std::string dests_str(match[1]);
    std::string dest_rns_base_ids_str(match[4]);

    size_t pos = 0;
    std::vector<LimbPtrT> dests;
    while ((pos = dests_str.find(',')) != std::string::npos) {
      auto reg = dests_str.substr(0, pos);
#ifdef CINNAMON_EUMLATOR_DEBUG
      if (reg[0] != 'r') {
        throw std::runtime_error("Invalid Instruction for rsv: " + instruction);
      }
#endif
      reg[0] = '0';
      dests.push_back(rf[std::stoul(reg)]);
      dests_str.erase(0, pos + 2);
    }
#ifdef CINNAMON_EUMLATOR_DEBUG
    if (dests_str[0] != 'r') {
      throw std::runtime_error("Invalid Instruction for rsv: " + instruction);
    }
#endif
    dests_str[0] = '0';
    dests.push_back(rf[std::stoul(dests_str)]);

    pos = 0;
    std::vector<uint64_t> dest_rns_base_ids;
    while ((pos = dest_rns_base_ids_str.find(',')) != std::string::npos) {
      auto id = dest_rns_base_ids_str.substr(0, pos);
      dest_rns_base_ids.push_back(std::stoul(id));
      dest_rns_base_ids_str.erase(0, pos + 1);
    }
    dest_rns_base_ids.push_back(std::stoul(dest_rns_base_ids_str));

    auto src1 = get_register(match[2], rf);
    auto rns_base_id = std::stoul(match[5]);

    evaluator_.resolve_write(src1, dests, dest_rns_base_ids, rns_base_id);

  } else {
    throw std::runtime_error("Invalid Instruction for pl2: " + instruction);
  }
};

void Emulator::handle_mod(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf) {
  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        mod_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    auto dest_reg = std::stoul(match[1]);
    auto rns_base_id = std::stoul(match[3]);

    std::string srcs_str(match[2]);
    size_t pos = 0;
    std::vector<LimbPtrT> srcs;
    while ((pos = srcs_str.find(',')) != std::string::npos) {
      auto reg = srcs_str.substr(0, pos);
      srcs.push_back(get_register(reg, rf));
      srcs_str.erase(0, pos + 2);
    }
    srcs.push_back(get_register(srcs_str, rf));

    rf[dest_reg] = evaluator_.mod(srcs, rns_base_id);

  } else {
    throw std::runtime_error("Invalid Instruction for pl2: " + instruction);
  }
};

void Emulator::handle_bci(const std::string &opcode, std::string &instruction,
                          std::vector<std::unique_ptr<BaseConverter>> &bcu) {
  auto dest = get_dest(instruction);
  if (dest[0] != 'B') {
    throw std::runtime_error("Invalid Dest for bci: " + dest);
  }
  dest[0] = '0';
  auto dest_bcu_id = std::stoul(dest);

  size_t pos = 0;
  pos = instruction.find("],");
  auto dest_base_ids_string = instruction.substr(1, pos - 1);
  instruction = instruction.erase(0, pos + 4);
  auto src_base_ids_string = instruction.erase(instruction.length() - 1);

  auto string_to_uint64_vec = [&](std::string &string) {
    std::vector<uint64_t> result;
    size_t pos = 0;
    while ((pos = string.find(",")) != std::string::npos) {
      auto num = std::stoul(string.substr(0, pos));
      result.push_back(num);
      string.erase(0, pos + 1);
    }
    auto num = std::stoul(string);
    result.push_back(num);
    return result;
  };

  auto dest_base_ids = string_to_uint64_vec(dest_base_ids_string);
  auto src_base_ids = string_to_uint64_vec(src_base_ids_string);

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
  std::cout
      << "BCI B" << dest_bcu_id << ":" << src_base_ids_string << " | "
      << dest_base_ids_string
      << "\n"; // << " :" << src1 << ":" << src2 << ":" << rns_id << ":\n";
#endif

  bcu[dest_bcu_id]->init(src_base_ids, dest_base_ids);
};

void Emulator::handle_bcw(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf,
                          std::vector<std::unique_ptr<BaseConverter>> &bcu) {

  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        bcw_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    auto dest_bcu_id = std::stoul(match[1]);
    auto reg1 = get_register(match[2], rf);
    auto rns_base_id = std::stoul(match[4]);

    if (match[3].length() != 0) {
      evaluator_.bcw(reg1, *bcu[dest_bcu_id], rns_base_id);
    } else {
      evaluator_.bcw(reg1, *bcu[dest_bcu_id], rns_base_id);
    }
  } else {
    throw std::runtime_error("Invalid Instruction for pl2: " + instruction);
  }
};

void Emulator::handle_pl1(const std::string &opcode,
                          const std::string &instruction,
                          Emulator::RegisterFileType &rf,
                          std::vector<std::unique_ptr<BaseConverter>> &bcu) {

  std::smatch match;
  if (std::regex_search(instruction.begin(), instruction.end(), match,
                        pl1_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
    for (auto m : match)
      std::cout << "  submatch " << m << '\n';
#endif

    auto dest_bcu_id = std::stoul(match[1]);
    auto reg1 = get_register(match[2], rf);
    auto rns_base_id = std::stoul(match[4]);

    if (match[3].length() != 0) {
      evaluator_.pl1_inplace(reg1, *bcu[dest_bcu_id], rns_base_id);
    } else {
      evaluator_.pl1(reg1, *bcu[dest_bcu_id], rns_base_id);
    }
  } else {
    throw std::runtime_error("Invalid Instruction for pl2: " + instruction);
  }
};

void Emulator::run_program_multithread(const std::string &instruction_file_base,
                                       const uint8_t partitions,
                                       const uint32_t registers) {

  std::cout << "Starting Program\n" << std::flush;
  begin_ = std::chrono::steady_clock::now();

  std::vector<std::vector<std::shared_ptr<Limb>>> register_files;
  std::vector<std::vector<Limb::Element_t>> register_files_scalar;
  std::vector<uint64_t> sync_id;
  std::map<uint64_t, uint64_t> sync_count;
  std::map<uint64_t, LimbPtr> dis_value;
  std::map<uint64_t, std::map<uint8_t, uint64_t>> rcv_pending;
  std::vector<bool> advance;

  auto handle_dis = [&](const std::string &opcode,
                        const std::string &instruction, size_t partition) {
    std::smatch match;
    std::lock_guard<std::mutex> lock(sync_lock);

    if (std::regex_search(instruction.begin(), instruction.end(), match,
                          dis_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
      for (auto m : match)
        std::cout << "  submatch " << m << '\n';
#endif
      auto sync_id_inst = std::stoul(match[1]);
      auto sync_count_inst = std::stoul(match[2]);
      auto reg1 = get_register(match[3], register_files[partition]);

      if (sync_id[partition] == 0) {
        sync_id[partition] = sync_id_inst;
        advance[partition] = false;
        sync_count[sync_id_inst]++;
        if (dis_value[sync_id_inst] != nullptr) {
          throw std::runtime_error("dis_value is not nullptr");
        }
        if (match[4].length() == 0) {
          dis_value[sync_id_inst] = std::move(reg1);
        } else {
          dis_value[sync_id_inst] = evaluator_.copy(reg1, reg1->rns_base_id());
        }
        // partition = (partition + 1) % partitions;
        return;
      } else if (sync_id[partition] != sync_id_inst) {
        throw std::runtime_error("Mismatched sync_id");
      }

      advance[partition] = true;
      sync_id[partition] = 0;
      if (sync_count[sync_id_inst] == sync_count_inst) {
        dis_value.erase(sync_id_inst);
        sync_count.erase(sync_id_inst);
      }
      // partition = (partition + 1) % partitions;

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
      std::cout
          << "DIS @ " << sync_id_inst << " : " << match[3]
          << "\n"; // << " :" << src1 << ":" << src2 << ":" << rns_id << ":\n";
#endif
    } else {
      throw std::runtime_error("Invalid Instruction for dis: " + instruction);
    }
  };

  auto handle_rcv = [&](const std::string &opcode,
                        const std::string &instruction, size_t partition) {
    std::smatch match;
    std::lock_guard<std::mutex> lock(sync_lock);
    if (std::regex_search(instruction.begin(), instruction.end(), match,
                          rcv_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
      for (auto m : match)
        std::cout << "  submatch " << m << '\n';
#endif
      auto sync_id_inst = std::stoul(match[1]);
      auto sync_count_inst = std::stoul(match[2]);

      if (sync_id[partition] == 0) {
        sync_id[partition] = sync_id_inst;
        advance[partition] = false;
        // partition = (partition + 1) % partitions;
        return;
      } else if (sync_id[partition] != sync_id_inst) {
        throw std::runtime_error("Mismatched sync_id");
      }

      if (dis_value.find(sync_id_inst) == dis_value.end()) {
        // partition = (partition + 1) % partitions;
        return;
      }

      auto dest_reg = std::stoul(match[3]);
      auto val = dis_value.at(sync_id_inst);
      register_files[partition][dest_reg] =
          evaluator_.copy(val, val->rns_base_id());
      sync_count[sync_id_inst]++;
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
      std::cout
          << "RCV @ " << sync_id_inst << " R" << match[3]
          << ":\n"; // << " :" << src1 << ":" << src2 << ":" << rns_id << ":\n";
#endif

      sync_id[partition] = 0;
      advance[partition] = true;
      if (sync_count[sync_id_inst] == sync_count_inst) {
        sync_count.erase(sync_id_inst);
        dis_value.erase(sync_id_inst);
      }
      // partition = (partition + 1) % partitions;

    } else {
      throw std::runtime_error("Invalid Instruction for dis: " + instruction);
    }
  };

  std::map<uint64_t, LimbPtrT> joi_value;
  // std::optional<std::pair<uint8_t,uint64_t>> joi_destination;
  auto handle_joi = [&](const std::string &opcode,
                        const std::string &instruction, size_t partition) {
    std::smatch match;
    std::lock_guard<std::mutex> lock(sync_lock);
    if (std::regex_search(instruction.begin(), instruction.end(), match,
                          joi_regex)) {

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
      for (auto m : match)
        std::cout << "  submatch " << m << '\n';
#endif
      auto sync_id_inst = std::stoul(match[1]);
      auto sync_count_inst = std::stoul(match[2]);

      auto rns_base_id = std::stoul(match[7]);

      if (sync_id[partition] == 0) {
        sync_id[partition] = sync_id_inst;
        advance[partition] = false;
        sync_count[sync_id_inst]++;
        auto src_reg = get_register(match[5], register_files[partition]);
        assert(src_reg);
        if (joi_value.find(sync_id_inst) == joi_value.end()) {
          joi_value[sync_id_inst] = src_reg;
        } else {
          joi_value[sync_id_inst] =
              evaluator_.add(joi_value.at(sync_id_inst), src_reg, rns_base_id);
        }
        if (match[3].length() == 0) {
          advance[partition] = true;
          sync_id[partition] = 0;
        }
        // partition = (partition + 1) % partitions;
        return;
      } else if (sync_id[partition] != sync_id_inst) {
        throw std::runtime_error("Mismatched sync_id");
      }

      // sync_id[partition] = 0;
      if (sync_count[sync_id_inst] == sync_count_inst) {
        if (match[3].length() != 0) {
          register_files[partition][std::stoul(match[4])] =
              std::move(joi_value.at(sync_id_inst));
          advance[partition] = true;
          sync_id[partition] = 0;
          sync_count.erase(sync_id_inst);
          joi_value.erase(sync_id_inst);
        }
      }
      // partition = (partition + 1) % partitions;

#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
      if (match[3].length() == 0) {
        std::cout << "JOI @ " << sync_id_inst << " : " << match[5]
                  << "\n"; // << " :" << src1 << ":" << src2 << ":" << rns_id <<
                           // ":\n";
      } else {
        std::cout << "JOI @ " << sync_id_inst << " R" << match[4] << " : "
                  << match[5] << "\n"; // << " :" << src1 << ":" << src2 << ":"
                                       // << rns_id << ":\n";
      }
#endif

    } else {
      throw std::runtime_error("Invalid Instruction for joi: " + instruction);
    }
  };

  std::vector<uint64_t> instruction_count;

  const auto num_base_conversion_units = 2;
  register_files.resize(partitions);
  register_files_scalar.resize(partitions);
  base_conversion_units_.resize(partitions);

  std::vector<std::ifstream> instruction_files;

  std::vector<std::string> instructions;
  std::vector<bool> completed;
  size_t complete_count = 0;

  for (size_t i = 0; i < partitions; i++) {
    auto instruction_file_name = instruction_file_base + std::to_string(i);
    std::ifstream ifile(instruction_file_name, std::ios::in);
    instruction_files.push_back(std::move(ifile));
    register_files.at(i).resize(registers);
    register_files_scalar.at(i).resize(registers);
    base_conversion_units_.at(i).resize(num_base_conversion_units);
    for (size_t j = 0; j < num_base_conversion_units; j++) {
      base_conversion_units_[i][j] = std::make_unique<BaseConverter>(context_);
    }
    instruction_count.push_back(0);
    std::string instruction;
    std::getline(instruction_files[i], instruction);
    instructions.push_back("");
    advance.push_back(true);
    sync_id.push_back(0);
    completed.push_back(false);
  }

  auto thread_fn = [&](std::size_t partition) {
    std::string instruction;
    while (true) {
      if (advance[partition]) {
        if (!std::getline(instruction_files.at(partition), instruction)) {
          // break;
          std::lock_guard<std::mutex> lock(sync_lock);
          if (completed[partition] == false) {
            complete_count++;
            completed[partition] = true;
          }
          if (complete_count == partitions) {
            break;
          } else {
            // partition = (partition + 1) % partitions;
            continue;
          }
        }
        instruction_count[partition]++;
        instructions[partition] = instruction;
      }
      instruction = instructions[partition];
      auto &register_file = register_files[partition];
      auto &register_file_scalar = register_files_scalar[partition];
      auto &base_conversion_unit = base_conversion_units_[partition];
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
      std::cout << "P" << partition << ": " << instruction << "\n";
#endif

      if (instruction_count[partition] % 100000 == 0) {
        auto now = std::chrono::steady_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::seconds>(now - begin_)
                .count();
        std::cout << "HEARTBEAT:" << partition << " @ " << duration
                  << " seconds " << instruction_count[partition] / (1000)
                  << "K instructions\n"
                  << std::flush;
      }
      auto opcode = get_opcode(instruction);
      if (opcode == "load") {
        handle_load(instruction, register_file);
      } else if (opcode == "loas") {
        handle_loas(instruction, register_file_scalar);
      } else if (opcode == "store") {
        handle_store(instruction, register_file);
      } else if (opcode == "spill") {
        handle_store(instruction, register_file);
      } else if (opcode == "evg") {
        handle_evg(instruction, register_file);
      } else if (opcode == "add") {
        handle_binop(opcode, instruction, register_file);
      } else if (opcode == "ads") {
        handle_binop_scalar(opcode, instruction, register_file,
                            register_file_scalar);
      } else if (opcode == "sub") {
        handle_binop(opcode, instruction, register_file);
      } else if (opcode == "sus") {
        handle_binop_scalar(opcode, instruction, register_file,
                            register_file_scalar);
      } else if (opcode == "neg") {
        handle_unop(opcode, instruction, register_file);
      } else if (opcode == "mov") {
        handle_mov(opcode, instruction, register_file);
      } else if (opcode == "mup") {
        handle_binop(opcode, instruction, register_file);
      } else if (opcode == "mus") {
        handle_binop_scalar(opcode, instruction, register_file,
                            register_file_scalar);
      } else if (opcode == "mul") {
        handle_binop(opcode, instruction, register_file);
      } else if (opcode == "int") {
        handle_ntt(opcode, instruction, register_file, base_conversion_unit);
      } else if (opcode == "ntt") {
        handle_ntt(opcode, instruction, register_file, base_conversion_unit);
      } else if (opcode == "sud") {
        handle_sud(opcode, instruction, register_file, base_conversion_unit);
      } else if (opcode == "rot") {
        handle_rot(opcode, instruction, register_file);
      } else if (opcode == "con") {
        handle_unop(opcode, instruction, register_file);
      } else if (opcode == "rsi") {
        handle_rsi(opcode, instruction, register_file);
      } else if (opcode == "rsv") {
        handle_rsv(opcode, instruction, register_file);
      } else if (opcode == "mod") {
        handle_mod(opcode, instruction, register_file);
      } else if (opcode == "bci") {
        handle_bci(opcode, instruction, base_conversion_unit);
      } else if (opcode == "bcw") {
        handle_bcw(opcode, instruction, register_file, base_conversion_unit);
      } else if (opcode == "pl1") {
        handle_pl1(opcode, instruction, register_file, base_conversion_unit);
        // } else if (opcode == "pl2") {
        //     handle_pl2(opcode, instruction, register_file,
        //     base_conversion_unit);
        // } else if (opcode == "pl3") {
        //     handle_pl3(opcode, instruction, register_file,
        //     base_conversion_unit);
        // } else if (opcode == "pl4") {
        //     handle_pl4(opcode, instruction, register_file,
        //     base_conversion_unit);
      } else if (opcode == "dis") {
        handle_dis(opcode, instruction, partition);
      } else if (opcode == "rcv") {
        handle_rcv(opcode, instruction, partition);
      } else if (opcode == "joi") {
        handle_joi(opcode, instruction, partition);
      } else {
        throw std::runtime_error("Invalid opcode: " + opcode);
      }
#ifdef CINNAMON_EUMLATOR_INTERPRETER_VERBOSE
      std::cout << "\t" << opcode << ":"
                << "\n";
      std::cout << "\t" << instruction << ":"
                << "\n";
#endif
    }
    // partition = (i + 1) % partitions;
  };

  std::vector<std::thread> threads;
  for (size_t tid = 0; tid < partitions; tid++) {
    threads.push_back(std::thread(thread_fn, tid));
  }

  for (size_t tid = 0; tid < partitions; tid++) {
    threads[tid].join();
  }

  end_ = std::chrono::steady_clock::now();
}

// std::map<std::string,std::vector<double>>
// Emulator::get_decrypted_outputs(CKKSEncryptor & encryptor,
// std::unordered_map<std::string,double> output_scales) {
void Emulator::decrypt_and_print_outputs(
    CKKSEncryptor &encryptor,
    std::unordered_map<std::string, double> output_scales) {
  // std::map<std::string,std::vector<double>> outputs;
  std::cout << "Outputs: \n";
  for (auto &[key, value] : program_outputs_) {
    auto c0_term = std::get<0>(value);
    auto c1_term = std::get<1>(value);
    auto rns_base_ids = std::get<2>(value);
    auto ct0 = std::make_shared<RnsPolynomial>();
    auto ct1 = std::make_shared<RnsPolynomial>();
    for (auto &rns_base_id : rns_base_ids) {
      auto c0_addr = c0_term + '(' + std::to_string(rns_base_id) + ')';
      auto c1_addr = c1_term + '(' + std::to_string(rns_base_id) + ')';
      ct0->write_limb(program_memory_.at(c0_addr)->move_to_host_ptr(),
                      rns_base_id);
      ct1->write_limb(program_memory_.at(c1_addr)->move_to_host_ptr(),
                      rns_base_id);
    }
    std::cout << key << " : [";
    auto decrypt = encryptor.decrypt_and_decode<double>(
        std::make_pair(ct0, ct1), rns_base_ids, output_scales.at(key));
    // auto decrypt =
    // encryptor.decrypt_and_decode<std::complex<double>>(std::make_pair(ct0,
    // ct1), rns_base_ids, output_scales.at(key));
    std::cout << std::setprecision(15) << decrypt[0];
    for (size_t i = 1; i < decrypt.size(); i++) {
      std::cout << std::setprecision(15) << ", " << decrypt[i];
    }
    std::cout << "]\n";
  }

  std::cout << "Execution Time = "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end_ -
                                                                     begin_)
                   .count()
            << " milliseconds" << std::endl;

  // return std::move(outputs);
}

// CINNAMON_EUMLATOR_DATATYPE_TEMPLATE(T)
template <typename T>
std::map<std::string, std::vector<T>> Emulator::get_decrypted_outputs(
    CKKSEncryptor &encryptor,
    std::unordered_map<std::string, double> output_scales) {
  std::map<std::string, std::vector<T>> outputs;
  for (auto &[key, value] : program_outputs_) {
    auto c0_term = std::get<0>(value);
    auto c1_term = std::get<1>(value);
    auto rns_base_ids = std::get<2>(value);
    auto ct0 = std::make_shared<RnsPolynomial>();
    auto ct1 = std::make_shared<RnsPolynomial>();
    for (auto &rns_base_id : rns_base_ids) {
      auto c0_addr = c0_term + '(' + std::to_string(rns_base_id) + ')';
      auto c1_addr = c1_term + '(' + std::to_string(rns_base_id) + ')';
      ct0->write_limb(program_memory_.at(c0_addr)->move_to_host_ptr(),
                      rns_base_id);
      ct1->write_limb(program_memory_.at(c1_addr)->move_to_host_ptr(),
                      rns_base_id);
    }
    if (output_scales.find(key) != output_scales.end()) {
      auto decrypt = encryptor.decrypt_and_decode<T>(
          std::make_pair(ct0, ct1), rns_base_ids, output_scales.at(key));
      outputs[key] = std::move(decrypt);
    } else {
      auto decrypt = encryptor.decrypt_and_decode<T>(std::make_pair(ct0, ct1),
                                                     rns_base_ids, 1);
      outputs[key] = std::move(decrypt);
    }
  }

  std::cout << "Execution Time = "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end_ -
                                                                     begin_)
                   .count()
            << " milliseconds" << std::endl;

  return std::move(outputs);
}

template std::map<std::string, std::vector<double>>
Emulator::get_decrypted_outputs<double>(
    CKKSEncryptor &encryptor,
    std::unordered_map<std::string, double> output_scales);
template std::map<std::string, std::vector<std::complex<double>>>
Emulator::get_decrypted_outputs(
    CKKSEncryptor &encryptor,
    std::unordered_map<std::string, double> output_scales);

} // namespace Cinnamon::Emulator
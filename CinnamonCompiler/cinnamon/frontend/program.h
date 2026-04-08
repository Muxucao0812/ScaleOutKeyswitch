// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "cinnamon/frontend/term.h"
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Cinnamon {
namespace Frontend {

template <typename> class TermMapOptional;
template <typename> class TermMap;
class TermMapBase;
class Program;
class Program {
public:
  Program(std::string name, std::uint32_t rnsBitSize,
          std::uint8_t partitionSize)
      : name(name), nextTermIndex(0), rnsBitSize(rnsBitSize),
        currentPartitionSize(partitionSize), currentPartitionId(0) {
    if (currentPartitionSize == 0) {
      throw std::runtime_error("Partition Size cannot be zero");
    }
    if (rnsBitSize != 28) {
      throw std::runtime_error(
          "Unsupported RNS Bit Size: " + std::to_string(rnsBitSize) +
          ". Only 28bit RNS primes are supported as of now.");
    }
  }

  Program(const Program &copy) = delete;

  Program &operator=(const Program &assign) = delete;

  Term::Ptr makeTermOld(Op op, const std::vector<Term::Ptr> &operands = {}) {
    throw std::runtime_error("Deprecated, this function must not be called");
    auto term = std::make_shared<Term>(op, *this, Term::Type::Cipher, 0, 0,
                                       false, 0, 0);
    if (operands.size() > 0) {
      term->setOperands(operands);
    }
    return term;
  }

  bool receiveCheck(const Term::Ptr &term) {
    auto termPartitionSize = term->getPartitionSize();
    auto termPartitionId = term->getPartitionId();

    auto currentPartitionSize = getCurrentPartitionSize();
    auto currentPartitionId = getCurrentPartitionId();

    if (termPartitionSize < currentPartitionSize) {
      if (currentPartitionSize % termPartitionSize != 0) {
        throw std::runtime_error(
            "ERROR: Receive called accross neither subpartitions: size");
        return false;
      }
      if (termPartitionId / (currentPartitionSize / termPartitionSize) !=
          currentPartitionId) {
        throw std::runtime_error(
            "ERROR: Receive called accross neither subpartitions: id");
        return false;
      }

    } else if (termPartitionSize > currentPartitionSize) {
      if (termPartitionSize % currentPartitionSize != 0) {
        throw std::runtime_error(
            "ERROR: Receive called accross neither subpartitions: size");
        return false;
      }
      if (currentPartitionId / (termPartitionSize / currentPartitionSize) !=
          termPartitionId) {
        throw std::runtime_error(
            "ERROR: Receive called accross neither subpartitions: id");
        return false;
      }

    } else {
      throw std::runtime_error(
          "ERROR: Receive called accross neither subpartitions: equal");
      return false;
    }

    return true;
  }

  Term::Ptr makeTerm(Op op, const Term::Type type, const Term::Scale_t scale,
                     const Term::Level_t level, bool ephemeralKey,
                     const std::vector<Term::Ptr> &operands_ = {}) {
    std::vector<Term::Ptr> operands;
    for (auto &o : operands_) {
      if (op == Op::Receive) {
        operands.push_back(o);
        continue;
      }
      if (o->getPartitionSize() != currentPartitionSize ||
          o->getPartitionId() != currentPartitionId) {
        if (o->getType() != Term::Type::Cipher) {
          throw std::runtime_error(
              "ERROR: Receive called on value of Type: " + o->getTypeString() +
              ". Receive Only Valid for Type::Cipher");
        }
        if (o->getNeedsRelinearization()) {
          throw std::runtime_error(
              "ERROR: Receive called on a term that needs relinearization. "
              "Operand of Receive Needs to be relinearized");
        }
        if (!receiveCheck(o)) {
          throw std::runtime_error("ERROR: Receive Check Failed");
        }
        auto receiveTerm = std::make_shared<Term>(
            Op::Receive, *this, type, scale, level, ephemeralKey,
            currentPartitionSize, currentPartitionId);
        terms.push_back(receiveTerm);
        receiveTerm->setOperands({o});
        operands.push_back(receiveTerm);
      } else {
        operands.push_back(o);
      }
    }
    auto term =
        std::make_shared<Term>(op, *this, type, scale, level, ephemeralKey,
                               currentPartitionSize, currentPartitionId);
    if (operands.size() > 0) {
      term->setOperands(operands);
    }
    terms.push_back(term);
    return term;
  }

  Term::Ptr makeTerm(Op op, const Term::Type type, const Term::Scale_t scale,
                     const Term::Level_t level, bool ephemeralKey,
                     std::uint8_t partitionSize, std::uint8_t partitionId,
                     const std::vector<Term::Ptr> &operands = {}) {
    auto term =
        std::make_shared<Term>(op, *this, type, scale, level, ephemeralKey,
                               partitionSize, partitionId);
    if (operands.size() > 0) {
      term->setOperands(operands);
    }
    terms.push_back(term);
    return term;
  }

  Term::Ptr makeCiphertextInput(const std::string &name, Term::Scale_t scale,
                                Term::Level_t level) {

    auto term = makeTerm(Op::Input, Term::Type::Cipher, scale, level, false);
    term->set<NameAttribute>(name);

    term->set<TypeAttribute>(Type::Cipher);

    inputs.emplace(name, term);

    // add new inputs to sources and sinks
    this->newSources.push_back(term);
    this->newSinks.push_back(term);
    return term;
  }

  Term::Ptr makePlaintextInput(const std::string &name, std::uint32_t scale,
                               std::uint32_t level, bool is_scalar = false) {
    // auto term = makeTerm(Op::Input, Term::Type::Plain, scale, level, false);
    // term->set<NameAttribute>(name);
    // term->set<EncodeAtScaleAttribute>(scale);
    // term->set<EncodeAtLevelAttribute>(level);
    // inputs.emplace(name, term);

    // term->set<TypeAttribute>(Type::Raw);

    auto encode = makeTerm(Op::Input, Term::Type::Plain, scale, level, false);
    encode->set<NameAttribute>(name);
    encode->set<IsScalarAttribute>(is_scalar);
    encode->set<TypeAttribute>(Type::Plain);
    inputs.emplace(name, encode);

    // add new inputs to sources and sinks
    this->newSources.push_back(encode);
    this->newSinks.push_back(encode);
    return encode;
  }

  Term::Ptr makeReceive(const Term::Ptr &term) {
    if (term->getPartitionSize() == getCurrentPartitionSize() &&
        term->getPartitionId() == getCurrentPartitionId()) {
      return term;
    }
    if (term->getType() != Term::Type::Cipher) {
      throw std::runtime_error(
          "ERROR: Receive called on value of Type: " + term->getTypeString() +
          ". Receive Only Valid for Type::Cipher");
    }
    if (term->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Receive called on a term that needs relinearization. Operand "
          "of Receive Needs to be relinearized");
    }
    if (!receiveCheck(term)) {
      throw std::runtime_error("ERROR: Receive check Failed");
    }
    auto receive = makeTerm(Op::Receive, term->getType(), term->getScale(),
                            term->getLevel(), term->getEphemeralKey(), {term});
    return receive;
  }

  void makePartition(uint8_t partitionSize, uint8_t partitionID) {

    auto term = makeTerm(Op::Partition, Term::Type::Cipher, 0, 0, false);
    term->set<PartitionSizeAttribute>(partitionSize);
    term->set<PartitionIdAttribute>(partitionID);
    this->setCurrentPartitionSize(partitionSize);
    this->setCurrentPartitionId(partitionID);
  }

  Term::Ptr makeOutput(std::string name, const Term::Ptr &term) {
    if (term->getType() != Term::Type::Cipher) {
      throw std::runtime_error(
          "ERROR: Output called on value of Type:" + term->getTypeString() +
          "Output Only Valid for Type::Cipher");
    }
    if (term->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Output called on a term that needs relinearization");
    }
    if (term->getEphemeralKey()) {
      throw std::runtime_error("ERROR: Output called on a term that is "
                               "encrypted with the ephemeral key.");
    }
    auto output = makeTerm(Op::Output, term->getType(), term->getScale(),
                           term->getLevel(), false, {term});
    outputs.emplace(name, output);
    output->set<NameAttribute>(name);
    return output;
  }

  Term::Ptr makeAdd(const Term::Ptr &op1, const Term::Ptr &op2) {
    if (op1->getScale() != op2->getScale()) {
      std::stringstream s;
      s << "Addition Between values of unequal scale: " << op1->getScale()
        << ", " << op2->getScale();
      throw std::runtime_error(s.str());
    }
    if (op1->getLevel() != op2->getLevel()) {
      std::stringstream s;
      s << "Addition Between values of unequal level : " << op1->getLevel()
        << ", " << op2->getLevel();
      throw std::runtime_error(s.str());
    }
    if (op1->getType() == Term::Type::Cipher &&
        op2->getType() == Term::Type::Cipher &&
        (op1->getEphemeralKey() != op2->getEphemeralKey())) {
      std::stringstream s;
      s << "Ciphertext Ciphertext addition Between values of invalid ephemeral "
           "key states : "
        << op1->getEphemeralKey() << ", " << op2->getEphemeralKey();
      throw std::runtime_error(s.str());
    }
    bool ephemeralKeyState = op1->getEphemeralKey() || op2->getEphemeralKey();

    Term::Type type = Term::Type::Plain;
    if (op1->getType() == Term::Type::Cipher ||
        op2->getType() == Term::Type::Cipher) {
      type = Term::Type::Cipher;
    }

    auto add = makeTerm(Op::Add, type, op1->getScale(), op1->getLevel(),
                        ephemeralKeyState, {op1, op2});
    bool needsRelinearization =
        op1->getNeedsRelinearization() || op2->getNeedsRelinearization();
    add->setNeedsRelinearization(needsRelinearization);
    return add;
  }

  Term::Ptr makeSubtract(const Term::Ptr &op1, const Term::Ptr &op2) {
    if (op1->getScale() != op2->getScale()) {
      std::stringstream s;
      s << "Subtraction Between values of unequal scale: " << op1->getScale()
        << ", " << op2->getScale();
      throw std::runtime_error(s.str());
    }
    if (op1->getLevel() != op2->getLevel()) {
      std::stringstream s;
      s << "Subtraction Between values of unequal level : " << op1->getLevel()
        << ", " << op2->getLevel();
      throw std::runtime_error(s.str());
    }

    if (op1->getType() == Term::Type::Cipher &&
        op2->getType() == Term::Type::Cipher &&
        (op1->getEphemeralKey() != op2->getEphemeralKey())) {
      std::stringstream s;
      s << "Ciphertext Ciphertext subtraction Between values of invalid "
           "ephemeral states : "
        << op1->getEphemeralKey() << ", " << op2->getEphemeralKey();
      throw std::runtime_error(s.str());
    }
    bool ephemeralKeyState = op1->getEphemeralKey() || op2->getEphemeralKey();

    Term::Type type = Term::Type::Plain;
    if (op1->getType() == Term::Type::Cipher ||
        op2->getType() == Term::Type::Cipher) {
      type = Term::Type::Cipher;
    }

    auto subtract = makeTerm(Op::Sub, type, op1->getScale(), op1->getLevel(),
                             ephemeralKeyState, {op1, op2});
    bool needsRelinearization =
        op1->getNeedsRelinearization() || op2->getNeedsRelinearization();
    subtract->setNeedsRelinearization(needsRelinearization);
    return subtract;
  }

  Term::Ptr makeMultiply(const Term::Ptr &op1, const Term::Ptr &op2) {
    if (op1->getLevel() != op2->getLevel()) {
      std::stringstream s;
      s << "Multiplication Between values of unequal level : "
        << op1->getLevel() << ", " << op2->getLevel();
      throw std::runtime_error(s.str());
    }

    auto newScale = op1->getScale() + op2->getScale();

    bool needsRelinearization =
        op1->getNeedsRelinearization() || op2->getNeedsRelinearization();
    Term::Type type = Term::Type::Plain;
    if (op1->getType() == Term::Type::Cipher ||
        op2->getType() == Term::Type::Cipher) {
      type = Term::Type::Cipher;
    }

    if (op1->getType() == Term::Type::Cipher &&
        op2->getType() == Term::Type::Cipher) {
      if (op1->getNeedsRelinearization() || op2->getNeedsRelinearization()) {
        throw std::runtime_error(
            "ERROR: Multiply called on a term that needs relinearization. Both "
            "operands of a Ciphertext Ciphertext Multiply need to be "
            "relinearized");
      }
      needsRelinearization = true;
    }

    if (op1->getType() == Term::Type::Cipher &&
        op2->getType() == Term::Type::Cipher &&
        (op1->getEphemeralKey() != op2->getEphemeralKey())) {
      std::stringstream s;
      s << "Ciphertext Ciphertext multiplication Between values of invalid "
           "ephemeral key states : "
        << op1->getEphemeralKey() << ", " << op2->getEphemeralKey();
      throw std::runtime_error(s.str());
    }
    bool ephemeralKeyState = op1->getEphemeralKey() || op2->getEphemeralKey();

    auto multiply = makeTerm(Op::Mul, type, newScale, op1->getLevel(),
                             ephemeralKeyState, {op1, op2});
    multiply->setNeedsRelinearization(needsRelinearization);

    return multiply;
  }

  Term::Ptr makeNegate(const Term::Ptr &term) {
    auto negate = makeTerm(Op::Negate, term->getType(), term->getScale(),
                           term->getLevel(), term->getEphemeralKey(), {term});
    negate->setNeedsRelinearization(term->getNeedsRelinearization());
    return negate;
  }

  Term::Ptr makeConjugate(const Term::Ptr &term) {
    if (term->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Conjugate called on a term that needs relinearization. "
          "Operand of Conjugate Needs to be relinearized");
    }
    if (term->getEphemeralKey()) {
      throw std::runtime_error(
          "ERROR: Conjugate called on a term that is encrypted with the "
          "ephemeral key. Operand of Conjugate Needs to be encrypted with the "
          "standard key");
    }
    auto conjugate = makeTerm(Op::Conjugate, term->getType(), term->getScale(),
                              term->getLevel(), false, {term});
    return conjugate;
  }

  Term::Ptr makeLeftRotation(const Term::Ptr &term, std::int32_t slots) {
    if (term->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Rotate called on a term that needs relinearization. Operand "
          "of Rotate Needs to be relinearized");
    }
    if (term->getEphemeralKey()) {
      throw std::runtime_error(
          "ERROR: Rotate called on a term that is encrypted with the ephemeral "
          "key. Operand of Rotate Needs to be encrypted with the standard key");
    }
    auto rotation = makeTerm(Op::RotateLeftConst, term->getType(),
                             term->getScale(), term->getLevel(), false, {term});
    rotation->set<RotationAttribute>(slots);
    return rotation;
  }

  Term::Ptr makeRightRotation(const Term::Ptr &term, std::int32_t slots) {
    if (term->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Rotate called on a term that needs relinearization. Operand "
          "of Rotate Needs to be relinearized");
    }
    if (term->getEphemeralKey()) {
      throw std::runtime_error(
          "ERROR: Rotate called on a term that is encrypted with the ephemeral "
          "key. Operand of Rotate Needs to be encrypted with the standard key");
    }
    auto rotation = makeTerm(Op::RotateRightConst, term->getType(),
                             term->getScale(), term->getLevel(), false, {term});
    rotation->set<RotationAttribute>(slots);
    return rotation;
  }

  Term::Ptr
  makeRotateMultiplyAccumulate(const Term::Ptr &CipherTextOperand,
                               const std::vector<Term::Ptr> &PlaintextOperands,
                               std::vector<std::int32_t> RotationIndices) {
    if (CipherTextOperand->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Rotate Multiply Accumulate called on a term that needs "
          "relinearization. Operand of Rotate Needs to be relinearized");
    }
    if (CipherTextOperand->getType() != Term::Type::Cipher) {
      throw std::runtime_error("ERROR: Rotate Multiply Accumulate CipherText "
                               "Operand of Invalid Type");
    }
    if (CipherTextOperand->getEphemeralKey()) {
      throw std::runtime_error(
          "ERROR: Rotate Multiply Accumulate called on a term that is "
          "encrypted with the ephemeral key. Operand of Rotate Needs to be "
          "encrypted with the standard key");
    }
    if (RotationIndices.size() <= 1) {
      throw std::runtime_error(
          "Must have atleast 2 Rotation Indices for RotateMultiplyAcc");
    }
    if (PlaintextOperands.size() != RotationIndices.size()) {
      throw std::runtime_error(
          "Must have one plaintext operand for every rotation index");
    }
    auto plaintextScale = PlaintextOperands.at(0)->getScale();
    auto ciphertextLevel = CipherTextOperand->getLevel();
    for (auto &plaintext : PlaintextOperands) {
      if (plaintextScale != plaintext->getScale()) {
        throw std::runtime_error(
            "All plaintext operands must have the same scale");
      }
      if (ciphertextLevel != plaintext->getLevel()) {
        throw std::runtime_error(
            "Ciphertext Level and plaintext Level must be equal");
      }
      if (plaintext->getType() != Term::Type::Plain) {
        throw std::runtime_error("ERROR: Rotate Multiply Accumulate Plaintext "
                                 "Operand of Invalid Type");
      }
    }

    std::vector<Term::Ptr> operands;
    operands.push_back(CipherTextOperand);
    operands.insert(operands.end(), PlaintextOperands.begin(),
                    PlaintextOperands.end());
    auto rotMulAcc = makeTerm(Op::RotMulAcc, Term::Type::Cipher,
                              CipherTextOperand->getScale() + plaintextScale,
                              CipherTextOperand->getLevel(), false, operands);
    rotMulAcc->set<RotMulAccRotationAttribute>(RotationIndices);
    return rotMulAcc;
  }

  Term::Ptr
  makeMultiplyRotateAccumulate(const Term::Ptr &CipherTextOperand,
                               const std::vector<Term::Ptr> &PlaintextOperands,
                               std::vector<std::int32_t> RotationIndices) {
    if (CipherTextOperand->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Multiply Rotate Accumulate called on a term that needs "
          "relinearization. Operand of Rotate Needs to be relinearized");
    }
    if (CipherTextOperand->getType() != Term::Type::Cipher) {
      throw std::runtime_error("ERROR: Multiply Rotate Accumulate CipherText "
                               "Operand of Invalid Type");
    }
    if (CipherTextOperand->getEphemeralKey()) {
      throw std::runtime_error(
          "ERROR: Multiply Rotate Accumulate called on a term that is "
          "encrypted with the ephemeral key. Operand of Rotate Needs to be "
          "encrypted with the standard key");
    }
    if (RotationIndices.size() <= 1) {
      throw std::runtime_error(
          "Must have atleast 2 Rotation Indices for RotateMultiplyAcc");
    }
    if (PlaintextOperands.size() != RotationIndices.size()) {
      throw std::runtime_error(
          "Must have one plaintext operand for every rotation index");
    }
    auto plaintextScale = PlaintextOperands.at(0)->getScale();
    auto ciphertextLevel = CipherTextOperand->getLevel();
    for (auto &plaintext : PlaintextOperands) {
      if (plaintextScale != plaintext->getScale()) {
        throw std::runtime_error(
            "All plaintext operands must have the same scale");
      }
      if (ciphertextLevel != plaintext->getLevel()) {
        throw std::runtime_error(
            "Ciphertext Level and plaintext Level must be equal");
      }
      if (plaintext->getType() != Term::Type::Plain) {
        throw std::runtime_error("ERROR: Multiply Rotate Accumulate Plaintext "
                                 "Operand of Invalid Type");
      }
    }

    std::vector<Term::Ptr> operands;
    operands.push_back(CipherTextOperand);
    operands.insert(operands.end(), PlaintextOperands.begin(),
                    PlaintextOperands.end());
    auto mulRotAcc = makeTerm(Op::MulRotAcc, Term::Type::Cipher,
                              CipherTextOperand->getScale() + plaintextScale,
                              CipherTextOperand->getLevel(), false, operands);
    mulRotAcc->set<RotMulAccRotationAttribute>(RotationIndices);
    return mulRotAcc;
  }

  Term::Ptr makeRotateAccumulate(const Term::Ptr &CipherTextOperand,
                                 std::vector<std::int32_t> RotationIndices) {
    if (CipherTextOperand->getNeedsRelinearization()) {
      throw std::runtime_error("ERROR: Rotate Accumulate called on a term that "
                               "needs relinearization. Operand of Rotate "
                               "Multiply Needs to be relinearized");
    }
    if (CipherTextOperand->getType() != Term::Type::Cipher) {
      throw std::runtime_error(
          "ERROR: Rotate Accumulate CipherText Operand of Invalid Type");
    }
    if (CipherTextOperand->getEphemeralKey()) {
      throw std::runtime_error(
          "ERROR: Rotate Accumulate called on a term that is encrypted with "
          "the ephemeral key. Operand of Rotate Needs to be encrypted with the "
          "standard key");
    }
    if (RotationIndices.size() <= 1) {
      throw std::runtime_error(
          "Must have atleast 2 Rotation Indices for RotateAccumulate");
    }
    auto ciphertextLevel = CipherTextOperand->getLevel();

    std::vector<Term::Ptr> operands;
    operands.push_back(CipherTextOperand);
    auto rotAcc =
        makeTerm(Op::RotAcc2, Term::Type::Cipher, CipherTextOperand->getScale(),
                 CipherTextOperand->getLevel(), false, operands);
    rotAcc->set<RotAccRotationAttribute>(RotationIndices);
    return rotAcc;
  }

  Term::Ptr makeBsgsMultiplyAccumulate(
      const Term::Ptr &CipherTextOperand,
      const std::vector<Term::Ptr> &PlaintextOperands,
      const std::vector<std::int32_t> &BabyStepRotationIndicies,
      const std::vector<std::int32_t> &GiantStepRotationIndices) {
    if (CipherTextOperand->getType() != Term::Type::Cipher) {
      throw std::runtime_error(
          "ERROR: BSGS Multiply Accumulate CipherText Operand of Invalid Type");
    }
    if (CipherTextOperand->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Bsgs Multiply Accumulate called on a term that needs "
          "relinearization. Operand of Rotate Needs to be relinearized");
    }
    if (CipherTextOperand->getEphemeralKey()) {
      throw std::runtime_error(
          "ERROR: Bsgs Multiply Accumulate called on a term that is encrypted "
          "with the ephemeral key. Operand of Rotate Needs to be encrypted "
          "with the standard key");
    }
    if (BabyStepRotationIndicies.size() <= 1) {
      throw std::runtime_error(
          "Must have atleast 2 Baby Step Rotation Indices for RotMulAcc");
    }
    if (PlaintextOperands.size() !=
        GiantStepRotationIndices.size() * BabyStepRotationIndicies.size()) {
      throw std::runtime_error("Incorrect Number of Plaintext Operands for "
                               "Specified Baby Step operation Giant Step");
    }

    auto plaintextScale = PlaintextOperands.at(0)->getScale();
    auto ciphertextLevel = CipherTextOperand->getLevel();
    for (auto &plaintext : PlaintextOperands) {
      if (plaintextScale != plaintext->getScale()) {
        throw std::runtime_error(
            "All plaintext operands must have the same scale");
      }
      if (ciphertextLevel != plaintext->getLevel()) {
        throw std::runtime_error(
            "Ciphertext Level and plaintext Level must be equal");
      }
      if (plaintext->getType() != Term::Type::Plain) {
        throw std::runtime_error("ERROR: BSGS Multiply Accumulate Plaintext "
                                 "Operand of Invalid Type");
      }
    }

    std::vector<Term::Ptr> operands;
    operands.push_back(CipherTextOperand);
    operands.insert(operands.end(), PlaintextOperands.begin(),
                    PlaintextOperands.end());
    auto bsgsMulAcc = makeTerm(Op::BsgsMulAcc, Term::Type::Cipher,
                               CipherTextOperand->getScale() + plaintextScale,
                               CipherTextOperand->getLevel(), false, operands);
    bsgsMulAcc->set<BsgsMulAccBabyStepAttribute>(BabyStepRotationIndicies);
    bsgsMulAcc->set<BsgsMulAccGiantStepAttribute>(GiantStepRotationIndices);
    return bsgsMulAcc;
  }

  Term::Ptr makeRescale(const Term::Ptr &term) {
    if (term->getType() != Term::Type::Cipher) {
      throw std::runtime_error(
          "ERROR: Rescale called on operand not of type Cipher");
    }
    if (term->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Rescale called on term that needs relinearization");
    }
    auto fixedRescale = rnsBitSize;
    if (term->getScale() <= fixedRescale) {
      throw std::runtime_error("Rescale results in a term with 0 scale");
    }
    auto newScale = term->getScale() - fixedRescale;
    if(term->getLevel() <= 1){
      throw std::runtime_error("Rescale results in a term with level 0");
    }
    // auto newLevel = term->getLevel() + 1;
    auto newLevel = term->getLevel() - 1;
    auto rescale = makeTerm(Op::Rescale, term->getType(), newScale, newLevel,
                            term->getEphemeralKey(), {term});
    return rescale;
  }

  Term::Ptr makeRelinearize(const Term::Ptr &term) {
    if (term->getType() != Term::Type::Cipher) {
      throw std::runtime_error(
          "ERROR: Relinearize called on operand not of type Cipher");
    }
    if (!term->getNeedsRelinearization()) {
      throw std::runtime_error("ERROR: Relinearization called on term that "
                               "does not need Relinearization");
    }
    if (term->getEphemeralKey()) {
      throw std::runtime_error("ERROR: Relinearization called on term that is "
                               "encrypted with the ephemeral key");
    }
    auto relin = makeTerm(Op::Relinearize, Term::Type::Cipher, term->getScale(),
                          term->getLevel(), false, {term});
    relin->setNeedsRelinearization(false);
    return relin;
  }

  Term::Ptr makeRelinearize2(const Term::Ptr &term) {
    if (term->getType() != Term::Type::Cipher) {
      throw std::runtime_error(
          "ERROR: Relinearize called on operand not of type Cipher");
    }
    if (!term->getNeedsRelinearization()) {
      throw std::runtime_error("ERROR: Relinearization called on term that "
                               "does not need Relinearization");
    }
    if (term->getEphemeralKey()) {
      throw std::runtime_error("ERROR: Relinearization called on term that is "
                               "encrypted with the ephemeral key");
    }
    auto relin = makeTerm(Op::Relinearize2, Term::Type::Cipher,
                          term->getScale(), term->getLevel(), false, {term});
    relin->setNeedsRelinearization(false);
    return relin;
  }

  Term::Ptr makeToEphemeral(const Term::Ptr &term) {
    if (term->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: Ephemeral called on a term that needs relinearization. "
          "Operand of needs to be relineraised");
    }
    if (term->getEphemeralKey()) {
      throw std::runtime_error("ERROR: To Ephemeral called on term that is "
                               "encrypted with the ephemeral key");
    }
    auto ephemeral = makeTerm(Op::ToEphemeral, term->getType(),
                              term->getScale(), term->getLevel(), true, {term});
    return ephemeral;
  }

  Term::Ptr makeModSwitch(const Term::Ptr &term) {
    // auto newLevel = term->getLevel() + 1;
    if (term->getLevel() <= 1) {
      throw std::runtime_error("ERROR: ModSwitch will result in a term with level 0");
    }
    auto newLevel = term->getLevel() - 1;
    auto modSwitch = makeTerm(Op::ModSwitch, term->getType(), term->getScale(),
                              newLevel, term->getEphemeralKey(), {term});
    modSwitch->setNeedsRelinearization(term->getNeedsRelinearization());
    return modSwitch;
  }

  Term::Ptr makeBootstrapModRaise(const Term::Ptr &term,
                                  const Term::Level_t newLevel) {
    if (term->getType() != Term::Type::Cipher) {
      throw std::runtime_error(
          "ERROR: BootstrapModRaise called on operand not of type Cipher");
    }
    if (term->getNeedsRelinearization()) {
      throw std::runtime_error(
          "ERROR: BootstrapModRaise called on term needs to be reliniearized");
    }
    if (!term->getEphemeralKey()) {
      throw std::runtime_error("ERROR: To BootstrapModRaise called on term "
                               "that is not encrypted with the ephemeral key");
    }
    auto modRaise = makeTerm(Op::BootstrapModRaise, Term::Type::Cipher,
                             term->getScale(), newLevel, false, {term});
    modRaise->set<ModRaiseLevelAttribute>(newLevel);
    return modRaise;
  }

  Term::Ptr getInput(std::string name) const {
    if (inputs.find(name) == inputs.end()) {
      std::stringstream s;
      s << "No input named " << name;
      throw std::out_of_range(s.str());
    }
    return inputs.at(name);
  }

  const auto &getInputs() const { return inputs; }

  const auto &getOutputs() const { return outputs; }

  const auto &getTerms() const { return terms; }

  std::string getName() const { return name; }
  void setName(std::string newName) { name = newName; }

  std::uint8_t getCurrentPartitionSize() const { return currentPartitionSize; }

  std::uint8_t getCurrentPartitionId() const { return currentPartitionId; }

  void setCurrentPartitionSize(std::uint8_t size) {
    currentPartitionSize = size;
  }

  void setCurrentPartitionId(std::uint8_t id) { currentPartitionId = id; }

  std::vector<Term::Ptr> getSources() const;

  std::vector<Term::Ptr> getSinks() const;

  std::vector<Term::Ptr> getNewSources() const;

  std::vector<Term::Ptr> getNewSinks() const;

  void emptyNewSources();

  void emptyNewSinks();

  std::string toDOT() const;
  std::string dump(TermMapOptional<std::uint32_t> &scales, TermMap<Type> &types,
                   TermMap<std::uint32_t> &level) const;

  std::uint64_t numTerms() const;

private:
  std::uint64_t allocateIndex();
  void initTermMap(TermMapBase &termMap);
  void registerTermMap(TermMapBase *annotation);
  void unregisterTermMap(TermMapBase *annotation);

  std::string name;

  std::uint32_t rnsBitSize;

  std::uint8_t currentPartitionSize;
  std::uint8_t currentPartitionId;

  // These are managed automatically by Term
  std::unordered_set<Term *> sources;
  std::unordered_set<Term *> sinks;

  // hold constant terms created during transformations
  std::vector<Term::Ptr> newSources;
  std::vector<Term::Ptr> newSinks;

  std::uint64_t nextTermIndex;
  std::vector<TermMapBase *> termMaps;

  std::vector<Term::Ptr> terms;

  // These members must currently be last, because their destruction triggers
  // associated Terms to be destructed, which still use the sources and sinks
  // structures above.
  // TODO: move away from shared ownership for Terms and have Program own them
  // uniquely. It is an error to hold onto a Term longer than a Program, but
  // the shared_ptr is misleading on this regard.
  std::unordered_map<std::string, Term::Ptr> outputs;
  std::unordered_map<std::string, Term::Ptr> inputs;

  friend class Term;
  friend class TermMapBase;
};

} // namespace Frontend
} // namespace Cinnamon

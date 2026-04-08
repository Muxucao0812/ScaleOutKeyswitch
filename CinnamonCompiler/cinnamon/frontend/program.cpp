// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#include "cinnamon/frontend/program.h"
#include "cinnamon/frontend/term_map.h"
#include "cinnamon/util/logging.h"
#include "cinnamon/util/program_traversal.h"
#include <stack>

using namespace std;

namespace Cinnamon {
namespace Frontend {

// TODO: maybe replace with smart iterator to avoid allocation
vector<Term::Ptr> toTermPtrs(const unordered_set<Term *> &terms) {
  vector<Term::Ptr> termPtrs;
  termPtrs.reserve(terms.size());
  for (auto &term : terms) {
    termPtrs.emplace_back(term->shared_from_this());
  }
  return termPtrs;
}

vector<Term::Ptr> Program::getSources() const {
  return toTermPtrs(this->sources);
}

vector<Term::Ptr> Program::getSinks() const { return toTermPtrs(this->sinks); }

vector<Term::Ptr> Program::getNewSources() const { return this->newSources; }

vector<Term::Ptr> Program::getNewSinks() const { return this->newSinks; }

void Program::emptyNewSources() { this->newSources.clear(); }

void Program::emptyNewSinks() { this->newSinks.clear(); }

uint64_t Program::allocateIndex() {
  // TODO: reuse released indices to save space in TermMap instances
  uint64_t index = nextTermIndex++;
  for (TermMapBase *termMap : termMaps) {
    termMap->resize(nextTermIndex);
  }
  return index;
}

void Program::initTermMap(TermMapBase &termMap) {
  termMap.resize(nextTermIndex);
}

void Program::registerTermMap(TermMapBase *termMap) {
  termMaps.emplace_back(termMap);
}

void Program::unregisterTermMap(TermMapBase *termMap) {
  auto iter = find(termMaps.begin(), termMaps.end(), termMap);
  if (iter == termMaps.end()) {
    throw runtime_error("TermMap to unregister not found");
  } else {
    termMaps.erase(iter);
  }
}

template <class Attr>
void dumpAttribute(stringstream &s, Term *term, std::string label) {
  if (term->has<Attr>()) {
    s << ", " << label << "=" << term->get<Attr>();
  }
}

// Print an attribute in DOT format as a box outside the term
template <class Attr>
void toDOTAttributeAsNode(stringstream &s, Term *term, std::string label) {
  if (term->has<Attr>()) {
    s << "t" << term->index << "_" << getAttributeName(Attr::key)
      << " [shape=box label=\"" << label << "=" << term->get<Attr>()
      << "\"];\n";
    s << "t" << term->index << "_" << getAttributeName(Attr::key) << " -> t"
      << term->index << ";\n";
  }
}

string Program::dump(TermMapOptional<std::uint32_t> &scales,
                     TermMap<Type> &types,
                     TermMap<std::uint32_t> &level) const {
  // TODO: switch to use a non-parallel generic traversal
  stringstream s;
  s << getName() << "(){\n";

  // Add all terms in topologically sorted order
  uint64_t nextIndex = 0;
  unordered_map<Term *, uint64_t> indices;
  stack<pair<bool, Term *>> work;
  for (const auto &sink : getSinks()) {
    work.emplace(true, sink.get());
  }
  while (!work.empty()) {
    bool visit = work.top().first;
    auto term = work.top().second;
    work.pop();
    if (indices.count(term)) {
      continue;
    }
    if (visit) {
      work.emplace(false, term);
      for (const auto &operand : term->getOperands()) {
        work.emplace(true, operand.get());
      }
    } else {
      auto index = nextIndex;
      nextIndex += 1;
      indices[term] = index;
      s << "t" << term->index << " = " << getOpName(term->getOp());
      if (term->has<RotationAttribute>()) {
        s << "(" << term->get<RotationAttribute>() << ")";
      }
      if (term->has<TypeAttribute>()) {
        s << ":" << getTypeName(term->get<TypeAttribute>());
      }
      if (term->has<RotMulAccRotationAttribute>()) {
        s << "(";
        auto &rotationIndices = term->get<RotMulAccRotationAttribute>();
        assert(rotationIndices.size() > 1);
        s << rotationIndices[0];
        for (int i = 1; i < rotationIndices.size(); i++) {
          s << ", " << rotationIndices[i];
        }
        s << ")";
      }
      for (int i = 0; i < term->numOperands(); ++i) {
        s << " t" << term->operandAt(i)->index;
      }
      if (types[*term] == Type::Cipher)
        s << ", "
          << "s"
          << "=" << scales[*term] << ", t=cipher ";
      else
        s << ", "
          << "s"
          << "=" << scales[*term] << ", t=plain ";
      s << "\n";
      // ConstantValue TODO: printing constant values for simple cases
    }
  }

  s << "}\n";
  return s.str();
}

string Program::toDOT() const {
  // TODO: switch to use a non-parallel generic traversal
  stringstream s;

  s << "digraph \"" << getName() << "\" {\n";

  // Add all terms in topologically sorted order
  uint64_t nextIndex = 0;
  unordered_map<Term *, uint64_t> indices;
  stack<pair<bool, Term *>> work;
  for (const auto &sink : getSinks()) {
    work.emplace(true, sink.get());
  }
  while (!work.empty()) {
    bool visit = work.top().first;
    auto term = work.top().second;
    work.pop();
    if (indices.count(term)) {
      continue;
    }
    if (visit) {
      work.emplace(false, term);
      for (const auto &operand : term->getOperands()) {
        work.emplace(true, operand.get());
      }
    } else {
      auto index = nextIndex;
      nextIndex += 1;
      indices[term] = index;

      // Operands are guaranteed to have been added
      s << "t" << term->index << " [label=\"" << getOpName(term->getOp());
      if (term->has<RotationAttribute>()) {
        s << "(" << term->get<RotationAttribute>() << ")";
      }
      if (term->has<TypeAttribute>()) {
        s << " : " << getTypeName(term->get<TypeAttribute>());
      }
      if (term->has<RotMulAccRotationAttribute>()) {
        s << "(";
        auto &rotationIndices = term->get<RotMulAccRotationAttribute>();
        assert(rotationIndices.size() > 1);
        s << rotationIndices[0];
        for (int i = 1; i < rotationIndices.size(); i++) {
          s << ", " << rotationIndices[i];
        }
        s << ")";
      }
      if (term->has<BsgsMulAccBabyStepAttribute>()) {
        s << "(";
        auto &rotationIndices = term->get<BsgsMulAccBabyStepAttribute>();
        assert(rotationIndices.size() > 1);
        s << "BabySteps: " << rotationIndices[0];
        for (int i = 1; i < rotationIndices.size(); i++) {
          s << ", " << rotationIndices[i];
        }
        s << ")";
      }
      if (term->has<BsgsMulAccGiantStepAttribute>()) {
        s << "(";
        auto &rotationIndices = term->get<BsgsMulAccGiantStepAttribute>();
        assert(rotationIndices.size() > 1);
        s << "GiantSteps: " << rotationIndices[0];
        for (int i = 1; i < rotationIndices.size(); i++) {
          s << ", " << rotationIndices[i];
        }
        s << ")";
      }
      s << "\""; // End label
      s << "];\n";
      for (int i = 0; i < term->numOperands(); ++i) {
        s << "t" << term->operandAt(i)->index << " -> t" << term->index
          << " [label=\"" << i << "\"];\n";
      }
    }
  }

  s << "}\n";

  return s.str();
}

std::uint64_t Program::numTerms() const { return nextTermIndex; }

} // namespace Frontend
} // namespace Cinnamon

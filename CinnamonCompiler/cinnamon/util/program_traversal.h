// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "cinnamon/frontend/program.h"
#include "cinnamon/frontend/term_map.h"
#include "cinnamon/util/logging.h"
#include <stdexcept>
#include <vector>

namespace Cinnamon {
using namespace Frontend;

/*
Implements efficient forward and backward traversals of Program in the
presence of modifications during traversal.
The rewriter is called for each term in the Program exactly once.
Rewriters must not modify the Program in such a way that terms that are
not uses/operands (for forward/backward traversal, respectively) of the
current term are enabled. With such modifications the whole program is
not guaranteed to be traversed.
*/
class ProgramTraversal {
  Program &program;

  TermMap<bool> ready;
  TermMap<bool> processed;

  bool heartbeat = false;
  uint64_t total_terms = 0;
  uint64_t terms_processed = 0;
  uint64_t percent_complete = 0;

  template <bool isForward> bool arePredecessorsDone(const Term::Ptr &term) {
    for (auto &operand : isForward ? term->getOperands() : term->getUses()) {
      if (!processed[operand]) return false;
    }
    return true;
  }

  template <typename Rewriter, bool isForward>
  void traverse(Rewriter &&rewrite) {
    processed.clear();
    ready.clear();

    std::vector<Term::Ptr> readyNodes =
        isForward ? program.getSources() : program.getSinks();
    for (auto &term : readyNodes) {
      ready[term] = true;
    }
    // Used for remembering uses/operands before rewrite is called. Using a
    // vector here is fine because duplicates in the list are handled
    // gracefully.
    std::vector<Term::Ptr> checkList;

    while (readyNodes.size() != 0) {
      // Pop term to transform
      auto term = readyNodes.back();
      readyNodes.pop_back();

      // If this term is removed, we will lose uses/operands of this term.
      // Remember them here for checking readyness after the rewrite.
      checkList.clear();
      for (auto &succ : isForward ? term->getUses() : term->getOperands()) {
        checkList.push_back(succ);
      }

      log(Verbosity::Trace, "Processing term with index=%lu", term->index);
      rewrite(term);
      processed[term] = true;
      if (heartbeat) {
        terms_processed++;
        uint64_t percent_complete_new = (100 * terms_processed) / (total_terms);
        if (percent_complete_new != percent_complete) {
          percent_complete = percent_complete_new;
        }
      }

      // If transform adds new sources/sinks add them to ready terms.
      // for (auto &leaf : isForward ? program.getSources() :
      // program.getSinks()) {
      for (auto &leaf :
           isForward ? program.getNewSources() : program.getNewSinks()) {
        if (!ready[leaf]) {
          readyNodes.push_back(leaf);
          ready[leaf] = true;
        }
      }

      // clear new sources and sinks
      isForward ? program.emptyNewSources() : program.emptyNewSinks();

      // Also check current uses/operands in case any new ones were added.
      for (auto &succ : isForward ? term->getUses() : term->getOperands()) {
        checkList.push_back(succ);
      }

      // Push and mark uses/operands that are ready to be processed.
      TermMap<bool> visited(program);
      for (auto &succ : checkList) {
        if (!ready[succ]) {
          if (arePredecessorsDone<isForward>(succ)) {
            readyNodes.push_back(succ);
            ready[succ] = true;
            continue;
            ;
          }
          // Perform DFS to find all ready predecessor nodes
          std::vector<Term::Ptr> workList;
          workList.push_back(succ);
          visited[succ] = true;
          while (!workList.empty()) {
            Term::Ptr top = workList.back();
            workList.pop_back();
            const std::vector<Term::Ptr> &preds =
                isForward ? top->getOperands() : top->getUses();
            for (auto &pred : preds) {
              if (visited[pred]) {
                continue;
              }
              visited[pred] = true;
              if (!ready[pred]) {
                if (arePredecessorsDone<isForward>(pred)) {
                  readyNodes.push_back(pred);
                  ready[pred] = true;
                } else {
                  workList.push_back(pred);
                }
              }
            }
          }
        }
      }
    }
  }

public:
  ProgramTraversal(Program &g) : program(g), processed(g), ready(g) {
    total_terms = program.numTerms();
  }
  ProgramTraversal(Program &g, bool heartbeat)
      : program(g), processed(g), ready(g), heartbeat(heartbeat) {
    total_terms = program.numTerms();
  }

  template <typename Rewriter> void forwardPass(Rewriter &&rewrite) {
    traverse<Rewriter, true>(std::forward<Rewriter>(rewrite));
  }

  template <typename Rewriter> void backwardPass(Rewriter &&rewrite) {
    traverse<Rewriter, false>(std::forward<Rewriter>(rewrite));
  }
};

class ProgramTraversalNew {
  Program &program;

  TermMap<bool> ready;
  TermMap<bool> processed;
  TermMap<bool> checked;

  bool heartbeat = false;
  uint64_t total_terms = 0;
  uint64_t terms_processed = 0;
  uint64_t percent_complete = 0;

  template <bool isForward> bool arePredecessorsDone(const Term::Ptr &term) {
    for (auto &operand : isForward ? term->getOperands() : term->getUses()) {
      if (!processed[operand]) return false;
    }
    return true;
  }

  template <typename Rewriter, bool isForward>
  void traverse(Rewriter &&rewrite) {
    processed.clear();
    ready.clear();
    checked.clear();

    auto terms = program.getTerms();
    std::vector<Term::Ptr> checklist;

    if (isForward) {
      for (auto it = terms.begin(); it != terms.end(); it++) {
        // auto term = *it;
        checklist.push_back(*it);
        checked[*it] = true;
        while (!checklist.empty()) {
          auto term = checklist.back();
          if (processed[term]) {
            checklist.pop_back();
            continue;
          }
          if (!arePredecessorsDone<isForward>(term)) {
            for (auto &operand :
                 isForward ? term->getOperands() : term->getUses()) {
              if (!checked[operand]) {
                checklist.push_back(operand);
                checked[operand] = true;
              };
            }
            continue;
          }
          log(Verbosity::Trace, "Processing term with index=%lu", term->index);
          rewrite(term);
          processed[term] = true;
          checklist.pop_back();
          if (heartbeat) {
            terms_processed++;
            uint64_t percent_complete_new =
                (100 * terms_processed) / (total_terms);
            if (percent_complete_new != percent_complete) {
              percent_complete = percent_complete_new;
            }
          }
          if (term->getHasReceiveUse()) {
            for (auto &use : term->getUses()) {
              if (use->getOp() == Op::Receive &&
                  use->getPartitionSize() < term->getPartitionSize()) {
                checklist.push_back(use);
              }
            }
          }
        }
      }
    } else {
      assert(0);
      for (auto it = terms.rbegin(); it != terms.rend(); it++) {
        auto term = *it;
        log(Verbosity::Trace, "Processing term with index=%lu", term->index);
        rewrite(term);
        if (heartbeat) {
          terms_processed++;
          uint64_t percent_complete_new =
              (100 * terms_processed) / (total_terms);
          if (percent_complete_new != percent_complete) {
            percent_complete = percent_complete_new;
          }
        }
      }
    }
  }

public:
  ProgramTraversalNew(Program &g)
      : program(g), processed(g), ready(g), checked(g) {
    total_terms = program.numTerms();
  }
  ProgramTraversalNew(Program &g, bool heartbeat)
      : program(g), processed(g), ready(g), checked(g), heartbeat(heartbeat) {
    total_terms = program.numTerms();
  }

  template <typename Rewriter> void forwardPass(Rewriter &&rewrite) {
    traverse<Rewriter, true>(std::forward<Rewriter>(rewrite));
  }

  template <typename Rewriter> void backwardPass(Rewriter &&rewrite) {
    traverse<Rewriter, false>(std::forward<Rewriter>(rewrite));
  }
};

} // namespace Cinnamon

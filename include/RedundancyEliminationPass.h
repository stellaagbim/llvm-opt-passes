//===- RedundancyEliminationPass.h - Remove Redundant Computations --*- C++ -*-===//
//
// Part of the llvm-opt-passes project
//
// Transformation pass that consumes RedundancyAnalysis results and replaces
// redundant instructions with references to the available dominating value.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPT_PASSES_REDUNDANCY_ELIMINATION_H
#define LLVM_OPT_PASSES_REDUNDANCY_ELIMINATION_H

#include "RedundancyAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"

namespace llvm {
namespace optpasses {

//===----------------------------------------------------------------------===//
// RedundancyEliminationPass
//
// Transformation pass that eliminates redundant computations identified
// by RedundancyAnalysis.
//===----------------------------------------------------------------------===//

class RedundancyEliminationPass 
    : public PassInfoMixin<RedundancyEliminationPass> {
public:
    /// Main entry point for the pass
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

    /// Pass name for registration
    static StringRef name() { return "RedundancyEliminationPass"; }

    /// Enable debug output
    void setDebug(bool Enable) { DebugMode = Enable; }

    /// Statistics
    struct Statistics {
        unsigned InstructionsEliminated = 0;
        unsigned FunctionsProcessed = 0;
    };

    const Statistics& getStatistics() const { return Stats; }

private:
    Statistics Stats;
    bool DebugMode = false;

    /// Perform the elimination
    bool eliminateRedundancies(Function &F, const RedundancyInfo &RI);
};

} // namespace optpasses
} // namespace llvm

#endif // LLVM_OPT_PASSES_REDUNDANCY_ELIMINATION_H

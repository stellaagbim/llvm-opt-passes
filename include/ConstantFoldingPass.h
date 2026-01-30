//===- ConstantFoldingPass.h - Aggressive Constant Folding ------*- C++ -*-===//
//
// Part of the llvm-opt-passes project
//
// Identifies binary ops with constant operands and evaluates them at compile
// time. Uses InstVisitor for traversal and ConstantFoldInstruction for the
// actual folding logic.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPT_PASSES_CONSTANT_FOLDING_H
#define LLVM_OPT_PASSES_CONSTANT_FOLDING_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

namespace llvm {
namespace optpasses {

//===----------------------------------------------------------------------===//
// ConstantFoldingVisitor
//
// Uses the Visitor pattern to traverse instructions and identify candidates
// for constant folding. This exploits SSA form where each value has exactly
// one definition point.
//===----------------------------------------------------------------------===//

class ConstantFoldingVisitor 
    : public InstVisitor<ConstantFoldingVisitor, bool> {
public:
    /// Constructor takes DataLayout for target-specific folding
    explicit ConstantFoldingVisitor(const DataLayout &DL) : DL(DL) {}

    /// Visit a binary operator (add, sub, mul, div, etc.)
    /// Returns true if the instruction is a folding candidate
    bool visitBinaryOperator(BinaryOperator &BO);

    /// Visit cast instructions (zext, sext, trunc, etc.)
    bool visitCastInst(CastInst &CI);

    /// Visit comparison instructions
    bool visitCmpInst(CmpInst &CI);

    /// Visit select instructions (ternary operator)
    bool visitSelectInst(SelectInst &SI);

    /// Visit GEP instructions with constant indices
    bool visitGetElementPtrInst(GetElementPtrInst &GEP);

    /// Default visitor for unhandled instructions
    bool visitInstruction(Instruction &I) { return false; }

    /// Get collected folding candidates
    const std::vector<Instruction*>& getCandidates() const { 
        return FoldingCandidates; 
    }

    /// Clear the candidate list
    void clear() { FoldingCandidates.clear(); }

    /// Statistics
    struct Stats {
        unsigned BinaryOpsFound = 0;
        unsigned CastsFound = 0;
        unsigned ComparisonsFound = 0;
        unsigned SelectsFound = 0;
        unsigned GEPsFound = 0;
    };

    const Stats& getStats() const { return Statistics; }

private:
    const DataLayout &DL;
    std::vector<Instruction*> FoldingCandidates;
    Stats Statistics;

    /// Check if all operands are constants
    bool allOperandsConstant(Instruction &I);
};

//===----------------------------------------------------------------------===//
// ConstantFoldingPass
//
// New Pass Manager compatible transformation pass that performs constant
// folding across an entire function.
//===----------------------------------------------------------------------===//

class ConstantFoldingPass : public PassInfoMixin<ConstantFoldingPass> {
public:
    /// Main entry point for the pass
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

    /// Pass name for registration
    static StringRef name() { return "ConstantFoldingPass"; }

    /// Enable debug output
    void setDebug(bool Enable) { DebugMode = Enable; }

private:
    bool DebugMode = false;

    /// Attempt to fold a single instruction
    /// Returns the folded constant if successful, nullptr otherwise
    Constant* tryFold(Instruction *I, const DataLayout &DL);

    /// Replace instruction uses and mark for deletion
    void replaceAndScheduleRemoval(Instruction *I, Constant *Replacement,
                                   std::vector<Instruction*> &ToDelete);

    /// Print debug information
    void debugPrint(const Twine &Msg) const;
};

} // namespace optpasses
} // namespace llvm

#endif // LLVM_OPT_PASSES_CONSTANT_FOLDING_H

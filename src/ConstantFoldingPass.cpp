//===- ConstantFoldingPass.cpp - Aggressive Constant Folding ----*- C++ -*-===//
//
// InstVisitor-based constant folder. Collects foldable instructions in one
// pass, then folds and deletes in a separate phase to avoid iterator
// invalidation. Iterates to fixed point for chained constants.
//
//===----------------------------------------------------------------------===//

#include "ConstantFoldingPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "constant-folding"

using namespace llvm;
using namespace llvm::optpasses;

//===----------------------------------------------------------------------===//
// ConstantFoldingVisitor Implementation
//===----------------------------------------------------------------------===//

bool ConstantFoldingVisitor::allOperandsConstant(Instruction &I) {
    for (Use &Op : I.operands()) {
        if (!isa<Constant>(Op.get())) {
            return false;
        }
    }
    return true;
}

bool ConstantFoldingVisitor::visitBinaryOperator(BinaryOperator &BO) {
    // Check if both operands are constants
    if (isa<Constant>(BO.getOperand(0)) && isa<Constant>(BO.getOperand(1))) {
        // Verify we can actually fold this (avoid division by zero, etc.)
        if (Constant *C = ConstantFoldInstruction(&BO, DL)) {
            FoldingCandidates.push_back(&BO);
            Statistics.BinaryOpsFound++;
            LLVM_DEBUG(dbgs() << "  Found foldable binary op: " << BO << "\n");
            return true;
        }
    }
    return false;
}

bool ConstantFoldingVisitor::visitCastInst(CastInst &CI) {
    // Cast instructions with constant operands can be folded
    if (isa<Constant>(CI.getOperand(0))) {
        if (Constant *C = ConstantFoldInstruction(&CI, DL)) {
            FoldingCandidates.push_back(&CI);
            Statistics.CastsFound++;
            LLVM_DEBUG(dbgs() << "  Found foldable cast: " << CI << "\n");
            return true;
        }
    }
    return false;
}

bool ConstantFoldingVisitor::visitCmpInst(CmpInst &CI) {
    // Comparison with constant operands
    if (isa<Constant>(CI.getOperand(0)) && isa<Constant>(CI.getOperand(1))) {
        if (Constant *C = ConstantFoldInstruction(&CI, DL)) {
            FoldingCandidates.push_back(&CI);
            Statistics.ComparisonsFound++;
            LLVM_DEBUG(dbgs() << "  Found foldable comparison: " << CI << "\n");
            return true;
        }
    }
    return false;
}

bool ConstantFoldingVisitor::visitSelectInst(SelectInst &SI) {
    // Select with constant condition can be folded
    if (isa<Constant>(SI.getCondition())) {
        if (Constant *C = ConstantFoldInstruction(&SI, DL)) {
            FoldingCandidates.push_back(&SI);
            Statistics.SelectsFound++;
            LLVM_DEBUG(dbgs() << "  Found foldable select: " << SI << "\n");
            return true;
        }
    }
    return false;
}

bool ConstantFoldingVisitor::visitGetElementPtrInst(GetElementPtrInst &GEP) {
    // GEP with all constant indices and constant base
    if (allOperandsConstant(GEP)) {
        if (Constant *C = ConstantFoldInstruction(&GEP, DL)) {
            FoldingCandidates.push_back(&GEP);
            Statistics.GEPsFound++;
            LLVM_DEBUG(dbgs() << "  Found foldable GEP: " << GEP << "\n");
            return true;
        }
    }
    return false;
}

//===----------------------------------------------------------------------===//
// ConstantFoldingPass Implementation
//===----------------------------------------------------------------------===//

void ConstantFoldingPass::debugPrint(const Twine &Msg) const {
    if (DebugMode) {
        errs() << "[ConstantFolding] " << Msg << "\n";
    }
}

Constant* ConstantFoldingPass::tryFold(Instruction *I, const DataLayout &DL) {
    // Use LLVM's ConstantFoldInstruction utility
    // This handles all the edge cases like division by zero, NaN, etc.
    return ConstantFoldInstruction(I, DL);
}

void ConstantFoldingPass::replaceAndScheduleRemoval(
    Instruction *I, Constant *Replacement,
    std::vector<Instruction*> &ToDelete) {
    
    debugPrint("  Replacing: " + I->getName() + " with constant");
    
    // SSA property: replaceAllUsesWith updates all uses across the function
    // This is O(uses) complexity because SSA maintains explicit def-use chains
    I->replaceAllUsesWith(Replacement);
    
    // Schedule for deletion - never delete while iterating
    ToDelete.push_back(I);
}

PreservedAnalyses ConstantFoldingPass::run(Function &F, 
                                           FunctionAnalysisManager &AM) {
    debugPrint("Processing function: " + F.getName());

    const DataLayout &DL = F.getParent()->getDataLayout();
    
    // Phase 1: Identify candidates using the visitor pattern
    ConstantFoldingVisitor Visitor(DL);
    
    // Visit all instructions in the function
    // The visitor collects all foldable instructions
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            Visitor.visit(I);
        }
    }

    const auto &Candidates = Visitor.getCandidates();
    
    if (Candidates.empty()) {
        debugPrint("  No folding candidates found");
        return PreservedAnalyses::all();
    }

    debugPrint("  Found " + Twine(Candidates.size()) + " folding candidates");

    // Phase 2: Fold candidates and collect for deletion
    // We need to iterate until no more folding is possible (fixed point)
    bool Changed = false;
    unsigned TotalFolded = 0;
    
    do {
        Changed = false;
        std::vector<Instruction*> ToDelete;
        
        // Process in reverse order to handle chains of constants
        for (BasicBlock &BB : F) {
            for (Instruction &I : make_early_inc_range(BB)) {
                if (Constant *C = tryFold(&I, DL)) {
                    replaceAndScheduleRemoval(&I, C, ToDelete);
                    Changed = true;
                    TotalFolded++;
                }
            }
        }

        // Phase 3: Delete folded instructions
        for (Instruction *I : ToDelete) {
            I->eraseFromParent();
        }
        
    } while (Changed);  // Repeat until no more folding possible

    debugPrint("  Folded " + Twine(TotalFolded) + " instructions");

    // Print statistics
    const auto &Stats = Visitor.getStats();
    LLVM_DEBUG(dbgs() << "ConstantFolding Statistics:\n"
                      << "  Binary operators: " << Stats.BinaryOpsFound << "\n"
                      << "  Casts: " << Stats.CastsFound << "\n"
                      << "  Comparisons: " << Stats.ComparisonsFound << "\n"
                      << "  Selects: " << Stats.SelectsFound << "\n"
                      << "  GEPs: " << Stats.GEPsFound << "\n");

    // Folding invalidates most analyses
    // DominatorTree and LoopInfo may still be valid if structure unchanged
    if (TotalFolded > 0) {
        return PreservedAnalyses::none();
    }
    
    return PreservedAnalyses::all();
}

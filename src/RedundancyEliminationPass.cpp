//===- RedundancyEliminationPass.cpp - Remove Redundant Computations ------===//
//
// Consumes RedundancyAnalysis, does replaceAllUsesWith + eraseFromParent
// for each flagged instruction.
//
//===----------------------------------------------------------------------===//

#include "RedundancyEliminationPass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "redundancy-elimination"

using namespace llvm;
using namespace llvm::optpasses;

bool RedundancyEliminationPass::eliminateRedundancies(Function &F,
                                                      const RedundancyInfo &RI) {
    if (!RI.hasRedundancies()) {
        return false;
    }
    
    LLVM_DEBUG(dbgs() << "Eliminating " << RI.Statistics.RedundantInstructions
                      << " redundant instructions\n");
    
    // Collect instructions to delete
    // We must not delete while iterating over the IR
    std::vector<Instruction*> ToDelete;
    
    for (const auto &[Redundant, Replacement] : RI.RedundantInstructions) {
        // Verify both instructions are still valid
        // (previous transformations might have changed things)
        if (!Redundant || !Replacement) {
            continue;
        }
        
        // Verify types match
        if (Redundant->getType() != Replacement->getType()) {
            LLVM_DEBUG(dbgs() << "  Type mismatch, skipping: " << *Redundant << "\n");
            continue;
        }
        
        LLVM_DEBUG(dbgs() << "  Replacing: " << *Redundant << "\n"
                          << "       with: " << *Replacement << "\n");
        
        // Replace all uses of redundant instruction with the available value
        // SSA form maintains explicit def-use chains, so this is efficient
        Redundant->replaceAllUsesWith(Replacement);
        
        // Schedule for deletion
        ToDelete.push_back(Redundant);
        Stats.InstructionsEliminated++;
    }
    
    // Now delete all redundant instructions
    for (Instruction *I : ToDelete) {
        I->eraseFromParent();
    }
    
    return !ToDelete.empty();
}

PreservedAnalyses RedundancyEliminationPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
    LLVM_DEBUG(dbgs() << "RedundancyEliminationPass: Processing function "
                      << F.getName() << "\n");
    
    Stats.FunctionsProcessed++;
    
    // Get redundancy analysis results
    const auto &RI = AM.getResult<RedundancyAnalysis>(F);
    
    // Perform elimination
    bool Changed = eliminateRedundancies(F, RI);
    
    LLVM_DEBUG(dbgs() << "  Eliminated " << Stats.InstructionsEliminated 
                      << " instructions\n");
    
    if (!Changed) {
        return PreservedAnalyses::all();
    }
    
    // Elimination invalidates the redundancy analysis (we changed the IR)
    // But it preserves the CFG structure
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    PA.preserve<DominatorTreeAnalysis>();
    return PA;
}

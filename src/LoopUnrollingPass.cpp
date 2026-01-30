//===- LoopUnrollingPass.cpp - Custom Loop Unrolling ------------*- C++ -*-===//
//
// Analyzes loops via LoopInfo + SCEV, determines unroll strategy based on
// trip count and body size, then calls LLVM's UnrollLoop utility.
//
//===----------------------------------------------------------------------===//

#include "LoopUnrollingPass.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "loop-unrolling"

using namespace llvm;
using namespace llvm::optpasses;

//===----------------------------------------------------------------------===//
// LoopAnalyzer Implementation
//===----------------------------------------------------------------------===//

unsigned LoopAnalyzer::computeTripCount(Loop *L) {
    // Use ScalarEvolution to compute the trip count
    // SCEV represents symbolic expressions for induction variables
    
    // getSmallConstantTripCount returns a concrete value if the trip count
    // is a compile-time constant, 0 otherwise
    unsigned TripCount = SE.getSmallConstantTripCount(L);
    
    if (TripCount > 0) {
        LLVM_DEBUG(dbgs() << "  Trip count: " << TripCount << "\n");
        return TripCount;
    }

    // Check if we can at least get a trip multiple
    // (trip count is guaranteed to be a multiple of this value)
    unsigned TripMultiple = SE.getSmallConstantTripMultiple(L);
    LLVM_DEBUG(dbgs() << "  Trip multiple: " << TripMultiple << "\n");
    
    return 0;  // Unknown trip count
}

unsigned LoopAnalyzer::countInstructions(Loop *L) {
    unsigned Count = 0;
    
    for (BasicBlock *BB : L->blocks()) {
        for (Instruction &I : *BB) {
            // Don't count PHI nodes and terminators as heavily
            if (!isa<PHINode>(I) && !I.isTerminator()) {
                Count++;
            }
        }
    }
    
    return Count;
}

bool LoopAnalyzer::isSimpleLoop(Loop *L) {
    // Check for canonical loop form:
    // 1. Single entry (preheader exists)
    // 2. Single backedge (latch exists)
    // 3. Single exit (for simplicity)
    
    if (!L->getLoopPreheader()) {
        LLVM_DEBUG(dbgs() << "  No preheader\n");
        return false;
    }
    
    if (!L->getLoopLatch()) {
        LLVM_DEBUG(dbgs() << "  No unique latch\n");
        return false;
    }
    
    // Check for nested loops - unrolling outer loops is more complex
    if (!L->getSubLoops().empty()) {
        LLVM_DEBUG(dbgs() << "  Contains nested loops\n");
        // We can still unroll, but it's more complex
    }
    
    // Check for multiple exits
    SmallVector<BasicBlock*, 4> ExitBlocks;
    L->getExitBlocks(ExitBlocks);
    if (ExitBlocks.size() > 1) {
        LLVM_DEBUG(dbgs() << "  Multiple exit blocks\n");
        // Not disqualifying, but complicates unrolling
    }
    
    return true;
}

bool LoopAnalyzer::hasSideEffects(Loop *L) {
    for (BasicBlock *BB : L->blocks()) {
        for (Instruction &I : *BB) {
            // Check for calls with potential side effects
            if (auto *CI = dyn_cast<CallInst>(&I)) {
                Function *Callee = CI->getCalledFunction();
                if (!Callee || !Callee->doesNotAccessMemory()) {
                    LLVM_DEBUG(dbgs() << "  Found call with side effects: " << I << "\n");
                    return true;
                }
            }
            
            // Check for volatile operations
            if (I.isVolatile()) {
                LLVM_DEBUG(dbgs() << "  Found volatile operation\n");
                return true;
            }
            
            // Check for atomic operations
            if (I.isAtomic()) {
                LLVM_DEBUG(dbgs() << "  Found atomic operation\n");
                return true;
            }
        }
    }
    
    return false;
}

LoopUnrollCandidate::UnrollStrategy 
LoopAnalyzer::determineStrategy(const LoopUnrollCandidate &Candidate) {
    
    // No unrolling if we have side effects and it's not allowed
    if (Candidate.HasSideEffects && !Config.UnrollLoopsWithCalls) {
        return LoopUnrollCandidate::NoUnroll;
    }
    
    // Full unroll: known trip count, small enough
    if (Candidate.TripCount > 0 && 
        Candidate.TripCount <= Config.FullUnrollMaxCount &&
        Candidate.InstructionCount * Candidate.TripCount <= Config.FullUnrollMaxInstructions) {
        return LoopUnrollCandidate::FullUnroll;
    }
    
    // Partial unroll: known trip count but too large for full unroll
    if (Candidate.TripCount > 0) {
        return LoopUnrollCandidate::PartialUnroll;
    }
    
    // Runtime unroll: unknown trip count
    if (Config.AllowRuntimeUnroll && Candidate.IsSimple) {
        return LoopUnrollCandidate::RuntimeUnroll;
    }
    
    return LoopUnrollCandidate::NoUnroll;
}

unsigned LoopAnalyzer::calculateUnrollFactor(const LoopUnrollCandidate &Candidate) {
    switch (Candidate.Strategy) {
        case LoopUnrollCandidate::FullUnroll:
            return Candidate.TripCount;
            
        case LoopUnrollCandidate::PartialUnroll: {
            // Choose factor that divides trip count evenly if possible
            unsigned Factor = Config.PartialUnrollFactor;
            
            // Adjust factor to divide trip count evenly
            while (Factor > 1 && Candidate.TripCount % Factor != 0) {
                Factor--;
            }
            
            // Check code size limit
            while (Factor > 1 && 
                   Candidate.InstructionCount * Factor > Config.MaxUnrolledSize) {
                Factor--;
            }
            
            return Factor;
        }
            
        case LoopUnrollCandidate::RuntimeUnroll:
            return Config.PartialUnrollFactor;
            
        default:
            return 1;
    }
}

LoopUnrollCandidate LoopAnalyzer::analyzeLoop(Loop *L) {
    LoopUnrollCandidate Candidate(L);
    
    LLVM_DEBUG(dbgs() << "Analyzing loop: " << L->getName() << "\n");
    
    // Gather loop properties
    Candidate.TripCount = computeTripCount(L);
    Candidate.TripMultiple = SE.getSmallConstantTripMultiple(L);
    Candidate.InstructionCount = countInstructions(L);
    Candidate.IsSimple = isSimpleLoop(L);
    Candidate.HasSideEffects = hasSideEffects(L);
    
    LLVM_DEBUG(dbgs() << "  Instructions: " << Candidate.InstructionCount << "\n");
    LLVM_DEBUG(dbgs() << "  Is simple: " << Candidate.IsSimple << "\n");
    LLVM_DEBUG(dbgs() << "  Has side effects: " << Candidate.HasSideEffects << "\n");
    
    // Determine strategy and factor
    Candidate.Strategy = determineStrategy(Candidate);
    Candidate.UnrollFactor = calculateUnrollFactor(Candidate);
    
    LLVM_DEBUG(dbgs() << "  Strategy: " << static_cast<int>(Candidate.Strategy) << "\n");
    LLVM_DEBUG(dbgs() << "  Unroll factor: " << Candidate.UnrollFactor << "\n");
    
    return Candidate;
}

std::vector<LoopUnrollCandidate> LoopAnalyzer::getCandidates() {
    std::vector<LoopUnrollCandidate> Candidates;
    
    // Process loops in post-order (innermost first)
    // This ensures inner loops are unrolled before outer loops
    for (Loop *L : LI) {
        // Get all loops in post-order
        SmallVector<Loop*, 8> Worklist;
        for (Loop *SubLoop : depth_first(L)) {
            Worklist.push_back(SubLoop);
        }
        
        for (Loop *SubLoop : Worklist) {
            LoopUnrollCandidate Candidate = analyzeLoop(SubLoop);
            if (Candidate.Strategy != LoopUnrollCandidate::NoUnroll) {
                Candidates.push_back(Candidate);
            }
        }
    }
    
    return Candidates;
}

//===----------------------------------------------------------------------===//
// LoopUnrollingPass Implementation
//===----------------------------------------------------------------------===//

bool LoopUnrollingPass::unrollLoop(Loop *L, LoopInfo &LI, ScalarEvolution &SE,
                                   DominatorTree &DT, AssumptionCache &AC,
                                   const TargetTransformInfo &TTI,
                                   OptimizationRemarkEmitter &ORE,
                                   const LoopUnrollCandidate &Candidate) {
    
    // Configure unroll options
    UnrollLoopOptions ULO;
    ULO.Count = Candidate.UnrollFactor;
    ULO.Force = false;
    ULO.AllowExpensiveTripCount = false;
    ULO.UnrollRemainder = (Candidate.Strategy == LoopUnrollCandidate::RuntimeUnroll);
    ULO.ForgetAllSCEV = false;
    
    // Set trip count if known
    if (Candidate.TripCount > 0) {
        ULO.TripCount = Candidate.TripCount;
    }
    ULO.TripMultiple = Candidate.TripMultiple;

    LLVM_DEBUG(dbgs() << "Attempting to unroll loop with factor " 
                      << ULO.Count << "\n");

    // Perform the unroll
    // The unrollLoop function from LLVM handles all the complexity
    LoopUnrollResult Result = UnrollLoop(
        L,
        ULO,
        &LI,
        &SE,
        &DT,
        &AC,
        &TTI,
        &ORE,
        true  // PreserveLCSSA
    );

    return Result != LoopUnrollResult::Unmodified;
}

void LoopUnrollingPass::emitRemark(OptimizationRemarkEmitter &ORE, Loop *L,
                                   const LoopUnrollCandidate &Candidate, 
                                   bool Success) {
    if (Success) {
        ORE.emit([&]() {
            return OptimizationRemark(DEBUG_TYPE, "Unrolled", L->getStartLoc(),
                                      L->getHeader())
                   << "unrolled loop by factor " << ore::NV("Factor", Candidate.UnrollFactor);
        });
    } else {
        ORE.emit([&]() {
            return OptimizationRemarkMissed(DEBUG_TYPE, "NotUnrolled", 
                                            L->getStartLoc(), L->getHeader())
                   << "failed to unroll loop";
        });
    }
}

PreservedAnalyses LoopUnrollingPass::run(Function &F, 
                                         FunctionAnalysisManager &AM) {
    
    LLVM_DEBUG(dbgs() << "LoopUnrollingPass: Processing function " 
                      << F.getName() << "\n");

    // Get required analyses from the analysis manager
    auto &LI = AM.getResult<LoopAnalysis>(F);
    auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
    auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
    auto &TTI = AM.getResult<TargetIRAnalysis>(F);
    auto &AC = AM.getResult<AssumptionAnalysis>(F);
    auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);

    // No loops? Nothing to do
    if (LI.empty()) {
        LLVM_DEBUG(dbgs() << "  No loops found\n");
        return PreservedAnalyses::all();
    }

    // Analyze all loops
    LoopAnalyzer Analyzer(LI, SE, TTI, Config);
    std::vector<LoopUnrollCandidate> Candidates = Analyzer.getCandidates();
    
    Stats.LoopsAnalyzed += LI.getLoopsInPreorder().size();

    if (Candidates.empty()) {
        LLVM_DEBUG(dbgs() << "  No unrolling candidates found\n");
        return PreservedAnalyses::all();
    }

    LLVM_DEBUG(dbgs() << "  Found " << Candidates.size() 
                      << " unrolling candidates\n");

    bool Changed = false;

    // Process candidates (innermost first due to post-order traversal)
    for (const auto &Candidate : Candidates) {
        // Re-check that the loop still exists (previous unrolling may have deleted it)
        Loop *L = Candidate.L;
        if (!LI.getLoopFor(L->getHeader())) {
            continue;
        }

        bool Success = unrollLoop(L, LI, SE, DT, AC, TTI, ORE, Candidate);
        
        if (Success) {
            Changed = true;
            switch (Candidate.Strategy) {
                case LoopUnrollCandidate::FullUnroll:
                    Stats.LoopsFullyUnrolled++;
                    break;
                case LoopUnrollCandidate::PartialUnroll:
                    Stats.LoopsPartiallyUnrolled++;
                    break;
                case LoopUnrollCandidate::RuntimeUnroll:
                    Stats.LoopsRuntimeUnrolled++;
                    break;
                default:
                    break;
            }
        } else {
            Stats.LoopsSkipped++;
        }
        
        emitRemark(ORE, L, Candidate, Success);
    }

    LLVM_DEBUG(dbgs() << "LoopUnrolling Statistics:\n"
                      << "  Loops analyzed: " << Stats.LoopsAnalyzed << "\n"
                      << "  Fully unrolled: " << Stats.LoopsFullyUnrolled << "\n"
                      << "  Partially unrolled: " << Stats.LoopsPartiallyUnrolled << "\n"
                      << "  Runtime unrolled: " << Stats.LoopsRuntimeUnrolled << "\n"
                      << "  Skipped: " << Stats.LoopsSkipped << "\n");

    if (!Changed) {
        return PreservedAnalyses::all();
    }

    // Unrolling invalidates several analyses
    PreservedAnalyses PA;
    PA.preserve<DominatorTreeAnalysis>();
    // LoopInfo and SCEV are invalidated
    return PA;
}

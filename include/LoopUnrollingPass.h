//===- LoopUnrollingPass.h - Custom Loop Unrolling --------------*- C++ -*-===//
//
// Part of the llvm-opt-passes project
//
// Uses LoopInfo + ScalarEvolution to find loops with computable trip counts,
// then applies full/partial/runtime unrolling based on cost heuristics.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPT_PASSES_LOOP_UNROLLING_H
#define LLVM_OPT_PASSES_LOOP_UNROLLING_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

namespace llvm {
namespace optpasses {

//===----------------------------------------------------------------------===//
// LoopUnrollCandidate
//
// Represents a loop that has been analyzed for unrolling potential.
//===----------------------------------------------------------------------===//

struct LoopUnrollCandidate {
    Loop *L;
    unsigned TripCount;           // 0 if unknown
    unsigned TripMultiple;        // Trip count is multiple of this
    unsigned InstructionCount;    // Number of instructions in loop body
    bool IsSimple;                // No complex control flow
    bool HasSideEffects;          // Contains calls with side effects
    
    enum UnrollStrategy {
        FullUnroll,               // Completely unroll (TripCount known and small)
        PartialUnroll,            // Unroll by factor (TripCount known but large)
        RuntimeUnroll,            // Generate epilogue for unknown trip count
        NoUnroll                  // Not profitable to unroll
    } Strategy;

    unsigned UnrollFactor;        // How many times to unroll

    LoopUnrollCandidate(Loop *L) 
        : L(L), TripCount(0), TripMultiple(1), InstructionCount(0),
          IsSimple(true), HasSideEffects(false), 
          Strategy(NoUnroll), UnrollFactor(1) {}
};

//===----------------------------------------------------------------------===//
// LoopUnrollConfig
//
// Configuration parameters for the unrolling pass.
//===----------------------------------------------------------------------===//

struct LoopUnrollConfig {
    /// Maximum trip count for full unrolling
    unsigned FullUnrollMaxCount = 8;
    
    /// Maximum instruction count for full unrolling
    unsigned FullUnrollMaxInstructions = 100;
    
    /// Default partial unroll factor
    unsigned PartialUnrollFactor = 4;
    
    /// Maximum partial unroll factor
    unsigned MaxPartialUnrollFactor = 8;
    
    /// Enable runtime unrolling for unknown trip counts
    bool AllowRuntimeUnroll = true;
    
    /// Minimum trip count for runtime unrolling to be profitable
    unsigned RuntimeUnrollMinTripCount = 4;
    
    /// Enable unrolling of loops with side effects (calls)
    bool UnrollLoopsWithCalls = false;
    
    /// Cost threshold (instruction count increase limit)
    unsigned MaxUnrolledSize = 400;
};

//===----------------------------------------------------------------------===//
// LoopAnalyzer
//
// Analyzes loops to determine unrolling candidates using LoopInfo and
// ScalarEvolution.
//===----------------------------------------------------------------------===//

class LoopAnalyzer {
public:
    LoopAnalyzer(LoopInfo &LI, ScalarEvolution &SE, 
                 const TargetTransformInfo &TTI,
                 const LoopUnrollConfig &Config)
        : LI(LI), SE(SE), TTI(TTI), Config(Config) {}

    /// Analyze a loop and return candidate information
    LoopUnrollCandidate analyzeLoop(Loop *L);

    /// Get all unroll candidates in a function (innermost first)
    std::vector<LoopUnrollCandidate> getCandidates();

private:
    LoopInfo &LI;
    ScalarEvolution &SE;
    const TargetTransformInfo &TTI;
    const LoopUnrollConfig &Config;

    /// Compute trip count using SCEV
    unsigned computeTripCount(Loop *L);

    /// Count instructions in loop body
    unsigned countInstructions(Loop *L);

    /// Check if loop has simple structure
    bool isSimpleLoop(Loop *L);

    /// Check for side-effecting instructions
    bool hasSideEffects(Loop *L);

    /// Determine optimal unroll strategy
    LoopUnrollCandidate::UnrollStrategy 
    determineStrategy(const LoopUnrollCandidate &Candidate);

    /// Calculate unroll factor based on strategy
    unsigned calculateUnrollFactor(const LoopUnrollCandidate &Candidate);
};

//===----------------------------------------------------------------------===//
// LoopUnrollingPass
//
// New Pass Manager transformation pass that performs loop unrolling.
//===----------------------------------------------------------------------===//

class LoopUnrollingPass : public PassInfoMixin<LoopUnrollingPass> {
public:
    /// Constructor with optional custom configuration
    explicit LoopUnrollingPass(LoopUnrollConfig Config = LoopUnrollConfig())
        : Config(Config) {}

    /// Main entry point for the pass
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

    /// Pass name for registration
    static StringRef name() { return "LoopUnrollingPass"; }

    /// Set configuration
    void setConfig(const LoopUnrollConfig &NewConfig) { Config = NewConfig; }

    /// Enable debug output
    void setDebug(bool Enable) { DebugMode = Enable; }

    /// Statistics structure
    struct Statistics {
        unsigned LoopsAnalyzed = 0;
        unsigned LoopsFullyUnrolled = 0;
        unsigned LoopsPartiallyUnrolled = 0;
        unsigned LoopsRuntimeUnrolled = 0;
        unsigned LoopsSkipped = 0;
    };

    const Statistics& getStatistics() const { return Stats; }

private:
    LoopUnrollConfig Config;
    Statistics Stats;
    bool DebugMode = false;

    /// Perform unrolling on a single loop
    bool unrollLoop(Loop *L, LoopInfo &LI, ScalarEvolution &SE,
                    DominatorTree &DT, AssumptionCache &AC,
                    const TargetTransformInfo &TTI,
                    OptimizationRemarkEmitter &ORE,
                    const LoopUnrollCandidate &Candidate);

    /// Emit optimization remarks
    void emitRemark(OptimizationRemarkEmitter &ORE, Loop *L,
                    const LoopUnrollCandidate &Candidate, bool Success);
};

} // namespace optpasses
} // namespace llvm

#endif // LLVM_OPT_PASSES_LOOP_UNROLLING_H

//===- RedundancyAnalysis.h - GVN-Based Redundancy Detection ----*- C++ -*-===//
//
// Part of the llvm-opt-passes project
//
// Simplified GVN: assigns value numbers to expressions, checks dominance
// to determine availability, and flags instructions whose values are already
// computed by a dominating instruction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPT_PASSES_REDUNDANCY_ANALYSIS_H
#define LLVM_OPT_PASSES_REDUNDANCY_ANALYSIS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"

#include <unordered_map>
#include <vector>

namespace llvm {
namespace optpasses {

//===----------------------------------------------------------------------===//
// ExpressionKey
//
// Represents an expression for value numbering. Two expressions with the
// same key compute the same value (assuming no side effects).
//===----------------------------------------------------------------------===//

struct ExpressionKey {
    unsigned Opcode;
    std::vector<unsigned> OperandValueNumbers;
    Type *ResultType;
    
    // For comparison instructions
    unsigned Predicate = 0;
    
    // For GEP instructions
    bool InBounds = false;

    bool operator==(const ExpressionKey &Other) const {
        return Opcode == Other.Opcode &&
               OperandValueNumbers == Other.OperandValueNumbers &&
               ResultType == Other.ResultType &&
               Predicate == Other.Predicate &&
               InBounds == Other.InBounds;
    }
};

} // namespace optpasses
} // namespace llvm

// Hash function for ExpressionKey
namespace std {
template<>
struct hash<llvm::optpasses::ExpressionKey> {
    size_t operator()(const llvm::optpasses::ExpressionKey &Key) const {
        size_t H = hash<unsigned>()(Key.Opcode);
        for (unsigned VN : Key.OperandValueNumbers) {
            H ^= hash<unsigned>()(VN) + 0x9e3779b9 + (H << 6) + (H >> 2);
        }
        H ^= hash<void*>()(Key.ResultType) + 0x9e3779b9 + (H << 6) + (H >> 2);
        H ^= hash<unsigned>()(Key.Predicate);
        H ^= hash<bool>()(Key.InBounds);
        return H;
    }
};
} // namespace std

namespace llvm {
namespace optpasses {

//===----------------------------------------------------------------------===//
// ValueNumberTable
//
// Maps values to their value numbers and expressions to defining instructions.
//===----------------------------------------------------------------------===//

class ValueNumberTable {
public:
    ValueNumberTable() : NextValueNumber(1) {}

    /// Get value number for a value, creating one if necessary
    unsigned getValueNumber(Value *V);

    /// Lookup value number (returns 0 if not found)
    unsigned lookupValueNumber(Value *V) const;

    /// Create expression key for an instruction
    ExpressionKey createExpressionKey(Instruction *I);

    /// Lookup existing computation with same expression
    /// Returns the instruction if found and it dominates the query point
    Instruction* findAvailableValue(const ExpressionKey &Key,
                                    Instruction *QueryPoint,
                                    DominatorTree &DT);

    /// Add expression to the table
    void addExpression(const ExpressionKey &Key, Instruction *I);

    /// Clear the table
    void clear();

    /// Get statistics
    unsigned getNumValueNumbers() const { return NextValueNumber - 1; }
    unsigned getNumExpressions() const { return ExpressionTable.size(); }

private:
    unsigned NextValueNumber;
    DenseMap<Value*, unsigned> ValueNumbers;
    std::unordered_map<ExpressionKey, std::vector<Instruction*>> ExpressionTable;

    /// Canonicalize operand order for commutative operations
    void canonicalizeOperands(std::vector<unsigned> &Operands, unsigned Opcode);

    /// Check if opcode is commutative
    bool isCommutative(unsigned Opcode);
};

//===----------------------------------------------------------------------===//
// RedundancyInfo
//
// Result of redundancy analysis - maps redundant instructions to their
// available replacements.
//===----------------------------------------------------------------------===//

struct RedundancyInfo {
    /// Map from redundant instruction to the equivalent available instruction
    DenseMap<Instruction*, Instruction*> RedundantInstructions;

    /// Statistics
    struct Stats {
        unsigned TotalInstructions = 0;
        unsigned RedundantInstructions = 0;
        unsigned UniqueExpressions = 0;
    } Statistics;

    /// Check if an instruction is redundant
    bool isRedundant(Instruction *I) const {
        return RedundantInstructions.count(I) > 0;
    }

    /// Get the replacement for a redundant instruction
    Instruction* getReplacement(Instruction *I) const {
        auto It = RedundantInstructions.find(I);
        return It != RedundantInstructions.end() ? It->second : nullptr;
    }

    /// Check if analysis found any redundancies
    bool hasRedundancies() const {
        return !RedundantInstructions.empty();
    }
};

//===----------------------------------------------------------------------===//
// RedundancyAnalysis
//
// Analysis pass that identifies redundant computations using value numbering.
//===----------------------------------------------------------------------===//

class RedundancyAnalysis : public AnalysisInfoMixin<RedundancyAnalysis> {
public:
    using Result = RedundancyInfo;

    /// Run the analysis
    Result run(Function &F, FunctionAnalysisManager &AM);

    /// Analysis key for registration
    static AnalysisKey Key;

private:
    /// Check if instruction is analyzable (no side effects, etc.)
    bool isAnalyzable(Instruction *I);

    /// Process a basic block in dominator order
    void processBlock(BasicBlock *BB, ValueNumberTable &VNT,
                      DominatorTree &DT, RedundancyInfo &Result);
};

//===----------------------------------------------------------------------===//
// RedundancyAnalysisPrinterPass
//
// Utility pass to print redundancy analysis results.
//===----------------------------------------------------------------------===//

class RedundancyAnalysisPrinterPass 
    : public PassInfoMixin<RedundancyAnalysisPrinterPass> {
public:
    explicit RedundancyAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

    static StringRef name() { return "RedundancyAnalysisPrinterPass"; }

private:
    raw_ostream &OS;
};

} // namespace optpasses
} // namespace llvm

#endif // LLVM_OPT_PASSES_REDUNDANCY_ANALYSIS_H

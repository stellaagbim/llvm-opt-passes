//===- RedundancyAnalysis.cpp - GVN-Based Redundancy Detection --*- C++ -*-===//
//
// Value numbering pass. Walks domtree in preorder, hashes expressions by
// (opcode, operand VNs), checks if equivalent expression already dominates.
// Commutative ops are canonicalized so (x+y) == (y+x).
//
//===----------------------------------------------------------------------===//

#include "RedundancyAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "redundancy-analysis"

using namespace llvm;
using namespace llvm::optpasses;

// Define the analysis key for registration
AnalysisKey RedundancyAnalysis::Key;

//===----------------------------------------------------------------------===//
// ValueNumberTable Implementation
//===----------------------------------------------------------------------===//

bool ValueNumberTable::isCommutative(unsigned Opcode) {
    switch (Opcode) {
        case Instruction::Add:
        case Instruction::FAdd:
        case Instruction::Mul:
        case Instruction::FMul:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
            return true;
        default:
            return false;
    }
}

void ValueNumberTable::canonicalizeOperands(std::vector<unsigned> &Operands, 
                                            unsigned Opcode) {
    // For commutative operations, sort operands to ensure consistent keys
    // This way, (a + b) and (b + a) get the same value number
    if (isCommutative(Opcode) && Operands.size() == 2) {
        if (Operands[0] > Operands[1]) {
            std::swap(Operands[0], Operands[1]);
        }
    }
}

unsigned ValueNumberTable::getValueNumber(Value *V) {
    // Check if already numbered
    auto It = ValueNumbers.find(V);
    if (It != ValueNumbers.end()) {
        return It->second;
    }
    
    // Assign new value number
    unsigned VN = NextValueNumber++;
    ValueNumbers[V] = VN;
    
    LLVM_DEBUG(dbgs() << "  Assigned VN " << VN << " to: " << *V << "\n");
    
    return VN;
}

unsigned ValueNumberTable::lookupValueNumber(Value *V) const {
    auto It = ValueNumbers.find(V);
    return It != ValueNumbers.end() ? It->second : 0;
}

ExpressionKey ValueNumberTable::createExpressionKey(Instruction *I) {
    ExpressionKey Key;
    Key.Opcode = I->getOpcode();
    Key.ResultType = I->getType();
    
    // Build operand value numbers
    for (Use &Op : I->operands()) {
        Key.OperandValueNumbers.push_back(getValueNumber(Op.get()));
    }
    
    // Canonicalize for commutative operations
    canonicalizeOperands(Key.OperandValueNumbers, Key.Opcode);
    
    // Handle special instruction types
    if (auto *CI = dyn_cast<CmpInst>(I)) {
        Key.Predicate = CI->getPredicate();
    }
    
    if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
        Key.InBounds = GEP->isInBounds();
    }
    
    return Key;
}

Instruction* ValueNumberTable::findAvailableValue(const ExpressionKey &Key,
                                                  Instruction *QueryPoint,
                                                  DominatorTree &DT) {
    auto It = ExpressionTable.find(Key);
    if (It == ExpressionTable.end()) {
        return nullptr;
    }
    
    // Find an instruction that dominates the query point
    for (Instruction *Candidate : It->second) {
        // Skip if same instruction
        if (Candidate == QueryPoint) {
            continue;
        }
        
        // Check if candidate dominates query point
        // A value computed in block B1 is available at B2 if B1 dominates B2
        if (DT.dominates(Candidate, QueryPoint)) {
            LLVM_DEBUG(dbgs() << "  Found available value: " << *Candidate 
                              << " dominates " << *QueryPoint << "\n");
            return Candidate;
        }
    }
    
    return nullptr;
}

void ValueNumberTable::addExpression(const ExpressionKey &Key, Instruction *I) {
    ExpressionTable[Key].push_back(I);
    
    LLVM_DEBUG(dbgs() << "  Added expression for: " << *I << "\n");
}

void ValueNumberTable::clear() {
    NextValueNumber = 1;
    ValueNumbers.clear();
    ExpressionTable.clear();
}

//===----------------------------------------------------------------------===//
// RedundancyAnalysis Implementation
//===----------------------------------------------------------------------===//

bool RedundancyAnalysis::isAnalyzable(Instruction *I) {
    // Skip instructions that can't be analyzed for redundancy
    
    // PHI nodes are handled specially
    if (isa<PHINode>(I)) {
        return false;
    }
    
    // Terminators can't be redundant
    if (I->isTerminator()) {
        return false;
    }
    
    // Skip instructions with side effects
    if (I->mayHaveSideEffects()) {
        return false;
    }
    
    // Skip volatile operations
    if (I->isVolatile()) {
        return false;
    }
    
    // Skip memory operations (loads might be redundant but need alias analysis)
    if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
        return false;
    }
    
    // Skip allocations
    if (isa<AllocaInst>(I)) {
        return false;
    }
    
    // Skip calls (even pure ones, for simplicity)
    if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
        return false;
    }
    
    return true;
}

void RedundancyAnalysis::processBlock(BasicBlock *BB, ValueNumberTable &VNT,
                                      DominatorTree &DT, RedundancyInfo &Result) {
    
    LLVM_DEBUG(dbgs() << "Processing block: " << BB->getName() << "\n");
    
    for (Instruction &I : *BB) {
        Result.Statistics.TotalInstructions++;
        
        // Skip non-analyzable instructions
        if (!isAnalyzable(&I)) {
            // Still assign value numbers for uses
            VNT.getValueNumber(&I);
            continue;
        }
        
        // Create expression key for this instruction
        ExpressionKey Key = VNT.createExpressionKey(&I);
        
        // Look for an equivalent, dominating computation
        Instruction *Available = VNT.findAvailableValue(Key, &I, DT);
        
        if (Available) {
            // Found redundant computation!
            Result.RedundantInstructions[&I] = Available;
            Result.Statistics.RedundantInstructions++;
            
            LLVM_DEBUG(dbgs() << "  REDUNDANT: " << I << "\n"
                              << "    replaced by: " << *Available << "\n");
        } else {
            // New unique expression
            VNT.addExpression(Key, &I);
            Result.Statistics.UniqueExpressions++;
        }
        
        // Assign value number to this instruction
        VNT.getValueNumber(&I);
    }
}

RedundancyInfo RedundancyAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
    LLVM_DEBUG(dbgs() << "RedundancyAnalysis: Processing function " 
                      << F.getName() << "\n");
    
    RedundancyInfo Result;
    
    // Get dominator tree for availability checking
    auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
    
    // Create value number table
    ValueNumberTable VNT;
    
    // Assign value numbers to function arguments first
    for (Argument &Arg : F.args()) {
        VNT.getValueNumber(&Arg);
    }
    
    // Process basic blocks in dominator tree preorder
    // This ensures we process dominating blocks before dominated blocks
    for (auto *Node : depth_first(DT.getRootNode())) {
        BasicBlock *BB = Node->getBlock();
        processBlock(BB, VNT, DT, Result);
    }
    
    LLVM_DEBUG(dbgs() << "RedundancyAnalysis Statistics:\n"
                      << "  Total instructions: " 
                      << Result.Statistics.TotalInstructions << "\n"
                      << "  Redundant: " 
                      << Result.Statistics.RedundantInstructions << "\n"
                      << "  Unique expressions: " 
                      << Result.Statistics.UniqueExpressions << "\n");
    
    return Result;
}

//===----------------------------------------------------------------------===//
// RedundancyAnalysisPrinterPass Implementation
//===----------------------------------------------------------------------===//

PreservedAnalyses RedundancyAnalysisPrinterPass::run(Function &F,
                                                     FunctionAnalysisManager &AM) {
    const auto &RI = AM.getResult<RedundancyAnalysis>(F);
    
    OS << "Redundancy Analysis for function: " << F.getName() << "\n";
    OS << "  Total instructions analyzed: " 
       << RI.Statistics.TotalInstructions << "\n";
    OS << "  Redundant instructions found: " 
       << RI.Statistics.RedundantInstructions << "\n";
    OS << "  Unique expressions: " << RI.Statistics.UniqueExpressions << "\n";
    
    if (RI.hasRedundancies()) {
        OS << "\nRedundant instructions:\n";
        for (const auto &[Redundant, Replacement] : RI.RedundantInstructions) {
            OS << "  " << *Redundant << "\n";
            OS << "    -> can be replaced by: " << *Replacement << "\n";
        }
    }
    OS << "\n";
    
    return PreservedAnalyses::all();
}

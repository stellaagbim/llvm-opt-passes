# LLVM Optimization Pass Suite

**Author:** Stella Agbim

A collection of out-of-tree LLVM optimization passes targeting the **LLVM New Pass Manager (NPM)**. This project implements fundamental scalar optimizations constant folding, loop unrolling, and redundancy elimination demonstrating direct interaction with LLVM's Intermediate Representation (IR), analysis subsystems, and transformation APIs.

## Project Motivation

The primary objective of this project is to explore the mechanics of compiler optimization from first principles. Rather than relying on pre-existing LLVM utilities for transformation, this suite manually handles:
* **IR Traversal & Modification:** Safe iteration and replacement of instructions in Static Single Assignment (SSA) form.
* **Analysis Management:** Leveraging `ScalarEvolution` for loop properties and Dominator Trees for redundancy checks.
* **Pass Pipeline Integration:** Correctly preserving analysis results (`PreservedAnalyses`) to ensure pipeline efficiency and correctness within the NPM.

## Features & Optimization Passes

### 1. Iterative Constant Folding (`custom-constant-fold`)
A fix-point analysis that iteratively folds instructions where all operands are constant. It utilizes `InstVisitor` to traverse the IR and `ConstantFoldInstruction()` for evaluation.

* **Capabilities:** Handles binary operators, casts, integer comparisons (ICmp), select instructions, and constant-index GetElementPtr (GEP).
* **Mechanism:** Runs iteratively until convergence (fixed point) to resolve chained constants.
    * *Example:* `a = 5 + 10` (folds to 15) -> `b = a * 2` (folds to 30).
* **Implementation:** Uses `make_early_inc_range` to safely erase instructions during iteration without invalidating iterators.

### 2. ScalarEvolution-Driven Loop Unrolling (`custom-loop-unroll`)
A loop transformation pass that queries LLVM's `ScalarEvolution` (SCEV) analysis to determine trip counts and applies unrolling strategies based on loop characteristics.

* **Strategy Selection:**
    * **Full Unroll:** Applied if the trip count is known, <= 8, and the loop body is sufficiently small.
    * **Partial Unroll:** Applied with a factor of 4 for larger, known trip counts.
    * **Runtime Unroll:** Generates a loop epilogue to handle unknown trip counts.
* **Processing Order:** Iterates through the loop nest in post-order (innermost loops first) to maximize optimization opportunities.

### 3. GVN-Based Redundancy Elimination (`custom-redundancy-elim`)
A Global Value Numbering (GVN) style optimization implemented in two distinct phases to separate analysis from mutation.

* **Phase 1: RedundancyAnalysis**
    * Walks basic blocks in Dominator Tree preorder.
    * Builds a value number table to identify available expressions.
    * Canonicalizes commutative operations (e.g., treating `add %x, %y` and `add %y, %x` as equivalent).
    * Flags instructions dominated by equivalent, previously computed values.
* **Phase 2: RedundancyEliminationPass**
    * Consumes the analysis result.
    * Performs `replaceAllUsesWith` (RAUW) and instruction erasure.

## Benchmarks & Results

*Benchmarks are executed using the provided `scripts/benchmark.sh` and `test/benchmark.c`.*

* **IR Reduction:** The combined `custom-optimize` pipeline achieves a measurable reduction in static instruction count for arithmetic-heavy code paths.
* **Runtime Performance:**
    * Loop unrolling demonstrates performance gains in tight loops by reducing branch overhead and increasing instruction-level parallelism.
    * Redundancy elimination successfully removes re-computations in control-flow dependent blocks.

## Build Instructions

### Prerequisites
* **LLVM 15+** (Development headers and libraries)
* **CMake 3.13+**
* **C++17 Compiler** (GCC or Clang)

### Compilation

```bash
mkdir build && cd build
cmake ..
make

Platform Specifics:

Ubuntu: apt install llvm-17-dev

macOS: brew install llvm@17 (Ensure LLVM is in your PATH).

Artifacts:

Linux: LLVMOptPasses.so

macOS: LLVMOptPasses.dylib

## Usage

The passes are built as a dynamically loaded plugin for the opt tool.

Running Individual Passes:
opt -load-pass-plugin=./LLVMOptPasses.so -passes="custom-constant-fold" input.ll -S -o output.ll

Running the Full Optimization Pipeline:
opt -load-pass-plugin=./LLVMOptPasses.so -passes="custom-optimize" input.ll -S -o output.ll

Analyzing without Transformation:
# Print redundancy analysis results to stderr
opt -load-pass-plugin=./LLVMOptPasses.so -passes="print<custom-redundancy>" input.ll -disable-output

## Testing & Verification
The project includes a regression test suite and a performance benchmark.
# Run regression tests (FileCheck based)
./scripts/run_tests.sh ./build/LLVMOptPasses.so

# Run performance benchmarks
./scripts/benchmark.sh ./build/LLVMOptPasses.so

## Project Structure
.
├── include/
│   ├── ConstantFoldingPass.h       # Interface for constant folding
│   ├── LoopUnrollingPass.h         # Interface for loop unrolling
│   ├── RedundancyAnalysis.h        # Analysis pass definition
│   └── RedundancyEliminationPass.h # Transformation pass definition
├── scripts/
│   ├── benchmark.sh                # Benchmark runner
│   └── run_tests.sh                # Regression test runner
├── src/
│   ├── PassRegistration.cpp        # NPM Plugin registration callbacks
│   └── ...                         # Pass implementations
├── test/
│   ├── constant_folding.ll         # IR tests for constant propagation
│   ├── loop_unrolling.ll           # IR tests for loop strategies
│   ├── redundancy_elimination.ll   # IR tests for dominance-based elim
│   └── benchmark.c                 # C source for runtime comparison
└── CMakeLists.txt

## Limitations

Memory Operations: Redundancy elimination currently ignores load/store instructions as it does not integrate LLVM's MemorySSA or AliasAnalysis.

Complex Loops: The unroller conservatively bypasses loops with multiple exit blocks to maintain correctness without complex control flow reconstruction.

Target Independence: Constant folding does not currently handle target-specific intrinsics.
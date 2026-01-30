//===- PassRegistration.cpp - Plugin entry point --------------------------===//
//
// llvmGetPassPluginInfo() export for -load-pass-plugin. Registers passes
// with the PassBuilder's pipeline parsing callback.
//
//===----------------------------------------------------------------------===//

#include "ConstantFoldingPass.h"
#include "LoopUnrollingPass.h"
#include "RedundancyAnalysis.h"
#include "RedundancyEliminationPass.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::optpasses;

//===----------------------------------------------------------------------===//
// Pass Registration Callbacks
//
// These callbacks are invoked by the PassBuilder to register our passes
// for use with the -passes command line option.
//===----------------------------------------------------------------------===//

/// Register our function passes with the pass builder
static void registerFunctionPasses(FunctionPassManager &FPM,
                                   ArrayRef<PassBuilder::PipelineElement>) {
    // This callback allows insertion into the function pass pipeline
}

/// Register passes for parsing from command line
static bool registerPipelineParsingCallback(
    StringRef Name, FunctionPassManager &FPM,
    ArrayRef<PassBuilder::PipelineElement>) {
    
    // Constant Folding Pass
    if (Name == "custom-constant-fold") {
        FPM.addPass(ConstantFoldingPass());
        return true;
    }
    
    // Loop Unrolling Pass
    if (Name == "custom-loop-unroll") {
        FPM.addPass(LoopUnrollingPass());
        return true;
    }
    
    // Redundancy Elimination Pass (includes analysis)
    if (Name == "custom-redundancy-elim") {
        FPM.addPass(RedundancyEliminationPass());
        return true;
    }
    
    // Redundancy Analysis Printer (for debugging)
    if (Name == "print<custom-redundancy>") {
        FPM.addPass(RedundancyAnalysisPrinterPass(errs()));
        return true;
    }
    
    // Combined optimization pass
    if (Name == "custom-optimize") {
        // Run passes in optimal order:
        // 1. Constant folding (simplifies expressions)
        // 2. Redundancy elimination (removes duplicates)
        // 3. Loop unrolling (exposes more optimization opportunities)
        FPM.addPass(ConstantFoldingPass());
        FPM.addPass(RedundancyEliminationPass());
        FPM.addPass(LoopUnrollingPass());
        return true;
    }
    
    return false;
}

/// Register analyses
static void registerAnalyses(FunctionAnalysisManager &FAM) {
    FAM.registerPass([]() { return RedundancyAnalysis(); });
}

//===----------------------------------------------------------------------===//
// Plugin Interface
//
// This is the entry point for the plugin loader. It's called when the
// shared library is loaded via -load-pass-plugin.
//===----------------------------------------------------------------------===//

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "LLVMOptPasses",
        LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Register analysis passes
            PB.registerAnalysisRegistrationCallback(
                [](FunctionAnalysisManager &FAM) {
                    registerAnalyses(FAM);
                });
            
            // Register transformation passes for -passes option
            PB.registerPipelineParsingCallback(registerPipelineParsingCallback);
            
            // Optionally register passes to run at specific extension points
            // For example, to run at the end of the optimization pipeline:
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level) {
                    if (Level != OptimizationLevel::O0) {
                        // Add our passes at the end of optimization
                        FunctionPassManager FPM;
                        FPM.addPass(ConstantFoldingPass());
                        FPM.addPass(RedundancyEliminationPass());
                        // Don't auto-add loop unrolling - it's expensive
                        MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
                    }
                });
            
            // Print registration success
            errs() << "LLVMOptPasses plugin loaded successfully\n";
            errs() << "Available passes:\n";
            errs() << "  custom-constant-fold    - Constant folding optimization\n";
            errs() << "  custom-loop-unroll      - Loop unrolling optimization\n";
            errs() << "  custom-redundancy-elim  - GVN-based redundancy elimination\n";
            errs() << "  print<custom-redundancy> - Print redundancy analysis\n";
            errs() << "  custom-optimize         - Combined optimization pipeline\n";
        }
    };
}

//===----------------------------------------------------------------------===//
// Static Registration for Built-in Use
//
// If you want to build the passes into LLVM instead of as a plugin,
// use these registration functions.
//===----------------------------------------------------------------------===//

namespace llvm {

void initializeConstantFoldingPass(PassRegistry &PR) {
    // For legacy PM compatibility (if needed)
}

void initializeLoopUnrollingPass(PassRegistry &PR) {
    // For legacy PM compatibility (if needed)
}

void initializeRedundancyAnalysisPass(PassRegistry &PR) {
    // For legacy PM compatibility (if needed)
}

} // namespace llvm

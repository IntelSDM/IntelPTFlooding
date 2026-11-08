#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"

struct PTFloodPass : llvm::PassInfoMixin<PTFloodPass>
{
    llvm::PreservedAnalyses run(llvm::Module& m, llvm::ModuleAnalysisManager&);
};

bool RunPTFlood(llvm::Module& m);

extern llvm::cl::opt<bool> PTFloodEnable;
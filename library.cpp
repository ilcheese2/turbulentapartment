#include <iostream>

#include "gradient/gradient.h"
#include "gradient/utils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"

using namespace llvm;

namespace {
struct DifferentiationPass : PassInfoMixin<DifferentiationPass> {
    static PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        SmallVector<CallInst*> Calls;
        for (Function& F : M.functions()) {
            if (F.getName().contains("__enzyme_autodiff")) {
                for (auto *U : F.users()) {
                    if (auto *Call = dyn_cast<CallInst>(U)) {
                        if (Call->getCalledFunction() == &F) {
                            Calls.push_back(Call);
                        }
                    }
                }
            }
        }


        for (CallInst* Call : Calls) {
            DifferentiationRequest R = parseRequest(Call);
            GradientSignature S = buildSignature(R.Target, R.Activities); // cant cache bc metadata
            replaceMarkerCall(Call, createGradient(R.Target, S), R, S);
        }

        return PreservedAnalyses::none();
    }

    static bool isRequired() { return true; }
};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        .APIVersion = 2, .PluginName = "Differentiation", .PluginVersion = LLVM_VERSION_STRING, .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback([](StringRef Name, ModulePassManager &MPM,
                                                  ArrayRef<PassBuilder::PipelineElement>) {
                if (Name != "differentiation")
                    return false;

                MPM.addPass(DifferentiationPass());
                return true;
            });
        }};
}
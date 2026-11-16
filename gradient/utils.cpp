#include "utils.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

using namespace llvm;

Function* cloneFunction(Function* F, GradientSignature& S) {
    FunctionType* NewFuncTy = FunctionType::get(S.ResultTy, S.ParameterTy, false);
    Function* Grad = Function::Create(NewFuncTy, F->getLinkage(), F->getAddressSpace(), F->getName(), F->getParent());
    ValueToValueMapTy VMap;

    for (const ArgumentMapping& M : S.Arguments) {
        const Argument* Original = F->getArg(M.OriginalIndex);
        Argument* Generated = Grad->getArg(M.GeneratedIndex);

        Generated->setName(Original->getName());
        VMap[Original] = Generated;

        if (M.GeneratedShadowIndex) {
            Grad->getArg(*M.GeneratedShadowIndex)->setName(Original->getName() + ".shadow");
        }
    }

    SmallVector<ReturnInst *> Returns;
    CloneFunctionInto(Grad, F, VMap, CloneFunctionChangeType::LocalChangesOnly, Returns);

    return Grad;
}

GradientSignature buildSignature(Function* F, const ArrayRef<Activity> Activities) {
    GradientSignature S;
    SmallVector<Type*> ResultTypes;

    for (Argument& Arg : F->args()) {
        ArgumentMapping AM;

        AM.OriginalIndex = Arg.getArgNo();
        AM.Ty = Arg.getType();
        AM.Kind = Activities[AM.OriginalIndex];
        AM.GeneratedIndex = S.ParameterTy.size();

        S.ParameterTy.push_back(AM.Ty);

        if (AM.Kind == Activity::Duplicated) {
            AM.GeneratedShadowIndex = S.ParameterTy.size();
            S.ParameterTy.push_back(AM.Ty);
        }

        if (AM.Kind == Activity::Active) {
            AM.GradientResultIndex = ResultTypes.size();
            ResultTypes.push_back(AM.Ty);
        }

        S.Arguments.push_back(AM);
    }

    S.ResultTy = StructType::get(F->getContext(), ResultTypes);
    return S;
}

DifferentiationRequest parseRequest(const CallInst* Call) {
    DifferentiationRequest R;
    R.Target = dyn_cast<Function>(Call->getArgOperand(0)->stripPointerCasts());

    unsigned i = 1;

    for (Argument& Arg : R.Target->args()) {
        Activity Kind;

        Value* V = Call->getArgOperand(i);
        StringRef Name;

        if (const auto* M = dyn_cast<MetadataAsValue>(V)) {
            Name = M->getName();
            i++;
        } else if (auto* L = dyn_cast<LoadInst>(V)) { // idk how this helps frontends
            if (const auto* G = dyn_cast<GlobalVariable>(L->getPointerOperand()->stripPointerCasts())) {
                Name = G->getName();
                i++;
            }
        }

        if (Name == "enzyme_const") {
            Kind = Activity::Constant;
        } else if (Name == "enzyme_out") {
            Kind = Activity::Active;
        } else if (Name == "enzyme_dup") {
            Kind = Activity::Duplicated;
        } else {
            Type* Ty = Arg.getType();

            if (Ty->isIntegerTy()) {
                Kind = Activity::Constant;
            } else if (gradientSupported(Ty)) {
                Kind = Activity::Active;
            } else if (Ty->isPointerTy()) {
                Kind = Activity::Duplicated;
            }
        }
        R.Activities.push_back(Kind); // TODO: make it crash
        R.GeneratedArguments.push_back(Call->getArgOperand(i++));

        if (Kind == Activity::Duplicated) {
            R.GeneratedArguments.push_back(Call->getArgOperand(i++));
        }
    }
    return R;
}

void replaceMarkerCall(CallInst* Marker, Function* Gradient, const DifferentiationRequest& R, const GradientSignature& S) {
    IRBuilder Builder(Marker);
    CallInst* Generated = Builder.CreateCall(Gradient, R.GeneratedArguments);
    Generated->setCallingConv(Gradient->getCallingConv());

    if (!Marker->getType()->isVoidTy()) {
        Value* Replacement = Generated;

        if (!Marker->use_empty() && Marker->getType() != S.ResultTy) { // idk if this is right
            Replacement = Builder.CreateExtractValue(Generated, {0});
        }

        Marker->replaceAllUsesWith(Replacement);
    }

    Marker->eraseFromParent();
}

bool gradientSupported(const Type* Ty) {
    return Ty->isFloatTy() || Ty->isDoubleTy();
}

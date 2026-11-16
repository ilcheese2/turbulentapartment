#include "gradientbuilder.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"

using namespace llvm;


void GradientBuilder::prepareFrames() {
    for (BasicBlock* BB : ForwardBlocks) {
        auto& Info = Blocks[BB];
        Info.Reverse = BasicBlock::Create(Grad->getContext(), BB->getName() + ".reverse", Grad);
        for (Instruction* I : Info.Instructions) {
            if (I->isTerminator() || isa<PHINode>(I) || !gradientSupported(I->getType())) continue;
            for (Value* Operand : I->operands()) {
                if (isa<Instruction>(Operand) && (gradientSupported(Operand->getType()) || Operand->getType()->isIntegerTy(1)) && !is_contained(Info.SavedValues, Operand)) {
                    Info.SavedValues.push_back(Operand);
                }
            }
        }

        for (Instruction* I : Info.Instructions) {
            Type* Ty;
            Value* Pointer;

            if (auto* L = dyn_cast<LoadInst>(I)) {
                Ty = L->getType();
                Pointer = L->getPointerOperand();
            } else if (auto* S = dyn_cast<StoreInst>(I)) {
                Ty = S->getValueOperand()->getType();
                Pointer = S->getPointerOperand();
            } else {
                continue;
            }

            if (!gradientSupported(Ty)) continue;

            if (Value* Shadow = getOrCreateForwardShadow(Pointer); !is_contained(Info.SavedValues, Shadow)) {
                Info.SavedValues.push_back(Shadow);
            }
        }

        SmallVector<Type*> Fields{PtrTy, TagTy}; // prev frame, predecessor
        for (const Value* V : Info.SavedValues) {
            Fields.push_back(V->getType());
        }
        Info.FrameType = StructType::get(Grad->getContext(), Fields);
    }
}

void GradientBuilder::recordForwardExecution() {
    const DataLayout& DL = Grad->getParent()->getDataLayout();
    for (BasicBlock* BB : ForwardBlocks) {
        auto& Info = Blocks[BB];
        Value* Path = ConstantInt::get(TagTy, 0);
        if (Info.Predecessors.size() > 1) {
            auto* Tag = PHINode::Create(TagTy, pred_size(BB), "path", BB->begin());
            for (BasicBlock* Pred : predecessors(BB)) {
                Tag->addIncoming(ConstantInt::get(TagTy, Blocks[Pred].ID), Pred);
            }
            Path = Tag;
        }
        IRBuilder Save(BB->getTerminator());
        Value* Bytes = ConstantInt::get(SizeTy, DL.getTypeAllocSize(Info.FrameType).getFixedValue());
        Value* Frame = Save.CreateCall(Malloc, {Bytes});
        Value* Previous = Save.CreateLoad(PtrTy, TapeHead);
        Save.CreateStore(Previous, Save.CreateStructGEP(Info.FrameType, Frame, 0));
        Save.CreateStore(Path, Save.CreateStructGEP(Info.FrameType, Frame, 1));
        for (unsigned Index = 0; Index < Info.SavedValues.size(); ++Index) {
            Save.CreateStore(Info.SavedValues[Index], Save.CreateStructGEP(Info.FrameType, Frame, Index + 2));
        }
        Save.CreateStore(Frame, TapeHead);

        rewriteReturn(BB, Save);
    }
}

Value* GradientBuilder::restoreFrame(IRBuilder<>& Builder, BlockInfo& Info,
                                     DenseMap<Value*, Value*>& Saved) const {
    Value* Frame = Builder.CreateLoad(PtrTy, TapeHead);
    Value* Previous = Builder.CreateLoad(PtrTy, Builder.CreateStructGEP(Info.FrameType, Frame, 0));
    Value* Path = Builder.CreateLoad(TagTy, Builder.CreateStructGEP(Info.FrameType, Frame, 1));
    for (unsigned Index = 0; Index < Info.SavedValues.size(); ++Index) {
        Value* V = Info.SavedValues[Index];
        Saved[V] = Builder.CreateLoad(V->getType(), Builder.CreateStructGEP(Info.FrameType, Frame, Index + 2));
    }
    Builder.CreateStore(Previous, TapeHead);
    Builder.CreateCall(Free, {Frame});
    return Path;
}

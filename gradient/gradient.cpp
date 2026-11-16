#include "gradient.h"
#include "gradientbuilder.h"
#include "utils.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

GradientBuilder::GradientBuilder(Function* F, GradientSignature& S) : Signature(S) {
    Grad = cloneFunction(F, Signature);
    Grad->setName(F->getName() + ".grad");
    Grad->setAttributes(AttributeList()); // TODO: figure this out later
    Grad->setCallingConv(F->getCallingConv());
}

Function* createGradient(Function* F, GradientSignature& S) { return GradientBuilder(F, S).build(); }

Function* GradientBuilder::build() {
    copyForwardBlocks();
    initializeState();
    prepareFrames();
    recordForwardExecution();

    for (BasicBlock* BB : ForwardBlocks) {
        BlockInfo& Info = Blocks[BB];

        IRBuilder Builder(Info.Reverse);

        DenseMap<Value*, Value*> Saved;
        Value* Path = restoreFrame(Builder, Info, Saved);
        for (Instruction* I : reverse(Info.Instructions))
            emitReverseInstruction(Builder, I, Saved);
        emitReverseControlFlow(Builder, BB, Path);
    }
    return Grad;
}

void GradientBuilder::emitReverseInstruction(IRBuilder<>& Builder, Instruction* I, const DenseMap<Value*, Value*>& Saved) const {
    if (I->isTerminator() || isa<PHINode>(I) || I->getType()->isIntegerTy() || isa<GetElementPtrInst>(I)) return;

    if (auto* L = dyn_cast<LoadInst>(I)) {
        emitReverseLoad(Builder, L, Saved);
        return;
    }
    if (auto* S = dyn_cast<StoreInst>(I)) {
        emitReverseStore(Builder, S, Saved);
        return;
    }

    emitArithmetic(Builder, I, Saved);
}

void GradientBuilder::emitReverseLoad(IRBuilder<>& Builder, LoadInst* L, const DenseMap<Value*, Value*>& Saved) const {
    Type* Ty = L->getType();

    const Value* ForwardShadow = ShadowPointers.lookup(L->getPointerOperand());
    Value* ShadowAddress = Saved.lookup(ForwardShadow);
    Value* DV = getAdjoint(Builder, L);
    Value* Old = Builder.CreateAlignedLoad(Ty, ShadowAddress, Align(1)); // TODO: align
    Value* Updated = Builder.CreateFAdd(Old, DV);
    Builder.CreateAlignedStore(Updated, ShadowAddress, Align(1));
    Builder.CreateStore(ConstantFP::get(Ty, 0.0), AdjointSlots.lookup(L));
}

void GradientBuilder::emitReverseStore(IRBuilder<>& Builder, StoreInst* S, const DenseMap<Value*, Value*>& Saved) const {
    Type* Ty = S->getValueOperand()->getType();

    const Value* ForwardShadow = ShadowPointers.lookup(S->getPointerOperand());
    Value* ShadowAddress = Saved.lookup(ForwardShadow);
    Value* Contribution = Builder.CreateAlignedLoad(Ty, ShadowAddress, Align(1));
    Builder.CreateAlignedStore(ConstantFP::get(Ty, 0.0), ShadowAddress, Align(1));
    accumulate(Builder,S->getValueOperand(), Contribution);
}

void GradientBuilder::copyForwardBlocks() {
    for (BasicBlock& BB : *Grad) {
        auto& Info = Blocks[&BB];
        Info.ID = ForwardBlocks.size();
        ForwardBlocks.push_back(&BB);
        for (Instruction& I : BB) {
            if (!I.isTerminator()) { // handled later
                Info.Instructions.push_back(&I);
            }
        }
        for (BasicBlock* Pred : predecessors(&BB)) {
            if (!is_contained(Info.Predecessors, Pred)) {
                Info.Predecessors.push_back(Pred);
            }
        }
    }
}

void GradientBuilder::initializeState() {
    LLVMContext& Context = Grad->getContext();
    Module& M = *Grad->getParent();
    const DataLayout& DL = M.getDataLayout();
    PtrTy = PointerType::getUnqual(Context);
    TagTy = Type::getInt32Ty(Context);
    SizeTy = DL.getIntPtrType(Context);
    Malloc = M.getOrInsertFunction("malloc", PtrTy, SizeTy); // over stack for frame
    Free = M.getOrInsertFunction("free", Type::getVoidTy(Context), PtrTy);
    IRBuilder EntryBuilder(&*Grad->getEntryBlock().getFirstInsertionPt());
    TapeHead = EntryBuilder.CreateAlloca(PtrTy, nullptr);
    EntryBuilder.CreateStore(ConstantPointerNull::get(PtrTy), TapeHead);
    auto addAdjointSlot = [&](Value* V) {
        auto* Slot = EntryBuilder.CreateAlloca(V->getType(), nullptr);
        EntryBuilder.CreateStore(ConstantFP::get(V->getType(), 0.0), Slot);
        AdjointSlots[V] = Slot;
    };

    for (ArgumentMapping& AM: Signature.Arguments) {
        Argument* Generated = Grad->getArg(AM.GeneratedIndex);

        if (gradientSupported(AM.Ty)) {
            addAdjointSlot(Generated);
        }

        if (AM.GeneratedShadowIndex) {
            ShadowPointers[Generated] = Grad->getArg(*AM.GeneratedShadowIndex);
        }
    }

    for (BasicBlock* BB : ForwardBlocks) {
        for (Instruction* I : Blocks[BB].Instructions) {
            if (gradientSupported(I->getType())) {
                addAdjointSlot(I);
            }
        }
    }
}
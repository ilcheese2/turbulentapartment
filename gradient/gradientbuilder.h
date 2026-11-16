#pragma once

#include "utils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"


class GradientBuilder {
  public:
    explicit GradientBuilder(llvm::Function* F, GradientSignature& S);
    llvm::Function *build();

    [[nodiscard]] const GradientSignature& getSignature() const { return Signature; }

  private:
    struct BlockInfo {
        llvm::SmallVector<llvm::Instruction *> Instructions;
        llvm::SmallVector<llvm::BasicBlock *> Predecessors;
        llvm::SmallVector<llvm::Value *> SavedValues;
        llvm::StructType *FrameType = nullptr;
        llvm::BasicBlock *Reverse = nullptr;
        unsigned ID = 0;
    };

    void copyForwardBlocks();
    void initializeState();
    void prepareFrames();
    void recordForwardExecution();
    llvm::Value *restoreFrame(llvm::IRBuilder<> &Builder, BlockInfo &Info,
                              llvm::DenseMap<llvm::Value *, llvm::Value *> &Saved) const;

    llvm::Value* getAdjoint(llvm::IRBuilder<> &Builder, llvm::Value* V) const;
    llvm::Value* getOrCreateForwardShadow(llvm::Value* V);
    void accumulate(llvm::IRBuilder<> &Builder, llvm::Value *V, llvm::Value *Contribution) const;
    void emitReverseInstruction(llvm::IRBuilder<>& Builder, llvm::Instruction* I, const llvm::DenseMap<llvm::Value*, llvm::Value*>& Saved) const;
    void emitReverseLoad(llvm::IRBuilder<>& Builder, llvm::LoadInst* L, const llvm::DenseMap<llvm::Value*, llvm::Value*>& Saved) const;
    void emitReverseStore(llvm::IRBuilder<>& Builder, llvm::StoreInst* S, const llvm::DenseMap<llvm::Value*, llvm::Value*>& Saved) const;
    void emitArithmetic(llvm::IRBuilder<>& Builder, llvm::Instruction* I, const llvm::DenseMap<llvm::Value*, llvm::Value*>& Saved) const;

    void rewriteReturn(llvm::BasicBlock *BB, llvm::IRBuilder<> &Builder);
    void emitReverseControlFlow(llvm::IRBuilder<> &Builder, llvm::BasicBlock *BB,
                                llvm::Value *Path);
    llvm::BasicBlock *createReverseEdge(BlockInfo &Info, llvm::BasicBlock *Pred);

    GradientSignature& Signature; // #imisslifetimes
    llvm::Function* Grad;
    llvm::PointerType* PtrTy = nullptr;
    llvm::IntegerType* TagTy = nullptr;
    llvm::IntegerType* SizeTy = nullptr;
    llvm::FunctionCallee Malloc;
    llvm::FunctionCallee Free;
    llvm::AllocaInst* TapeHead = nullptr;
    llvm::DenseMap<llvm::Value*, llvm::AllocaInst*> AdjointSlots;
    llvm::DenseMap<llvm::Value*, llvm::Value*> ShadowPointers;
    llvm::SmallVector<llvm::BasicBlock*> ForwardBlocks;
    llvm::DenseMap<llvm::BasicBlock*, BlockInfo> Blocks;
};

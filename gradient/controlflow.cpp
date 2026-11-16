#include <iostream>

#include "gradientbuilder.h"

#include "llvm/IR/Constants.h"

using namespace llvm;


void GradientBuilder::rewriteReturn(BasicBlock* BB, IRBuilder<>& Builder) {
    if (auto* Ret = dyn_cast<ReturnInst>(BB->getTerminator())) {
        if (Ret->getReturnValue() && !isa<Constant>(Ret->getReturnValue())) {
            Builder.CreateStore(ConstantFP::get(Ret->getReturnValue()->getType(), 1.0), AdjointSlots.lookup(Ret->getReturnValue()));
        }
        Builder.CreateBr(Blocks[BB].Reverse);
        Ret->eraseFromParent();
    }
}

void GradientBuilder::emitReverseControlFlow(IRBuilder<>& Builder, BasicBlock* BB, Value* Path) {
    auto& Info = Blocks[BB];
    if (BB == &Grad->getEntryBlock()) {
        Value* Result = UndefValue::get(Signature.ResultTy);
        for (ArgumentMapping& AM: Signature.Arguments) {
            if (!AM.GradientResultIndex) continue;

            Argument* Generated = Grad->getArg(AM.GeneratedIndex);
            Result = Builder.CreateInsertValue(Result, getAdjoint(Builder, Generated), *AM.GradientResultIndex);
        }
        Builder.CreateRet(Result);
        return;
    }

    SmallVector<BasicBlock*> Edges;
    for (BasicBlock* Pred : Info.Predecessors) {
        Edges.push_back(createReverseEdge(Info, Pred));
    }

    if (Edges.size() == 1) {
        Builder.CreateBr(Edges.front());
    } else if (!Edges.empty()) {
        auto* Switch = Builder.CreateSwitch(Path, Edges.front(), Edges.size() - 1); // easier than reconstructing
        for (unsigned Index = 1; Index < Edges.size(); Index++) {
            Switch->addCase(Builder.getInt32(Blocks[Info.Predecessors[Index]].ID), Edges[Index]);
        }
    }
}

BasicBlock* GradientBuilder::createReverseEdge(BlockInfo& Info, BasicBlock* Pred) {
    auto* Edge = BasicBlock::Create(Grad->getContext(), "reverse.edge", Grad);
    IRBuilder EdgeBuilder(Edge);
    SmallVector<std::pair<Value*, Value*>> Contributions;

    for (Instruction* I : Info.Instructions) {
        const auto* Phi = dyn_cast<PHINode>(I);
        if (!Phi || !gradientSupported(Phi->getType())) continue;

        Value* DZ = EdgeBuilder.CreateLoad(Phi->getType(), AdjointSlots.lookup(Phi));
        Contributions.emplace_back(Phi->getIncomingValueForBlock(Pred), DZ);
    }

    for (Instruction* I : Info.Instructions) {
        if (isa<PHINode>(I) && gradientSupported(I->getType())) {
            EdgeBuilder.CreateStore(ConstantFP::get(I->getType(), 0.0), AdjointSlots.lookup(I));
        }
    }

    for (auto [Incoming, DZ] : Contributions) {
        if (isa<Constant>(Incoming)) continue;
        auto* Slot = AdjointSlots.lookup(Incoming);
        Value* Old = EdgeBuilder.CreateLoad(Incoming->getType(), Slot);
        EdgeBuilder.CreateStore(EdgeBuilder.CreateFAdd(Old, DZ), Slot);
    }
    EdgeBuilder.CreateBr(Blocks[Pred].Reverse);
    return Edge;
}

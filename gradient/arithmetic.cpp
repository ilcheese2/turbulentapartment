#include "gradientbuilder.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

Value* GradientBuilder::getAdjoint(IRBuilder<>& Builder, Value* V) const {
    if (isa<Constant>(V)) return ConstantFP::get(V->getType(), 0.0);
    return Builder.CreateLoad(V->getType(), AdjointSlots.lookup(V));
}

Value* GradientBuilder::getOrCreateForwardShadow(Value* V) {
    if (Value* Shadow = ShadowPointers.lookup(V)) return Shadow;

    auto* GEP = dyn_cast<GetElementPtrInst>(V);
    Value* ShadowBase = getOrCreateForwardShadow(GEP->getPointerOperand()); // base

    SmallVector<Value*> Indices;
    for (Value* Index : GEP->indices()) {
        Indices.push_back(Index);
    }

    IRBuilder Builder(GEP->getNextNode());

    Value* Shadow = Builder.CreateGEP(GEP->getSourceElementType(), ShadowBase, Indices);
    ShadowPointers[GEP] = Shadow;
    return Shadow;

}

void GradientBuilder::accumulate(IRBuilder<>& Builder, Value* V, Value* Contribution) const {
    if (!isa<Constant>(V)) {
        Builder.CreateStore(Builder.CreateFAdd(getAdjoint(Builder, V), Contribution), AdjointSlots.lookup(V));
    }
}

void GradientBuilder::emitArithmetic(IRBuilder<>& Builder, Instruction* I, const DenseMap<Value*, Value*>& Saved) const {
    auto primal = [&](Value* V) -> Value*  {
        if (Value* Recorded = Saved.lookup(V))
            return Recorded;
        return V;
    };

    Value* DZ = getAdjoint(Builder, I);
    Builder.CreateStore(ConstantFP::get(I->getType(), 0.0), AdjointSlots.lookup(I));
    if (const auto* Binary = dyn_cast<BinaryOperator>(I)) {
        Value* A = Binary->getOperand(0);
        Value* B = Binary->getOperand(1);
        switch (Binary->getOpcode()) {
            case Instruction::FAdd:
                accumulate(Builder, A, DZ);
                accumulate(Builder, B, DZ);
                break;
            case Instruction::FSub:
                accumulate(Builder, A, DZ);
                accumulate(Builder, B, Builder.CreateFNeg(DZ));
                break;
            case Instruction::FMul:
                accumulate(Builder, A, Builder.CreateFMul(primal(B), DZ));
                accumulate(Builder, B, Builder.CreateFMul(primal(A), DZ));
                break;
            case Instruction::FDiv:
                accumulate(Builder, A, Builder.CreateFDiv(DZ, primal(B)));
                accumulate(Builder, B, Builder.CreateFNeg(Builder.CreateFDiv(Builder.CreateFMul(DZ, primal(A)), Builder.CreateFMul(primal(B), primal(B)))));
                break;
            default:
                report_fatal_error("unsupported binary operation");
        }
    } else if (auto* Select = dyn_cast<SelectInst>(I)) {
        Value* Condition = primal(Select->getCondition());
        accumulate(Builder, Select->getTrueValue(), Builder.CreateSelect(Condition, DZ, ConstantFP::get(I->getType(), 0.0)));
        accumulate(Builder, Select->getFalseValue(), Builder.CreateSelect(Condition, ConstantFP::get(I->getType(), 0.0), DZ));
    } else if (I->getOpcode() == Instruction::FNeg) {
        accumulate(Builder, I->getOperand(0), Builder.CreateFNeg(DZ));
    } else if (const auto* Ext = dyn_cast<FPExtInst>(I)) { // this is what enzyme does
        Value* Input = Ext->getOperand(0);
        Value* Contribution = Builder.CreateFPTrunc(DZ, Input->getType());
        accumulate(Builder, Input, Contribution);
    } else if (const auto* Trunc = dyn_cast<FPTruncInst>(I)) {
        Value* Input = Trunc->getOperand(0);
        Value* Contribution = Builder.CreateFPExt(DZ, Input->getType());
        accumulate(Builder, Input, Contribution);
    } else {
        errs() << *I << "\n";
        report_fatal_error("unsupported instruction");
    }
}

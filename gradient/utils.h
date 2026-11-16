#pragma once
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Type.h>

namespace llvm {
    class CallInst;
    class Value;
    class StructType;
    class ReturnInst;
    class Type;
    class Function;
}

enum class Activity {
    Constant,
    Active,
    Duplicated
};
struct ArgumentMapping {
    unsigned OriginalIndex;
    llvm::Type* Ty;
    Activity Kind;
    unsigned GeneratedIndex;
    std::optional<unsigned> GeneratedShadowIndex;
    std::optional<unsigned> GradientResultIndex;
};

struct GradientSignature {
    llvm::SmallVector<ArgumentMapping> Arguments;
    llvm::SmallVector<llvm::Type*> ParameterTy;
    llvm::StructType* ResultTy;
};

struct DifferentiationRequest {
    llvm::Function* Target;
    llvm::SmallVector<Activity> Activities;
    llvm::SmallVector<llvm::Value*> GeneratedArguments;
};

llvm::Function* cloneFunction(llvm::Function* F, GradientSignature& S);
GradientSignature buildSignature(llvm::Function* F, llvm::ArrayRef<Activity> Activities);
DifferentiationRequest parseRequest(const llvm::CallInst* Call);
void replaceMarkerCall(llvm::CallInst* Marker, llvm::Function* Gradient, const DifferentiationRequest& R, const GradientSignature& S);
bool gradientSupported(const llvm::Type* Ty);
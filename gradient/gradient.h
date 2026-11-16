#pragma once
#include "utils.h"

enum class Activity;

namespace llvm {
class Function;
}

llvm::Function* createGradient(llvm::Function* F, GradientSignature& S);

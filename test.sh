/opt/homebrew/opt/llvm/bin/clang -O2 -ffp-contract=off -fno-vectorize -fno-discard-value-names -fno-slp-vectorize -fno-unroll-loops -S -emit-llvm tests/test.c -o test.ll
/opt/homebrew/opt/llvm/bin/opt -load-pass-plugin=./cmake-build-debug/turbulentapartment.dylib \
  -passes='module(differentiation)' \
   test.ll -S -o result.ll
/opt/homebrew/opt/llvm/bin/clang result.ll -O3 -o a.out
./a.out
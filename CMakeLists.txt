cmake_minimum_required(VERSION 4.1)
project(turbulentapartment)

find_package(LLVM REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

set(CMAKE_CXX_STANDARD 20)


add_llvm_pass_plugin(turbulentapartment PARTIAL_SOURCES_INTENDED
    library.cpp
    gradient/utils.cpp
    gradient/gradient.cpp
    gradient/tape.cpp
    gradient/arithmetic.cpp
    gradient/controlflow.cpp
)

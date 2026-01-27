cmake_minimum_required(VERSION 3.15)
project(GuardedGraphPass LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_library(GuardedGraphPass SHARED GuardedGraphPass.cpp)
llvm_update_compile_flags(GuardedGraphPass)

add_library(CollapseDerefsPass SHARED CollapseDerefsPass.cpp)
llvm_update_compile_flags(CollapseDerefsPass)

cmake_minimum_required(VERSION 3.15)
project(GuardedHeapGraphDemo C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(runtime
    runtime/checked_ptr.c
    runtime/heap_gen.c
)

target_include_directories(runtime PUBLIC runtime)

add_library(kernels programs/kernels.c)
target_include_directories(kernels PUBLIC runtime programs)
target_link_libraries(kernels runtime)

add_library(checker checker/graph_eval.c)
target_include_directories(checker PUBLIC runtime checker)
target_link_libraries(checker runtime)

add_executable(driver driver/main.c)
target_link_libraries(driver runtime kernels checker)

add_executable(bench_triple_deref driver/bench_triple_deref.c)
target_link_libraries(bench_triple_deref runtime kernels)

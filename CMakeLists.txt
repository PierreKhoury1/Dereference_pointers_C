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

add_executable(bench_triple_deref_ssa driver/bench_triple_deref_ssa.c)
target_link_libraries(bench_triple_deref_ssa runtime kernels)

# Build optimized SSA benchmark via the existing collapse pass pipeline.
add_custom_target(bench_triple_deref_ssa_opt
    COMMAND ${CMAKE_COMMAND} -E env RUNS=0 WARMUP=0 RUN_SSA=1 ${CMAKE_SOURCE_DIR}/run_bench.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    BYPRODUCTS ${CMAKE_BINARY_DIR}/bench_triple_deref_ssa_opt
    USES_TERMINAL
)

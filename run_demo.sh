#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT/out}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_LLVM_DIR="${BUILD_LLVM_DIR:-$ROOT/build_llvm}"
CLANG_BIN="${CLANG:-clang}"
OPT_BIN="${OPT:-opt}"

mkdir -p "$OUT_DIR"

cmake -S "$ROOT/llvm_pass" -B "$BUILD_LLVM_DIR"
cmake --build "$BUILD_LLVM_DIR" --config Release

cmake -S "$ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --config Release

"$CLANG_BIN" -S -emit-llvm -O0 -g -fno-discard-value-names -fno-inline -Xclang -disable-O0-optnone \
  -I "$ROOT/runtime" -I "$ROOT/programs" \
  "$ROOT/programs/kernels.c" -o "$BUILD_DIR/kernels.ll"

PASS_LIB="$BUILD_LLVM_DIR/libGuardedGraphPass.so"
if [ ! -f "$PASS_LIB" ]; then
  PASS_LIB="$BUILD_LLVM_DIR/libGuardedGraphPass.dylib"
fi

GRAPH_OUT_DIR="$OUT_DIR" "$OPT_BIN" -load-pass-plugin "$PASS_LIB" \
  -passes="guarded-graph" -disable-output "$BUILD_DIR/kernels.ll"

"$BUILD_DIR/driver" --trials 200 --seed 1234 --graph_dir "$OUT_DIR" --out_dir "$OUT_DIR"

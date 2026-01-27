#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT/out}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_LLVM_DIR="${BUILD_LLVM_DIR:-$ROOT/build_llvm}"
CLANG_BIN="${CLANG:-clang}"
OPT_BIN="${OPT:-opt}"
ITERS="${ITERS:-10000000}"

mkdir -p "$OUT_DIR"

cmake -S "$ROOT/llvm_pass" -B "$BUILD_LLVM_DIR"
cmake --build "$BUILD_LLVM_DIR" --config Release

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --config Release

"$CLANG_BIN" -S -emit-llvm -O0 -g -fno-discard-value-names -fno-inline -Xclang -disable-O0-optnone \
  -I "$ROOT/runtime" -I "$ROOT/programs" \
  "$ROOT/programs/kernels.c" -o "$BUILD_DIR/kernels.ll"

GRAPH_PASS="$BUILD_LLVM_DIR/libGuardedGraphPass.so"
if [ ! -f "$GRAPH_PASS" ]; then
  GRAPH_PASS="$BUILD_LLVM_DIR/libGuardedGraphPass.dylib"
fi

COLLAPSE_PASS="$BUILD_LLVM_DIR/libCollapseDerefsPass.so"
if [ ! -f "$COLLAPSE_PASS" ]; then
  COLLAPSE_PASS="$BUILD_LLVM_DIR/libCollapseDerefsPass.dylib"
fi

GRAPH_OUT_DIR="$OUT_DIR" "$OPT_BIN" -load-pass-plugin "$GRAPH_PASS" \
  -passes="guarded-graph" -disable-output "$BUILD_DIR/kernels.ll"

BASE_BIN="$BUILD_DIR/bench_triple_deref"

"$CLANG_BIN" -S -emit-llvm -O3 \
  -I "$ROOT/runtime" -I "$ROOT/programs" \
  "$ROOT/driver/bench_triple_deref.c" -o "$BUILD_DIR/bench.ll"

GRAPH_OUT_DIR="$OUT_DIR" "$OPT_BIN" -load-pass-plugin "$COLLAPSE_PASS" \
  -passes="collapse-deref" -S "$BUILD_DIR/bench.ll" -o "$BUILD_DIR/bench_opt.ll"

"$CLANG_BIN" -O3 "$BUILD_DIR/bench_opt.ll" -L "$BUILD_DIR" -lkernels -lruntime \
  -o "$BUILD_DIR/bench_triple_deref_opt"

BASE_CALLS=$(grep -c "call.*@triple_deref" "$BUILD_DIR/bench.ll" || true)
OPT_CALLS=$(grep -c "call.*@triple_deref" "$BUILD_DIR/bench_opt.ll" || true)
echo "IR call count (triple_deref): baseline=$BASE_CALLS optimized=$OPT_CALLS"

BASE_OUT=$("$BASE_BIN" --iters "$ITERS")
OPT_OUT=$("$BUILD_DIR/bench_triple_deref_opt" --iters "$ITERS")

echo "baseline: $BASE_OUT"
echo "optimized: $OPT_OUT"

BASE_TIME=$(echo "$BASE_OUT" | awk -F'time_ns=' '{print $2}' | awk '{print $1}')
OPT_TIME=$(echo "$OPT_OUT" | awk -F'time_ns=' '{print $2}' | awk '{print $1}')

if [ -n "$BASE_TIME" ] && [ -n "$OPT_TIME" ]; then
  SPEEDUP=$(awk -v b="$BASE_TIME" -v o="$OPT_TIME" 'BEGIN { if (o>0) printf "%.3f", b/o; }')
  echo "speedup: ${SPEEDUP}x"
else
  echo "speedup: unable to parse time_ns"
fi

BASE_LINE=$(grep -n "call .*@triple_deref" "$BUILD_DIR/bench.ll" | head -n 1 | cut -d: -f1 || true)
OPT_LINE=$(grep -n "triple_deref_cached" "$BUILD_DIR/bench_opt.ll" | head -n 1 | cut -d: -f1 || true)
if [ -n "$BASE_LINE" ]; then
  echo "IR baseline snippet:"
  sed -n "$((BASE_LINE-3)),$((BASE_LINE+5))p" "$BUILD_DIR/bench.ll"
fi
if [ -n "$OPT_LINE" ]; then
  echo "IR optimized snippet:"
  sed -n "$((OPT_LINE-3)),$((OPT_LINE+5))p" "$BUILD_DIR/bench_opt.ll"
fi

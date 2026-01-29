#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT/out}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_LLVM_DIR="${BUILD_LLVM_DIR:-$ROOT/build_llvm}"
CLANG_BIN="${CLANG:-clang}"
OPT_BIN="${OPT:-opt}"
ITERS="${ITERS:-10000000}"
RUNS="${RUNS:-5}"
WARMUP="${WARMUP:-1}"
DATA_PATH="${DATA_PATH:-$ROOT/viz/bench_data.js}"
RUN_SSA="${RUN_SSA:-1}"
SSA_DATA_PATH="${SSA_DATA_PATH:-$ROOT/viz/bench_data_ssa.js}"

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

SSA_BIN="$BUILD_DIR/bench_triple_deref_ssa"
SSA_OPT_BIN="$BUILD_DIR/bench_triple_deref_ssa_opt"
if [ "$RUN_SSA" -ne 0 ]; then
  "$CLANG_BIN" -S -emit-llvm -O3 \
    -I "$ROOT/runtime" -I "$ROOT/programs" \
    "$ROOT/driver/bench_triple_deref_ssa.c" -o "$BUILD_DIR/bench_ssa.ll"

  GRAPH_OUT_DIR="$OUT_DIR" "$OPT_BIN" -load-pass-plugin "$COLLAPSE_PASS" \
    -passes="collapse-deref" -S "$BUILD_DIR/bench_ssa.ll" -o "$BUILD_DIR/bench_ssa_opt.ll"

  "$CLANG_BIN" -O3 "$BUILD_DIR/bench_ssa_opt.ll" -L "$BUILD_DIR" -lkernels -lruntime \
    -o "$SSA_OPT_BIN"
fi

BASE_CALLS=$(grep -c "call.*@triple_deref" "$BUILD_DIR/bench.ll" || true)
OPT_CALLS=$(grep -c "call.*@triple_deref" "$BUILD_DIR/bench_opt.ll" || true)
echo "IR call count (triple_deref): baseline=$BASE_CALLS optimized=$OPT_CALLS"
if [ "$RUN_SSA" -ne 0 ]; then
  SSA_BASE_CALLS=$(grep -c "call.*@triple_deref" "$BUILD_DIR/bench_ssa.ll" || true)
  SSA_OPT_CALLS=$(grep -c "call.*@triple_deref" "$BUILD_DIR/bench_ssa_opt.ll" || true)
  echo "IR call count (triple_deref, SSA): baseline=$SSA_BASE_CALLS optimized=$SSA_OPT_CALLS"
fi

stats() {
  local prec="${1:-0}"
  sort -n | awk -v prec="$prec" '{
      a[NR]=$1; sum+=$1;
    }
    END{
      if (NR==0) { print "n/a"; exit 0 }
      min=a[1]; max=a[NR];
      if (NR % 2) {
        med=a[(NR+1)/2];
      } else {
        med=(a[NR/2] + a[NR/2+1]) / 2;
      }
      mean=sum/NR;
      for (i=1;i<=NR;i++) { d=a[i]-mean; ss+=d*d; }
      stdev=sqrt(ss/NR);
      printf "mean=%.*f median=%.*f min=%.*f max=%.*f stdev=%.*f", prec, mean, prec, med, prec, min, prec, max, prec, stdev;
    }'
}

ns_per_iter() {
  awk -v t="$1" -v it="$2" 'BEGIN { if (it>0) printf "%.3f", t/it; else print "n/a"; }'
}

extract_time() {
  echo "$1" | awk -F'time_ns=' '{print $2}' | awk '{print $1}'
}

echo "bench config: iters=$ITERS runs=$RUNS warmup=$WARMUP"

run_variant() {
  local label="$1"
  local base_bin="$2"
  local opt_bin="$3"
  local data_path="$4"
  local data_var="$5"
  local base_calls="$6"
  local opt_calls="$7"

  local -a base_times=()
  local -a opt_times=()
  local -a speedups=()
  local -a base_nspi_list=()
  local -a opt_nspi_list=()

  # Warmup to stabilize caches/CPU frequency
  for ((i=0; i<WARMUP; i++)); do
    "$base_bin" --iters "$ITERS" >/dev/null
    "$opt_bin" --iters "$ITERS" >/dev/null
  done

  for ((i=1; i<=RUNS; i++)); do
    local base_out
    local opt_out
    local base_time
    local opt_time
    local speedup
    local base_nspi
    local opt_nspi

    base_out=$("$base_bin" --iters "$ITERS")
    opt_out=$("$opt_bin" --iters "$ITERS")

    base_time=$(extract_time "$base_out")
    opt_time=$(extract_time "$opt_out")
    speedup=$(awk -v b="$base_time" -v o="$opt_time" 'BEGIN { if (o>0) printf "%.3f", b/o; else print "n/a"; }')

    base_nspi=$(ns_per_iter "$base_time" "$ITERS")
    opt_nspi=$(ns_per_iter "$opt_time" "$ITERS")

    base_times+=("$base_time")
    opt_times+=("$opt_time")
    speedups+=("$speedup")
    base_nspi_list+=("$base_nspi")
    opt_nspi_list+=("$opt_nspi")

    echo "[$label] run $i: baseline time_ns=$base_time ns/iter=$base_nspi | optimized time_ns=$opt_time ns/iter=$opt_nspi | speedup=${speedup}x"
  done

  local base_stat
  local opt_stat
  local spd_stat

  base_stat=$(printf "%s\n" "${base_times[@]}" | stats 0)
  opt_stat=$(printf "%s\n" "${opt_times[@]}" | stats 0)
  spd_stat=$(printf "%s\n" "${speedups[@]}" | stats 3)
  echo "[$label] baseline stats (time_ns): $base_stat"
  echo "[$label] optimized stats (time_ns): $opt_stat"
  echo "[$label] speedup stats (x): $spd_stat"

  local base_times_csv
  local opt_times_csv
  local base_nspi_csv
  local opt_nspi_csv
  local speedups_csv

  base_times_csv=$(IFS=,; echo "${base_times[*]}")
  opt_times_csv=$(IFS=,; echo "${opt_times[*]}")
  base_nspi_csv=$(IFS=,; echo "${base_nspi_list[*]}")
  opt_nspi_csv=$(IFS=,; echo "${opt_nspi_list[*]}")
  speedups_csv=$(IFS=,; echo "${speedups[*]}")

  BASE_TIMES_CSV="$base_times_csv" OPT_TIMES_CSV="$opt_times_csv" \
  BASE_NSPI_CSV="$base_nspi_csv" OPT_NSPI_CSV="$opt_nspi_csv" \
  SPEEDUPS_CSV="$speedups_csv" \
  BASE_CALLS="$base_calls" OPT_CALLS="$opt_calls" \
  ITERS="$ITERS" RUNS="$RUNS" WARMUP="$WARMUP" DATA_PATH="$data_path" DATA_VAR="$data_var" \
  python3 - <<'PY'
import json, math, os, time

def parse_csv(s):
    if not s:
        return []
    return [float(x) for x in s.split(",") if x]

def stats(arr):
    if not arr:
        return {}
    arr_sorted = sorted(arr)
    n = len(arr_sorted)
    mean = sum(arr_sorted) / n
    median = arr_sorted[n // 2] if n % 2 else (arr_sorted[n//2 - 1] + arr_sorted[n//2]) / 2
    var = sum((x - mean) ** 2 for x in arr_sorted) / n
    return {
        "mean": mean,
        "median": median,
        "min": arr_sorted[0],
        "max": arr_sorted[-1],
        "stdev": math.sqrt(var),
    }

data = {
    "timestamp": time.strftime("%Y-%m-%d %H:%M:%S %Z"),
    "config": {
        "iters": int(os.environ.get("ITERS", "0")),
        "runs": int(os.environ.get("RUNS", "0")),
        "warmup": int(os.environ.get("WARMUP", "0")),
    },
    "ir_call_count": {
        "baseline": int(os.environ.get("BASE_CALLS", "0") or 0),
        "optimized": int(os.environ.get("OPT_CALLS", "0") or 0),
    },
}

base_times = parse_csv(os.environ.get("BASE_TIMES_CSV", ""))
opt_times = parse_csv(os.environ.get("OPT_TIMES_CSV", ""))
base_nspi = parse_csv(os.environ.get("BASE_NSPI_CSV", ""))
opt_nspi = parse_csv(os.environ.get("OPT_NSPI_CSV", ""))
speedups = parse_csv(os.environ.get("SPEEDUPS_CSV", ""))

data["baseline"] = {
    "times_ns": base_times,
    "ns_per_iter": base_nspi,
    "stats_time_ns": stats(base_times),
    "stats_ns_per_iter": stats(base_nspi),
}
data["optimized"] = {
    "times_ns": opt_times,
    "ns_per_iter": opt_nspi,
    "stats_time_ns": stats(opt_times),
    "stats_ns_per_iter": stats(opt_nspi),
}
data["speedup"] = {
    "values": speedups,
    "stats": stats(speedups),
}

out_path = os.environ.get("DATA_PATH")
data_var = os.environ.get("DATA_VAR", "BENCH_DATA")
if out_path:
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(f"window.{data_var} = ")
        json.dump(data, f, indent=2)
        f.write(";\n")
PY
}

run_variant "base" "$BASE_BIN" "$BUILD_DIR/bench_triple_deref_opt" "$DATA_PATH" "BENCH_DATA" "$BASE_CALLS" "$OPT_CALLS"
if [ "$RUN_SSA" -ne 0 ]; then
  run_variant "ssa" "$SSA_BIN" "$SSA_OPT_BIN" "$SSA_DATA_PATH" "BENCH_DATA_SSA" "$SSA_BASE_CALLS" "$SSA_OPT_CALLS"
fi

BASE_LINE=$(grep -n "call .*@triple_deref" "$BUILD_DIR/bench.ll" | head -n 1 | cut -d: -f1 || true)
OPT_LINE=$(grep -n "call .*@triple_deref" "$BUILD_DIR/bench_opt.ll" | head -n 1 | cut -d: -f1 || true)
if [ -n "$BASE_LINE" ]; then
  echo "IR baseline snippet:"
  sed -n "$((BASE_LINE-3)),$((BASE_LINE+5))p" "$BUILD_DIR/bench.ll"
fi
if [ -n "$OPT_LINE" ]; then
  echo "IR optimized snippet (base):"
  sed -n "$((OPT_LINE-3)),$((OPT_LINE+5))p" "$BUILD_DIR/bench_opt.ll"
fi

if [ "$RUN_SSA" -ne 0 ]; then
  SSA_BASE_LINE=$(grep -n "call .*@triple_deref" "$BUILD_DIR/bench_ssa.ll" | head -n 1 | cut -d: -f1 || true)
  SSA_OPT_LINE=$(grep -n "call .*@triple_deref" "$BUILD_DIR/bench_ssa_opt.ll" | head -n 1 | cut -d: -f1 || true)
  if [ -n "$SSA_BASE_LINE" ]; then
    echo "IR baseline snippet (ssa):"
    sed -n "$((SSA_BASE_LINE-3)),$((SSA_BASE_LINE+5))p" "$BUILD_DIR/bench_ssa.ll"
  fi
  if [ -n "$SSA_OPT_LINE" ]; then
    echo "IR optimized snippet (ssa):"
    sed -n "$((SSA_OPT_LINE-3)),$((SSA_OPT_LINE+5))p" "$BUILD_DIR/bench_ssa_opt.ll"
  fi
fi

# Guarded Heap-Access Graph Demo (C + LLVM)

This repo demonstrates **exact reconstruction of deep pointer dereference semantics** as guarded heap-access graphs, end-to-end:

- C kernels use **checked primitives** (no UB, errors are explicit).
- An LLVM `opt` pass extracts a guarded graph per kernel.
- A C graph evaluator runs the graph on randomized heaps.
- A driver compares kernel vs graph across trials and records witnesses.

## Layout

- `runtime/checked_ptr.h` + `runtime/checked_ptr.c`
  - Checked primitives (guard, deref/load, select, add) returning `Eval`.
- `runtime/heap_gen.h` + `runtime/heap_gen.c`
  - Random heap/env generator + JSON serializer.
- `programs/kernels.c`
  - Test kernels: `triple_deref`, `field_chain`, `guarded_chain`, `alias_branch`, `mixed_fields`, `add_two`.
- `llvm_pass/`
  - LLVM pass that emits guarded graphs as JSON (one per kernel).
- `checker/graph_eval.*`
  - Graph evaluator (C) that loads JSON graphs and evaluates them.
- `driver/main.c`
  - Runs randomized trials, compares kernel vs graph, prints stats, writes witnesses.
- `run_demo.sh`
  - Single command: build pass + build C code + emit graphs + run driver.

## Semantics

Errors are explicit and never cause UB:

```c
typedef enum { OK=0, ERR_NULL=1, ERR_INVALID=2, ERR_TYPE=3, ERR_MISSING_FIELD=4 } Err;
typedef struct { int ok; Err err; int value; } Eval;
```

Values are **tagged** in `Eval.value`:

- `LSB=1` => int
- `LSB=0` => pointer (`0` is null)

This preserves pointer vs int distinctions for type checking.

## Build + Run

Prereqs (WSL/Linux): `clang`, `opt`, `cmake`, a matching LLVM dev install.

```bash
chmod +x run_demo.sh
./run_demo.sh
```

Outputs:

- Graph JSON files in `out/*.json`
- Witness heaps in `out/*_witness.json`
- Any mismatches in `out/*_mismatch_*.json`

## Notes

- The LLVM pass recognizes the checked primitives (`ck_*`) and builds the graph from those calls.
- `ck_select` is explicit in kernels, so control flow becomes a graph `select` node without CFG analysis.
- Random heaps are generated deterministically from the seed.
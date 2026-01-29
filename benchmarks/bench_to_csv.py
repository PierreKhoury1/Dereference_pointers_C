#!/usr/bin/env python3
import csv
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = ROOT / "viz" / "bench_data.js"
SSA_PATH = ROOT / "viz" / "bench_data_ssa.js"
OUT_DIR = ROOT / "out"
SUMMARY_CSV = OUT_DIR / "bench_results_summary.csv"
RUNS_CSV = OUT_DIR / "bench_results_runs.csv"


def load_js(path: Path) -> dict:
    text = path.read_text(encoding="utf-8").strip()
    text = re.sub(r"^\s*window\.[A-Z_]+\s*=\s*", "", text)
    if text.endswith(";"):
        text = text[:-1]
    return json.loads(text)


def get_stat(stats: dict, key: str):
    if not stats:
        return None
    return stats.get(key)


def load_variants():
    variants = []
    for label, path in (("base", BASE_PATH), ("ssa", SSA_PATH)):
        if path.exists():
            variants.append((label, load_js(path)))
    return variants


def write_summary(rows):
    fields = [
        "variant",
        "timestamp",
        "iters",
        "runs",
        "warmup",
        "ir_calls_baseline",
        "ir_calls_optimized",
        "base_mean_time_ns",
        "base_median_time_ns",
        "base_min_time_ns",
        "base_max_time_ns",
        "base_stdev_time_ns",
        "base_mean_ns_per_iter",
        "base_median_ns_per_iter",
        "base_min_ns_per_iter",
        "base_max_ns_per_iter",
        "base_stdev_ns_per_iter",
        "opt_mean_time_ns",
        "opt_median_time_ns",
        "opt_min_time_ns",
        "opt_max_time_ns",
        "opt_stdev_time_ns",
        "opt_mean_ns_per_iter",
        "opt_median_ns_per_iter",
        "opt_min_ns_per_iter",
        "opt_max_ns_per_iter",
        "opt_stdev_ns_per_iter",
        "speedup_mean",
        "speedup_median",
        "speedup_min",
        "speedup_max",
        "speedup_stdev",
    ]
    with SUMMARY_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_runs(rows):
    fields = [
        "variant",
        "run_index",
        "timestamp",
        "iters",
        "baseline_time_ns",
        "baseline_ns_per_iter",
        "optimized_time_ns",
        "optimized_ns_per_iter",
        "speedup",
    ]
    with RUNS_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main():
    variants = load_variants()
    if not variants:
        raise SystemExit("No benchmark data found in viz/bench_data*.js")

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    summary_rows = []
    run_rows = []

    for label, data in variants:
        cfg = data.get("config", {})
        calls = data.get("ir_call_count", {})
        base_stats_t = data.get("baseline", {}).get("stats_time_ns", {})
        base_stats_i = data.get("baseline", {}).get("stats_ns_per_iter", {})
        opt_stats_t = data.get("optimized", {}).get("stats_time_ns", {})
        opt_stats_i = data.get("optimized", {}).get("stats_ns_per_iter", {})
        spd_stats = data.get("speedup", {}).get("stats", {})

        summary_rows.append(
            {
                "variant": label,
                "timestamp": data.get("timestamp"),
                "iters": cfg.get("iters"),
                "runs": cfg.get("runs"),
                "warmup": cfg.get("warmup"),
                "ir_calls_baseline": calls.get("baseline"),
                "ir_calls_optimized": calls.get("optimized"),
                "base_mean_time_ns": get_stat(base_stats_t, "mean"),
                "base_median_time_ns": get_stat(base_stats_t, "median"),
                "base_min_time_ns": get_stat(base_stats_t, "min"),
                "base_max_time_ns": get_stat(base_stats_t, "max"),
                "base_stdev_time_ns": get_stat(base_stats_t, "stdev"),
                "base_mean_ns_per_iter": get_stat(base_stats_i, "mean"),
                "base_median_ns_per_iter": get_stat(base_stats_i, "median"),
                "base_min_ns_per_iter": get_stat(base_stats_i, "min"),
                "base_max_ns_per_iter": get_stat(base_stats_i, "max"),
                "base_stdev_ns_per_iter": get_stat(base_stats_i, "stdev"),
                "opt_mean_time_ns": get_stat(opt_stats_t, "mean"),
                "opt_median_time_ns": get_stat(opt_stats_t, "median"),
                "opt_min_time_ns": get_stat(opt_stats_t, "min"),
                "opt_max_time_ns": get_stat(opt_stats_t, "max"),
                "opt_stdev_time_ns": get_stat(opt_stats_t, "stdev"),
                "opt_mean_ns_per_iter": get_stat(opt_stats_i, "mean"),
                "opt_median_ns_per_iter": get_stat(opt_stats_i, "median"),
                "opt_min_ns_per_iter": get_stat(opt_stats_i, "min"),
                "opt_max_ns_per_iter": get_stat(opt_stats_i, "max"),
                "opt_stdev_ns_per_iter": get_stat(opt_stats_i, "stdev"),
                "speedup_mean": get_stat(spd_stats, "mean"),
                "speedup_median": get_stat(spd_stats, "median"),
                "speedup_min": get_stat(spd_stats, "min"),
                "speedup_max": get_stat(spd_stats, "max"),
                "speedup_stdev": get_stat(spd_stats, "stdev"),
            }
        )

        base_times = data.get("baseline", {}).get("times_ns", []) or []
        base_nspi = data.get("baseline", {}).get("ns_per_iter", []) or []
        opt_times = data.get("optimized", {}).get("times_ns", []) or []
        opt_nspi = data.get("optimized", {}).get("ns_per_iter", []) or []
        speedups = data.get("speedup", {}).get("values", []) or []
        run_count = min(len(base_times), len(base_nspi), len(opt_times), len(opt_nspi), len(speedups))

        for i in range(run_count):
            run_rows.append(
                {
                    "variant": label,
                    "run_index": i + 1,
                    "timestamp": data.get("timestamp"),
                    "iters": cfg.get("iters"),
                    "baseline_time_ns": base_times[i],
                    "baseline_ns_per_iter": base_nspi[i],
                    "optimized_time_ns": opt_times[i],
                    "optimized_ns_per_iter": opt_nspi[i],
                    "speedup": speedups[i],
                }
            )

    write_summary(summary_rows)
    write_runs(run_rows)


if __name__ == "__main__":
    main()

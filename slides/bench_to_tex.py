#!/usr/bin/env python3
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
base_path = ROOT / "viz" / "bench_data.js"
ssa_path = ROOT / "viz" / "bench_data_ssa.js"
output_path = Path(__file__).resolve().parent / "bench_results.tex"


def load_js(path):
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"^\s*window\.[A-Z_]+\s*=\s*", "", text.strip())
    if text.endswith(";"):
        text = text[:-1]
    return json.loads(text)


def ns_to_ms(ns):
    return ns / 1e6


def fmt_ms(ns):
    return f"{ns_to_ms(ns):.2f}"


def fmt_ns_per_iter(val):
    return f"{val:.3f}"


def fmt_speed(val):
    return f"{val:.2f}"


def table_row(label, data):
    base = data["baseline"]
    opt = data["optimized"]
    spd = data["speedup"]
    return (
        f"{label} & {fmt_ms(base['stats_time_ns']['mean'])} & {fmt_ns_per_iter(base['stats_ns_per_iter']['mean'])}"
        f" & {fmt_ms(opt['stats_time_ns']['mean'])} & {fmt_ns_per_iter(opt['stats_ns_per_iter']['mean'])}"
        f" & {fmt_speed(spd['stats']['mean'])}\\\\"
    )


def main():
    if not base_path.exists():
        raise SystemExit(f"Missing {base_path}")
    if not ssa_path.exists():
        raise SystemExit(f"Missing {ssa_path}")

    base_data = load_js(base_path)
    ssa_data = load_js(ssa_path)

    iters = base_data["config"]["iters"]
    runs = base_data["config"]["runs"]

    lines = []
    lines.append("% Auto-generated from run_bench.sh data")
    lines.append(f"\\newcommand{{\\BenchIters}}{{{iters:,}}}")
    lines.append(f"\\newcommand{{\\BenchRuns}}{{{runs}}}")
    lines.append("\\newcommand{\\BenchTable}{%")
    lines.append("\\begin{table}[h]")
    lines.append("\\centering")
    lines.append("\\small")
    lines.append("\\begin{tabular}{lrrrrr}")
    lines.append("\\toprule")
    lines.append("Variant & Base mean (ms) & Base ns/iter & Opt mean (ms) & Opt ns/iter & Speedup \\\\")
    lines.append("\\midrule")
    lines.append(table_row("Unoptimized", base_data))
    lines.append(table_row("SSA", ssa_data))
    lines.append("\\bottomrule")
    lines.append("\\end{tabular}")
    lines.append("\\caption{Mean timings over \\BenchRuns runs at \\BenchIters iterations.}")
    lines.append("\\end{table}")
    lines.append("}%")

    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

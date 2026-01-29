$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$basePath = Join-Path $root "viz\\bench_data.js"
$ssaPath = Join-Path $root "viz\\bench_data_ssa.js"
$outDir = Join-Path $root "out"
$summaryCsv = Join-Path $outDir "bench_results_summary.csv"
$runsCsv = Join-Path $outDir "bench_results_runs.csv"

function Load-BenchData {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        return $null
    }
    $text = Get-Content -Raw -Path $Path
    $text = [regex]::Replace($text, "^\s*window\.[A-Z_]+\s*=\s*", "")
    $text = $text.Trim()
    if ($text.EndsWith(";")) {
        $text = $text.Substring(0, $text.Length - 1)
    }
    return $text | ConvertFrom-Json
}

function Get-StatValue {
    param($Stats, [string]$Name)
    if ($null -eq $Stats) {
        return $null
    }
    return $Stats.$Name
}

$variants = @()
foreach ($entry in @(@("base", $basePath), @("ssa", $ssaPath))) {
    $label = $entry[0]
    $path = $entry[1]
    $data = Load-BenchData -Path $path
    if ($null -ne $data) {
        $variants += [pscustomobject]@{
            Label = $label
            Data = $data
        }
    }
}

if ($variants.Count -eq 0) {
    throw "No benchmark data found in viz/bench_data*.js"
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$summaryRows = @()
$runRows = @()

foreach ($variant in $variants) {
    $label = $variant.Label
    $data = $variant.Data

    $cfg = $data.config
    $calls = $data.ir_call_count
    $baseStatsT = $data.baseline.stats_time_ns
    $baseStatsI = $data.baseline.stats_ns_per_iter
    $optStatsT = $data.optimized.stats_time_ns
    $optStatsI = $data.optimized.stats_ns_per_iter
    $spdStats = $data.speedup.stats

    $summaryRows += [pscustomobject]([ordered]@{
        variant = $label
        timestamp = $data.timestamp
        iters = $cfg.iters
        runs = $cfg.runs
        warmup = $cfg.warmup
        ir_calls_baseline = $calls.baseline
        ir_calls_optimized = $calls.optimized
        base_mean_time_ns = (Get-StatValue $baseStatsT "mean")
        base_median_time_ns = (Get-StatValue $baseStatsT "median")
        base_min_time_ns = (Get-StatValue $baseStatsT "min")
        base_max_time_ns = (Get-StatValue $baseStatsT "max")
        base_stdev_time_ns = (Get-StatValue $baseStatsT "stdev")
        base_mean_ns_per_iter = (Get-StatValue $baseStatsI "mean")
        base_median_ns_per_iter = (Get-StatValue $baseStatsI "median")
        base_min_ns_per_iter = (Get-StatValue $baseStatsI "min")
        base_max_ns_per_iter = (Get-StatValue $baseStatsI "max")
        base_stdev_ns_per_iter = (Get-StatValue $baseStatsI "stdev")
        opt_mean_time_ns = (Get-StatValue $optStatsT "mean")
        opt_median_time_ns = (Get-StatValue $optStatsT "median")
        opt_min_time_ns = (Get-StatValue $optStatsT "min")
        opt_max_time_ns = (Get-StatValue $optStatsT "max")
        opt_stdev_time_ns = (Get-StatValue $optStatsT "stdev")
        opt_mean_ns_per_iter = (Get-StatValue $optStatsI "mean")
        opt_median_ns_per_iter = (Get-StatValue $optStatsI "median")
        opt_min_ns_per_iter = (Get-StatValue $optStatsI "min")
        opt_max_ns_per_iter = (Get-StatValue $optStatsI "max")
        opt_stdev_ns_per_iter = (Get-StatValue $optStatsI "stdev")
        speedup_mean = (Get-StatValue $spdStats "mean")
        speedup_median = (Get-StatValue $spdStats "median")
        speedup_min = (Get-StatValue $spdStats "min")
        speedup_max = (Get-StatValue $spdStats "max")
        speedup_stdev = (Get-StatValue $spdStats "stdev")
    })

    $baseTimes = @($data.baseline.times_ns)
    $baseNspi = @($data.baseline.ns_per_iter)
    $optTimes = @($data.optimized.times_ns)
    $optNspi = @($data.optimized.ns_per_iter)
    $speedups = @($data.speedup.values)
    $runCount = @(
        $baseTimes.Count,
        $baseNspi.Count,
        $optTimes.Count,
        $optNspi.Count,
        $speedups.Count
    ) | Measure-Object -Minimum | Select-Object -ExpandProperty Minimum

    for ($i = 0; $i -lt $runCount; $i++) {
        $runRows += [pscustomobject]([ordered]@{
            variant = $label
            run_index = $i + 1
            timestamp = $data.timestamp
            iters = $cfg.iters
            baseline_time_ns = $baseTimes[$i]
            baseline_ns_per_iter = $baseNspi[$i]
            optimized_time_ns = $optTimes[$i]
            optimized_ns_per_iter = $optNspi[$i]
            speedup = $speedups[$i]
        })
    }
}

$summaryRows | Export-Csv -NoTypeInformation -Path $summaryCsv
$runRows | Export-Csv -NoTypeInformation -Path $runsCsv

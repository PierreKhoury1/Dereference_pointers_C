const levels = [
  {
    id: "c",
    title: "C source (conceptual)",
    baseline: {
      title: "C source",
      desc: "Each loop iteration calls triple_deref, re-running guards and loads every time.",
      code: `
<pre><code class="language-c">
<span class="tok-kw">for</span> (<span class="tok-type">int</span> i = 0; i &lt; N; i++) {
  <span class="tok-type">Eval</span> r = <span class="diff-remove"><span class="tok-fn">triple_deref</span>(heap, p, 0)</span>;
  sum += r.value;
}
</code></pre>
`
    },
    optimized: {
      title: "C source (conceptual)",
      desc: "We hoist the deref chain once and reuse the cached value inside the loop.",
      code: `
<pre><code class="language-c">
<span class="tok-type">Eval</span> cached = <span class="diff-add"><span class="tok-fn">triple_deref</span>(heap, p, 0)</span>; <span class="tok-comment">// hoisted</span>
<span class="tok-kw">for</span> (<span class="tok-type">int</span> i = 0; i &lt; N; i++) {
  <span class="tok-type">Eval</span> r = <span class="diff-add">cached</span>;
  sum += r.value;
}
</code></pre>
`
    }
  },
  {
    id: "guarded",
    title: "Checked / guarded semantics",
    baseline: {
      title: "Guarded semantics",
      desc: "The loop repeats pointer guards and dereferences for each iteration.",
      code: `
<pre><code class="language-text">
<span class="tok-comment">// inside loop</span>
vp  = <span class="diff-remove">guard_ptr</span>(input p)
v1  = <span class="diff-remove">guard_nonnull</span>(vp)
v2  = <span class="diff-remove">load_ptr</span>(v1)
v3  = <span class="diff-remove">guard_ptr</span>(v2)
v4  = <span class="diff-remove">guard_nonnull</span>(v3)
v5  = <span class="diff-remove">load_ptr</span>(v4)
v6  = <span class="diff-remove">guard_ptr</span>(v5)
v7  = <span class="diff-remove">guard_nonnull</span>(v6)
v8  = <span class="diff-remove">load_ptr</span>(v7)
return v8
</code></pre>
`
    },
    optimized: {
      title: "Guarded semantics",
      desc: "All guards and loads run once, then the loop uses the cached result.",
      code: `
<pre><code class="language-text">
<span class="tok-comment">// preheader</span>
vp  = <span class="diff-add">guard_ptr</span>(input p)
v1  = <span class="diff-add">guard_nonnull</span>(vp)
v2  = <span class="diff-add">load_ptr</span>(v1)
v3  = <span class="diff-add">guard_ptr</span>(v2)
v4  = <span class="diff-add">guard_nonnull</span>(v3)
v5  = <span class="diff-add">load_ptr</span>(v4)
v6  = <span class="diff-add">guard_ptr</span>(v5)
v7  = <span class="diff-add">guard_nonnull</span>(v6)
<span class="diff-add">cached = load_ptr</span>(v7)

<span class="tok-comment">// inside loop</span>
return <span class="diff-add">cached</span>
</code></pre>
`
    }
  },
  {
    id: "llvm",
    title: "LLVM IR",
    baseline: {
      title: "LLVM IR",
      desc: "The loop body still calls triple_deref and extracts the value every iteration.",
      code: `
<pre><code class="language-llvm">
; inside loop
%val = <span class="diff-remove">call</span> { i64, i32 } @triple_deref(ptr %heap, i32 %p, i32 0)
%v   = extractvalue { i64, i32 } %val, 1
%sum = add i64 %sum, %v
</code></pre>
`
    },
    optimized: {
      title: "LLVM IR",
      desc: "The call is hoisted and cached; the loop just loads the cached struct.",
      code: `
<pre><code class="language-llvm">
; inside loop
%cached = <span class="diff-add">load</span> { i64, i32 }, ptr %triple_deref_cache
%v      = extractvalue { i64, i32 } %cached, 1
%sum    = add i64 %sum, %v
</code></pre>
`
    }
  },
  {
    id: "asm",
    title: "Assembly intuition",
    baseline: {
      title: "Assembly intuition",
      desc: "A call in the loop means repeated loads, checks, and branches per iteration.",
      code: `
<pre><code class="language-asm">
loop:
  <span class="diff-remove">call triple_deref</span>   <span class="tok-comment">; guard+load chain</span>
  add  s0, s0, a0
  j    loop
</code></pre>
`
    },
    optimized: {
      title: "Assembly intuition",
      desc: "The loop reduces to a cached load and add; the heavy work is outside.",
      code: `
<pre><code class="language-asm">
preheader:
  <span class="diff-add">call triple_deref</span>
  <span class="diff-add">store cached</span>

loop:
  <span class="diff-add">load cached</span>
  add  s0, s0, a0
  j    loop
</code></pre>
`
    }
  }
];

const levelTabs = document.getElementById("level-tabs");
const baselineTitle = document.getElementById("baseline-title");
const optimizedTitle = document.getElementById("optimized-title");
const baselineDesc = document.getElementById("baseline-desc");
const optimizedDesc = document.getElementById("optimized-desc");
const baselineCode = document.getElementById("baseline-code");
const optimizedCode = document.getElementById("optimized-code");

const benchMeta = document.getElementById("bench-meta");
const benchSpeedup = document.getElementById("bench-speedup");
const benchBaseMean = document.getElementById("bench-base-mean");
const benchBaseNspi = document.getElementById("bench-base-nspi");
const benchOptMean = document.getElementById("bench-opt-mean");
const benchOptNspi = document.getElementById("bench-opt-nspi");
const benchCallCount = document.getElementById("bench-call-count");
const benchRows = document.getElementById("bench-rows");
const benchSpark = document.getElementById("bench-spark");

let currentLevel = levels[0].id;

function setActiveLevel(id) {
  currentLevel = id;
  const level = levels.find((lvl) => lvl.id === id);
  if (!level) return;

  baselineTitle.textContent = level.baseline.title;
  optimizedTitle.textContent = level.optimized.title;
  baselineDesc.textContent = level.baseline.desc;
  optimizedDesc.textContent = level.optimized.desc;
  baselineCode.innerHTML = level.baseline.code;
  optimizedCode.innerHTML = level.optimized.code;

  document.querySelectorAll(".tab").forEach((tab) => {
    tab.classList.toggle("is-active", tab.dataset.level === id);
  });
}

function renderTabs() {
  levelTabs.innerHTML = "";
  levels.forEach((level) => {
    const btn = document.createElement("button");
    btn.className = "tab";
    btn.type = "button";
    btn.dataset.level = level.id;
    btn.setAttribute("role", "tab");
    btn.textContent = level.title;
    btn.addEventListener("click", () => setActiveLevel(level.id));
    levelTabs.appendChild(btn);
  });
}

function setupViewControls() {
  document.querySelectorAll("[data-mode]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.body.dataset.mode = btn.dataset.mode;
      document.querySelectorAll("[data-mode]").forEach((el) => {
        el.classList.toggle("is-active", el.dataset.mode === btn.dataset.mode);
      });
    });
  });

  document.querySelectorAll("[data-side]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.body.dataset.side = btn.dataset.side;
      document.querySelectorAll("[data-side]").forEach((el) => {
        el.classList.toggle("is-active", el.dataset.side === btn.dataset.side);
      });
    });
  });
}

renderTabs();
setupViewControls();
setActiveLevel(currentLevel);

function formatTime(ns) {
  if (ns === undefined || ns === null || Number.isNaN(ns)) return "—";
  const ms = ns / 1e6;
  if (ms < 1000) return `${ms.toFixed(2)} ms`;
  return `${(ms / 1000).toFixed(2)} s`;
}

function formatNsPerIter(val) {
  if (val === undefined || val === null || Number.isNaN(val)) return "—";
  return `${Number(val).toFixed(3)} ns/iter`;
}

function formatSpeedup(val) {
  if (val === undefined || val === null || Number.isNaN(val)) return "—";
  return `${Number(val).toFixed(3)}x`;
}

function renderSparkline(values) {
  if (!benchSpark) return;
  benchSpark.innerHTML = "";
  if (!values || !values.length) {
    benchSpark.textContent = "No run data yet.";
    return;
  }
  const width = Math.max(240, benchSpark.clientWidth || 320);
  const height = 60;
  const padding = 8;
  const min = Math.min(...values);
  const max = Math.max(...values);
  const span = max - min || 1;
  const points = values.map((v, i) => {
    const x = padding + (i / (values.length - 1 || 1)) * (width - padding * 2);
    const y = height - padding - ((v - min) / span) * (height - padding * 2);
    return [x, y];
  });

  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", width);
  svg.setAttribute("height", height);
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);

  const polyline = document.createElementNS(svg.namespaceURI, "polyline");
  polyline.setAttribute("fill", "none");
  polyline.setAttribute("stroke", "#0f6f61");
  polyline.setAttribute("stroke-width", "2");
  polyline.setAttribute("points", points.map((p) => p.join(",")).join(" "));
  svg.appendChild(polyline);

  points.forEach((p) => {
    const dot = document.createElementNS(svg.namespaceURI, "circle");
    dot.setAttribute("cx", p[0]);
    dot.setAttribute("cy", p[1]);
    dot.setAttribute("r", "2.5");
    dot.setAttribute("fill", "#d6a23a");
    svg.appendChild(dot);
  });

  benchSpark.appendChild(svg);
}

function populateBench() {
  if (typeof window.BENCH_DATA === "undefined") {
    return;
  }
  const data = window.BENCH_DATA;
  const config = data.config || {};
  const baseStats = data.baseline?.stats_time_ns || {};
  const optStats = data.optimized?.stats_time_ns || {};
  const baseNspiStats = data.baseline?.stats_ns_per_iter || {};
  const optNspiStats = data.optimized?.stats_ns_per_iter || {};
  const speedupStats = data.speedup?.stats || {};
  const callCounts = data.ir_call_count || {};

  benchMeta.textContent = `iters=${config.iters} runs=${config.runs} warmup=${config.warmup} • ${data.timestamp || "latest"}`;
  benchSpeedup.textContent = formatSpeedup(speedupStats.mean);
  benchBaseMean.textContent = formatTime(baseStats.mean);
  benchOptMean.textContent = formatTime(optStats.mean);
  benchBaseNspi.textContent = formatNsPerIter(baseNspiStats.mean);
  benchOptNspi.textContent = formatNsPerIter(optNspiStats.mean);
  benchCallCount.textContent = `${callCounts.baseline ?? "—"} → ${callCounts.optimized ?? "—"}`;

  const baseTimes = data.baseline?.times_ns || [];
  const optTimes = data.optimized?.times_ns || [];
  const speedups = data.speedup?.values || [];
  benchRows.innerHTML = "";
  const count = Math.max(baseTimes.length, optTimes.length, speedups.length);
  for (let i = 0; i < count; i += 1) {
    const row = document.createElement("div");
    row.className = "bench-row";
    const left = document.createElement("span");
    left.textContent = `run ${i + 1}`;
    const right = document.createElement("span");
    const base = baseTimes[i];
    const opt = optTimes[i];
    const spd = speedups[i];
    right.textContent = `${formatTime(base)} → ${formatTime(opt)} (${formatSpeedup(spd)})`;
    row.appendChild(left);
    row.appendChild(right);
    benchRows.appendChild(row);
  }

  renderSparkline(speedups);
}

populateBench();

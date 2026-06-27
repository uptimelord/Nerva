# v1.0 Benchmark Suite

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

Maps Architecture v0.4 Part 9 benchmarks to scripted graph tasks runnable via
`nerva_bench` (new) and existing unit tests. Benchmark #5 (belief/state tracking)
is **deferred** — out of v0.x scope per Implementation Plan non-goals.

**Promote if:**

- `tools/nerva_bench.c` runs all in-scope benchmarks (1–4, 6–10) and writes `bench.log`
- each benchmark reports pass/fail, ticks used, peak event queue depth, and trace path
- thresholds below are met on `nerva_config_test()` unless noted
- all existing unit tests still pass (`.\build.bat`)
- benchmark runner performs no heap allocation inside the tick loop
- efficiency benchmark (#10) reports peak queue depth, estimated RAM, ticks/sec, and routine fluid rate <5%

**Kill if:**

- benchmarks require hand-coded graph answers bypassing engine APIs
- pass/fail depends on tuned constants with no held-out variant
- event queue overflow is silent during any benchmark
- fluid workspace activation rate ≥5% on routine (pre-trained) inputs in benchmark #10
- any benchmark mutates global engine caps or skips adjacency rebuild after load

## Benchmarks

| # | Name | Success criterion | Source / new |
|---|------|-------------------|--------------|
| 1 | Few-shot relation | >80% apply accuracy at N=5 distinct pairs; report curve at N=1,3,5,10 | new `bench_fewshot.c` |
| 2 | Transitive reasoning | C fires above Θ within 100 ticks after A→B, B→C training | extend schema transitive test |
| 3 | Exception handling | schema path for bird→fly; blocker suppresses penguin→fly (>95% each) | `examples/penguin.nerva` + test |
| 4 | Container movement | ball `inside` box + box `moved_to` shelf → query ball `located_at` shelf within 200 ticks | new schema-apply script |
| 6 | Ambiguity resolution | 3:1 bias wins within 50 ticks in >80% of trials | routing competition test |
| 7 | Memory consolidation | 5-node episode retrievable in order (>90% = 4/5 nodes) after idle replay | extend memory test |
| 8 | Forgetting | unreinforced episode below Θ after hold + idle; report decay curve | extend memory forget test |
| 9 | Contradiction repair | contradiction within 2 ticks; lower-confidence suppressed within 10 | exception tests |
| 10 | Compute efficiency | all above complete; peak queue < cap; est. RAM < 7 GB; ≥1000 ticks/sec on test config | new metrics in bench runner |

**Deferred:** #5 belief/state tracking (requires per-agent belief contexts not in v0.x).

## Setup

stage: v1.0  
runner: `build/nerva_bench.exe` (new)  
config: `nerva_config_test()` for correctness; default config for efficiency sweep  
artifacts: `experiments/v10_benchmark_suite/bench.log`, per-benchmark `trace.log` snippets

## Commands

```text
BUILD: .\build.bat (unit tests + nerva_bench target)
RUN:   .\build\nerva_bench.exe --all
OUT:   bench.log with benchmark name, pass, ticks, peak_queue, notes
TRACE: each benchmark appends tick fire path to trace.log (No trace, no trust)
```

## Expected trace

```text
bench.log:
benchmark=few_shot_relation pass=1 ticks=... peak_queue=...
benchmark=transitive_reasoning pass=1 ...
...
benchmark=compute_efficiency pass=1 ticks_per_sec=... peak_queue=... fluid_routine_pct=...

trace.log: per-benchmark activation paths for failed or sample passed runs
```

Run: `.\build.bat` then `.\build\nerva_bench.exe --all`.

## Verdict

**Promote** — 9/9 benchmarks pass; all Part 9 in-scope tasks meet thresholds on `nerva_config_test()`. Efficiency: peak_queue=2, est_ram=0.89 MB, 10000 ticks/sec, 0% routine fluid. Evidence: [results.md](results.md), `bench.log`, `trace.log`.

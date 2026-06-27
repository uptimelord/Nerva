# Results: v1.0 Benchmark Suite

## Decision Rule

See [README.md](README.md).

## Setup

commit: 6a6ce6e  
date: 2026-06-27  
stage: v1.0  
runner: `build/nerva_bench.exe`  
config: `nerva_config_test()`  
seed: deterministic, no random seed

## Commands

Unit test: `tests/test_bench.c` → `test_bench_all_pass`  
CLI: `.\build\nerva_bench.exe --all` (default runs all benchmarks)

Build: `.\build.bat`

Artifacts:

- `experiments/v10_benchmark_suite/bench.log`
- `experiments/v10_benchmark_suite/trace.log`

## Results

task_success_rate: 9/9 benchmarks pass + 80/80 unit tests total  
few_shot_relation: N=5 held-out schema apply ok (curve at N=1,3,5,10)  
transitive_reasoning: C fired in 3 ticks (limit 100)  
exception_handling: bird→fly 20/20, penguin blocked 5/5  
container_movement: located_at edge inferred, shelf fired in 2 ticks (limit 200)  
ambiguity_resolution: 3:1 bias wins 5/5 within 50 ticks  
memory_consolidation: consolidated=yes, retrieval 4/4 follow-on nodes  
forgetting: marked_delete after hold period, charge=0  
contradiction_repair: blocker trace ≤2 ticks, fly suppressed  
compute_efficiency: peak_queue=2, est_ram=0.89 MB, tps=10000, fluid_routine=0.0%  
benchmark_5_belief_tracking: deferred (non-goal)  
variance/noise: deterministic run, no random seed used

## Trace Summary

```text
bench.log:
benchmark=few_shot_relation pass=1 ...
benchmark=transitive_reasoning pass=1 ticks=3 ...
benchmark=exception_handling pass=1 bird->fly 20/20 penguin blocked 5/5
benchmark=container_movement pass=1 located_at edge=yes shelf in 2 ticks
benchmark=ambiguity_resolution pass=1 3:1 bias wins 5/5
benchmark=memory_consolidation pass=1 retrieval=4/4 nodes
benchmark=forgetting pass=1 marked_delete=yes
benchmark=contradiction_repair pass=1 blocker_trace<=2t=yes
benchmark=compute_efficiency pass=1 tps=10000 ram=0.89MB fluid=0.0%
summary all_pass=1

trace.log: per-benchmark fire paths for sample runs
```

## Decision

Promote

## Notes

- Benchmark runner in `src/nerva_bench.c`; CLI in `tools/nerva_bench.c`.
- Belief/state tracking (#5) deferred per Implementation Plan non-goals.
- Efficiency metrics use test-config caps (well under 8 GB RAM budget).
- All benchmarks write tick-level fire evidence to `trace.log`.

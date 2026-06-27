# Results: v0.3 Feedback Write-Back

## Decision Rule

See [README.md](README.md).

## Setup

commit: dd4fc43  
date: 2026-06-27  
stage: v0.3  
graph: poodle/dog/animal kind_of chain + cat->animal decoy in unused-edge test  
parameters: `nerva_config_test()` defaults  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_learning.c`:

- `test_feedback_correct_strengthens_used_edges`
- `test_feedback_wrong_weakens_used_edges`
- `test_feedback_skips_unused_edges`
- `test_feedback_wrong_gates_after_repeat`
- `test_weight_clipping_prevents_runaway`
- `test_feedback_scoped_to_latest_query`
- `test_feedback_poodle_experiment_artifacts`

Build: `.\build.bat`

Trace artifacts:

- `experiments/v03_feedback_writeback/trace.log`
- `experiments/v03_feedback_writeback/mutation.log`

## Results

task_success_rate: 7/7 learning tests + 4/4 trace + 8/8 event + 9/9 graph (28 total)  
avg_ticks_per_query: 4 (poodle demo tick budget)  
avg_events_per_query: 2 used-path traces per query  
peak_queue_depth: tracked via `debug.event_depth_max`  
memory_used_mb: not measured (unit test scope)  
trace_records_count: 2 used-path traces per poodle query  
edge_mutations_count: 2 weight mutations on correct feedback demo  
mutation_count: logged in `mutation.log` with old/new weight and gate values  
variance/noise: deterministic run, no random seed used

## Trace Summary

Poodle demo after correct feedback:

```text
tick=1 edge=0 poodle->dog (used path, trace_tag set)
tick=2 edge=1 dog->animal (used path)
mutation: edge 0 weight 256->272, edge 1 weight 256->272
cat->animal decoy edge unchanged
```

## Decision

Promote

## Notes

- Feedback queues mutations; graph weights/gates change only on explicit `nerva_apply_mutations`.
- `nerva_activate_node` assigns `active_query_tag`; feedback defaults to the latest query only.
- `wrong_feedback_count` updates on mutation apply for feedback reason codes.
- Wrong feedback uses projected count for gate-close queuing; gate closes at threshold (2).
- Weight deltas clipped to `[weight_min_q8_8, weight_max_q8_8]`.

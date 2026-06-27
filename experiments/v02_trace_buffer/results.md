# Results: v0.2 Trace Buffer

## Decision Rule

See [README.md](README.md).

## Setup

commit: c47e5f0  
date: 2026-06-27  
stage: v0.2  
graph: poodle/dog/animal kind_of chain  
parameters: `nerva_config_test()` defaults  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_trace.c`:

- `test_trace_records_poodle_hops`
- `test_trace_decay_within_tolerance`
- `test_trace_ring_bounded`
- `test_trace_find_used_path_for_feedback`

Build: `.\build.bat`

Trace artifact: `experiments/v02_trace_buffer/trace.log` (written in path order by poodle trace test)

## Results

task_success_rate: 4/4 trace tests + 8/8 event tests + 9/9 graph tests (21 total)  
avg_ticks_per_query: 4 (poodle demo tick budget)  
avg_events_per_query: 2 edge traces on poodle two-hop path  
peak_queue_depth: tracked via `debug.event_depth_max`  
memory_used_mb: not measured (unit test scope)  
trace_records_count: 2 used-path traces on poodle demo (`debug.traces_recorded` >= 2)  
mutation_count: 0 (v0.2)  
variance/noise: deterministic run, no random seed used

## Trace Summary

Poodle demo used-path traces:

```text
tick=1 edge=0 poodle->dog (used path, trace_tag set)
tick=2 edge=1 dog->animal (used path, trace_tag set)
pre/post decay within 5% per tick (unit test)
```

## Decision

Promote

## Notes

- Trace ring is bounded by `max_traces`; overflow overwrites oldest entries.
- Decay scans only the recent trace window (`trace_decay_scan_limit`), not all nodes.
- Refractory is derived lazily from `last_fired_tick` + `refractory_max` (no per-tick node scan).
- `nerva_trace_find_edge_in_recent` accepts optional `trace_tag_filter` (0 = any tag).
- `nerva_trace_save_path` writes traces oldest-first for active-path artifacts.

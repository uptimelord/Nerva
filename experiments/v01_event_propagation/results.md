# Results: v0.1 Event Propagation

## Decision Rule

See [README.md](README.md).

## Setup

commit: 75498d2  
date: 2026-06-27  
stage: v0.1  
graph: poodle/dog/animal kind_of chain + alpha/beta/gamma generic chain  
parameters: `nerva_config_test()` defaults  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_event.c`:

- `test_event_heap_orders_due_ticks`
- `test_event_overflow_not_silent`
- `test_event_overflow_admits_stronger_signal`
- `test_fire_without_adjacency_still_records`
- `test_refractory_decays_on_idle_ticks`
- `test_activation_fires_once_with_refractory`
- `test_event_propagates_poodle_dog_animal`
- `test_propagation_not_name_hardcoded`

Build: `.\build.bat`

Trace artifact: `experiments/v01_event_propagation/trace.log` (written by poodle test)

## Results

task_success_rate: 8/8 event tests + 9/9 graph tests  
avg_ticks_per_query: 4 (poodle demo tick budget)  
avg_events_per_query: 3 fired nodes, queue drained to 0  
peak_queue_depth: tracked via `debug.event_depth_max`  
memory_used_mb: not measured (unit test scope)  
trace_records_count: 3 fire log entries on poodle demo  
mutation_count: 0 (v0.1)  
variance/noise: deterministic run, no random seed used

## Trace Summary

Poodle demo fire sequence:

```text
tick 0: poodle fired
tick 1: dog fired
tick 2: animal fired
path: poodle -> dog -> animal
```

Generic alpha/beta/gamma chain reaches gamma on tick 2 without name-specific traversal code.

## Decision

Promote

## Notes

- Fire log is v0.1 trace scaffold; full path-tagged trace ring lands in v0.2.
- `nerva_fire_node` uses sparse CSR adjacency (`sorted_edges`), not graph-wide edge scans.
- Refractory countdown advances on wall-clock ticks even when a node receives no new events.

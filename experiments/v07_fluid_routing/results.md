# Results: v0.7 Fluid / Crystallized Routing

## Decision Rule

See [README.md](README.md).

## Setup

commit: 89e9a73  
date: 2026-06-27  
stage: v0.7  
graph: routine poodle chain; novel A→Z (no path); penguin blocker contradiction  
parameters: `nerva_config_test()` (`fluid_threshold_base_q8_8=640`)  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_routing.c`:

- `test_routing_routine_difficulty_low`
- `test_routing_routine_stays_crystallized`
- `test_routing_novel_triggers_fluid`
- `test_routing_contradiction_triggers_fluid`
- `test_routing_adaptive_threshold_fatigue`
- `test_routing_fluid_workspace_bounded`
- `test_routing_stale_adjacency_routine_crystallized`
- `test_routing_lateral_inhibition_suppresses_rivals`
- `test_routing_experiment_artifacts`

Build: `.\build.bat`

Routing artifact: `experiments/v07_fluid_routing/routing.log`  
Trace artifact: `experiments/v07_fluid_routing/trace.log`

## Results

task_success_rate: 9/9 routing tests + 9/9 memory + 8/8 schema + 5/5 exception + 6/6 prediction + 7/7 learning + 4/4 trace + 8/8 event + 9/9 graph (65 total)  
routine_crystallized_rate: 20/20 (100%, fluid activation 0%)  
novel_query: fluid activation within propagation batch  
contradiction_query: fluid activation after exception trace  
routing_log: routine crystallized=1 fluid=0; novel fluid=1; contradiction fluid=1  
lateral_inhibition: losing rival activation reduced below winner at 200 q8.8  
stale_adjacency: auto-rebuild before reachability keeps routine crystallized  
variance/noise: deterministic run, no random seed used

## Trace Summary

```text
routine poodle x20: difficulty below threshold -> crystallized
novel A->Z: unreachable path, difficulty 768 > threshold 640 -> fluid
penguin->fly + blocker: exception trace -> contradiction -> fluid
fluid workspace: top active nodes capped, lateral inhibition applied
```

## Decision

Promote

## Notes

- Routing rebuilds adjacency on demand when edges exist but CSR is stale.
- Unreachable queries use zero confidence so difficulty clears the fluid threshold.
- Fluid workspace is debug-only in v0.7: no graph mutation or live replay events.

# Results: v0.4 Exception / Blocker Handling

## Decision Rule

See [README.md](README.md).

## Setup

commit: 0a9cab3  
date: 2026-06-27  
stage: v0.4  
graph: bird/penguin/fly with `BLOCK penguin fly`  
parameters: `nerva_config_test()` defaults  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_exception.c`:

- `test_blocker_created_via_mutation_queue`
- `test_bird_query_reaches_fly`
- `test_penguin_query_blocks_fly`
- `test_penguin_suppression_is_partial`
- `test_penguin_experiment_artifacts`

Build: `.\build.bat`

Trace artifact: `experiments/v04_exception_handling/trace.log`  
Mutation artifact: `experiments/v04_exception_handling/mutation.log`

## Results

task_success_rate: 5/5 exception tests + 6/6 prediction + 7/7 learning + 4/4 trace + 8/8 event + 9/9 graph (39 total)  
avg_ticks_per_query: 4  
trace_records_count: blocker edge trace with `NERVA_TRACE_BLOCKER` on penguin query  
edge_mutations_count: 1 `NERVA_MUT_ADD_BLOCKER_EDGE` logged in `mutation.log` (reason=5)  
blockers_applied: 1  
exceptions_suppressed: partial suppression on bird->fly path (`flags=0x0301` = USED_PATH|BLOCKER|EXCEPTION)  
variance/noise: deterministic run, no random seed used

## Trace Summary

```text
bird query: bird fired -> bird->fly used-path -> fly fired
penguin query: penguin fired -> penguin->fly blocker (flags=0x0101)
               bird->fly generalization path suppressed (flags=0x0301)
mutation log: tick=0 type=4 edge=2 reason=5 weight 0->256
```

## Decision

Promote

## Notes

- Blocker edges propagate inhibitory signal on fire; partial suppression applies on positive inbound when blocker source fired in the same query.
- Suppression flags are recorded on the suppressed path trace at record time (not retroactively).
- Incoming blocker lookup uses CSR `blocker_in_*` indices rebuilt with adjacency (not full edge scans per event).

# Results: v0.9 Save / Load Persistence

## Decision Rule

See [README.md](README.md).

## Setup

commit: 25a4c15  
date: 2026-06-27  
stage: v0.9  
graph: poodle `kind_of` chain; promoted schema; memory episode; feedback weight round-trip  
snapshot: `experiments/v09_persistence/roundtrip.nerva`  
parameters: `nerva_config_test()`  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_persist.c`:

- `test_persist_roundtrip_graph`
- `test_persist_rejects_bad_crc`
- `test_persist_rejects_truncated_header`
- `test_persist_rejects_bad_header_layout`
- `test_persist_feedback_weights_roundtrip`
- `test_persist_memory_schema_roundtrip`
- `test_persist_experiment_artifacts`

Parser: `SAVE` / `LOAD` in `src/nerva_parse.c`  
Example: `examples/save_poodle.nerva`

Build: `.\build.bat`

Artifacts:

- `experiments/v09_persistence/trace.log`
- `experiments/v09_persistence/persist.log`

## Results

task_success_rate: 7/7 persist tests + 7/7 parse + 9/9 routing + 9/9 memory + 8/8 schema + 5/5 exception + 6/6 prediction + 7/7 learning + 4/4 trace + 8/8 event + 9/9 graph (79 total)  
roundtrip_nodes_edges: 3 nodes, 2 edges preserved  
roundtrip_memory_schema: memory=1 schema=1 with charge and flags preserved  
roundtrip_reachability: poodle→animal true after load  
feedback_weight: strengthened edge weight survives save/load  
crc_validation: payload byte flip rejected; truncated file rejected  
header_validation: inconsistent header counts rejected (header CRC + layout chain)  
failed_load_atomic: corrupted load leaves pre-seeded node/edge counts and edge weight unchanged  
post_load_propagation: fire sequence poodle→dog→animal matches pre-save trace  
variance/noise: deterministic run, no random seed used

## Trace Summary

```text
pre-save trace.log:
tick 0: poodle fired
tick 1: dog fired
tick 2: animal fired
path: poodle -> dog -> animal

post-load trace.log: identical fire sequence after ACTIVATE/TICK

persist.log:
snapshot=experiments/v09_persistence/roundtrip.nerva
crc=validated header+payload
load=ok
pre-save/post-load summaries: memory=1 schemas=1 adjacency_valid=1 after load
```

## Decision

Promote

## Notes

- Snapshot stores learned graph state (nodes, edges, names, memory, schemas, tick) only.
- Load parses into a staging buffer; live engine is untouched until full payload validates.
- Header CRC32 and sequential offset layout checked before payload CRC.
- Runtime queues and traces are cleared on successful load; adjacency rebuilt before use.
- `SAVE`/`LOAD` parser commands delegate to `nerva_persist_save` / `nerva_persist_load`.

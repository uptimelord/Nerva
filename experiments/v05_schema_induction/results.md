# Results: v0.5 Schema Induction

## Decision Rule

See [README.md](README.md).

## Setup

commit: 6ce885d  
date: 2026-06-27  
stage: v0.5  
graph: train A/B/C `kind_of`; held-out X/Y/Z with two-hop edges only  
parameters: `nerva_config_test()` (`schema_support_threshold=3`)  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_schema.c`:

- `test_schema_observe_builds_candidate`
- `test_schema_duplicate_observe_does_not_promote`
- `test_schema_promotes_at_threshold`
- `test_schema_apply_before_promote_fails`
- `test_schema_apply_requires_premise_edges`
- `test_schema_transitive_kind_of_reachable`
- `test_schema_inside_move_output_relation`
- `test_schema_experiment_artifacts`

Build: `.\build.bat`

Mutation artifact: `experiments/v05_schema_induction/mutation.log`  
Trace artifact: `experiments/v05_schema_induction/trace.log`

## Results

task_success_rate: 8/8 schema tests + 5/5 exception + 6/6 prediction + 7/7 learning + 4/4 trace + 8/8 event + 9/9 graph (47 total)  
schema_support_at_promote: 3 distinct examples  
schemas_promoted: 1  
schemas_applied: 1  
compression_check: raw_hop_cost=6, schema_edge_cost=1 at promote  
inferred_edge: X→Z `kind_of` created via `NERVA_MUT_CREATE_EDGE` / reason `SCHEMA_APPLY`  
variance/noise: deterministic run, no random seed used

## Trace Summary

```text
observe x3 distinct: kind_of+kind_of candidate support=3, raw_hop_cost=6 -> promoted
apply on X/Y/Z with premise edges: queues CREATE_EDGE X->Z kind_of
duplicate observe of same triple does not inflate support
```

## Decision

Promote

## Notes

- Schema keyed by `(rel_a, rel_b, rel_out)` relation pattern, not node names.
- Promotion requires compression benefit: one shortcut edge cheaper than stored two-hop examples.
- Apply requires live premise edges `a->b` and `b->c`; apply does not ignore middle node `b`.

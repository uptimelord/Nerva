# v0.5 Schema Induction

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- repeated two-hop `kind_of` paths create a schema candidate
- candidate promotes at support threshold with compression benefit
- distinct example triples increment support; duplicate triples do not
- promoted schema applies only when premise edges exist
- promoted schema applies on held-out triple via mutation queue (`NERVA_MUT_CREATE_EDGE`)
- inferred edge is reachable after `nerva_apply_mutations`
- active path visible in `trace.log` after held-out apply
- schema apply does not mutate graph during propagation tick

**Kill if:**

- schemas are hand-coded one-offs per demo graph (no relation-pattern key)
- promotion happens below support threshold
- schema apply bypasses mutation system
- schema scan walks all nodes every tick
- rule explosion beyond the two initial patterns (`kind_of` chain, `inside`+`moved_to`)

## Verdict

**Promote** — 8/8 schema tests pass; distinct-example support, compression gate, premise-edge apply, and held-out shortcut via mutation log. Evidence: [results.md](results.md), `mutation.log`, `trace.log`.

## Setup

stage: v0.5  
graph: train triples A→B→C (`kind_of`); held-out X→Y→Z for apply test  
parameters: default test config (`nerva_config_test`), lowered `schema_support_threshold`  
observe: call `nerva_schema_observe_triple` once per distinct training triple  
apply: call `nerva_schema_apply` on held-out triple after promotion (premise edges required)

## Commands

```text
TRAIN: observe 3 distinct A/B/C kind_of triples
PROMOTE: distinct support >= schema_support_threshold and shortcut cost < raw hop cost
APPLY: X/Y/Z with premise edges present -> queue X kind_of Z
APPLY_MUTATIONS
QUERY: reachable X kind_of Z
```

## Expected trace

```text
schema candidate support increments per observed triple
promotion logged when threshold crossed
mutation log: CREATE_EDGE for inferred shortcut (reason=SCHEMA_APPLY)
trace.log: X activation path through inferred X->Z edge
reachability true on held-out pair after apply
```

Run: `.\build.bat` (includes unit tests).

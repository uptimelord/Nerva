# v0.7 Fluid / Crystallized Routing

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- difficulty score combines novelty, contradiction, unresolved constraints, and confidence
- routine reachable poodle query stays crystallized (fluid activation rate below 5% over repeated runs)
- novel/unreachable query triggers fluid within one propagation batch
- contradictory penguin query triggers fluid after exception trace in the same batch
- adaptive fluid threshold rises after fluid use and decays toward baseline
- routing rebuilds adjacency when edges exist but CSR is stale
- routine/novel/contradiction cases saved to `trace.log` with fires, edges, and routing mode
- lateral inhibition reduces losing active rivals in fluid workspace
- fluid workspace does not mutate graph edges during propagation tick

**Kill if:**

- every query enters fluid regardless of difficulty
- routine known-path query always enters fluid
- fluid workspace exceeds four active nodes
- stale adjacency falsely classifies reachable routine queries as novel/fluid
- routing bypasses graph reachability checks for crystallized path
- fluid step pushes unbounded propagation or full-graph scan every tick

## Verdict

**Promote** — 9/9 routing tests pass; routine crystallized rate 100%, novel/contradiction fluid within batch, lateral inhibition, stale-adjacency heal, bounded workspace. Evidence: [results.md](results.md), `trace.log`, `routing.log`.

## Setup

stage: v0.7  
graph: routine poodle `kind_of` chain; novel unreachable pair; penguin blocker contradiction  
parameters: `nerva_config_test()` with lowered fluid threshold for deterministic gate  
observe: `nerva_routing_begin_query` before activate; tick until event queue drains

## Commands

```text
ROUTINE (x20): begin_query poodle->animal kind_of, activate poodle, tick 4
NOVEL: begin_query A->Z kind_of (no path), activate A, tick
CONTRADICT: penguin graph + blocker, begin_query penguin->fly, activate penguin, tick
CHECK: routine crystallized rate >= 95%; novel/contradict log fluid activation
```

## Expected trace

```text
routine: difficulty below threshold, crystallized_queries increment
novel: difficulty above threshold, fluid_activations increment
contradiction: exception trace raises difficulty, fluid within batch
trace.log: fires + edge traces + routing mode per case (routine/novel/contradiction)
routing.log records difficulty/threshold/mode per query
```

Run: `.\build.bat` (includes unit tests).

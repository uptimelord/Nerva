# v0.4 Exception / Blocker Handling

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- `bird` query reaches `fly` (fly fires or crosses threshold)
- `penguin` query keeps `fly` below threshold via explicit blocker
- blocker edge is created through the mutation queue (`BLOCK` / `nerva_queue_blocker_edge`)
- blocker propagation is visible in trace (`NERVA_TRACE_BLOCKER`)
- suppression is partial, not a hard zero wipe of unrelated state

**Kill if:**

- blocker bypasses mutation system (direct graph edit in tests only)
- penguin query still fires `fly` from the generalization path
- bird query fails because blocker is too broad
- exception handling scans all nodes every tick
- trace does not explain which blocker suppressed the path

## Verdict

**Promote** — 5/5 exception tests pass; bird path reaches `fly`, penguin path blocked, `mutation.log` records blocker apply, suppressed `bird->fly` trace carries `EXCEPTION|BLOCKER`. Evidence: [results.md](results.md), `trace.log`, `mutation.log`.

## Setup

stage: v0.4  
graph: bird/penguin/fly — `bird usually_has_property fly`, `penguin kind_of bird`, `BLOCK penguin fly`  
parameters: default test config (`nerva_config_test`)  
tick budgets: 4 ticks after activation

## Commands

```text
NODE bird
NODE penguin
NODE fly
EDGE bird usually_has_property fly
EDGE penguin kind_of bird
BLOCK penguin fly
APPLY_MUTATIONS
ACTIVATE bird
TICK 4
EXPECT: fly fired

ACTIVATE penguin
TICK 4
EXPECT: fly below threshold, blocker trace visible
```

## Expected trace

```text
bird path: bird fired -> bird->fly used-path -> fly fired
penguin path: penguin fired -> penguin->bird -> bird->fly
              penguin->fly blocker (inhibitory trace)
              fly stays below threshold
mutation log: ADD_BLOCKER_EDGE for penguin->fly
```

Run: `.\build.bat` (includes unit tests).

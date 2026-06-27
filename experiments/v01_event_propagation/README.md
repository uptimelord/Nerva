# v0.1 Event Propagation

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- events arrive in timestamp order
- nodes fire only after threshold crossing
- outgoing edges create new events
- fire trace shows the active path for the poodle demo
- queue overflow increments debug counters (not silent)

**Kill if:**

- propagation only works for hardcoded node names
- event order is nondeterministic without explanation
- queue overflow is silent
- reachability or propagation scans all nodes every tick

## Verdict

**Promote** — 8/8 event tests + 9/9 graph tests pass; poodle→dog→animal fires in order with trace artifact. Evidence: [results.md](results.md), `trace.log`.

## Setup

stage: v0.1  
graph: poodle -> dog -> animal (kind_of chain)  
parameters: default test config (`nerva_config_test`)  
tick budgets: 4 ticks after activation

## Commands

```text
NODE poodle
NODE dog
NODE animal
EDGE poodle kind_of dog
EDGE dog kind_of animal
ACTIVATE poodle
TICK 4
```

## Expected trace

```text
tick 0: poodle fired
tick 1: dog fired
tick 2: animal fired
path: poodle -> dog -> animal
```

Run: `.\build.bat` (includes unit tests).

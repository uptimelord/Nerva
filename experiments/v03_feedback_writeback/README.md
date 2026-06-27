# v0.3 Feedback Write-Back

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- correct feedback increases used edge strength
- wrong feedback decreases or gates used edge strength
- unused edges are not changed
- all changes are logged through the mutation system

**Kill if:**

- feedback changes unrelated edges
- edge weights change inside propagation
- repeated feedback causes runaway values without clipping

## Verdict

**Promote** — 7/7 learning tests pass; correct/wrong feedback mutates used edges only via deferred queue. Evidence: [results.md](results.md), `trace.log`, `mutation.log`.

## Setup

stage: v0.3  
graph: poodle -> dog -> animal (kind_of chain) + optional unused decoy edge  
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
FEEDBACK correct
APPLY_MUTATIONS
FEEDBACK wrong
APPLY_MUTATIONS
```

## Expected trace

```text
used-path traces for poodle->dog and dog->animal
mutation log: positive weight deltas on correct feedback
mutation log: negative weight deltas on wrong feedback; gate close after repeated wrong
unused decoy edge unchanged
```

Run: `.\build.bat` (includes unit tests).

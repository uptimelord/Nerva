# v0.2 Trace Buffer

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- used edges receive trace records
- trace decay matches expected tolerance
- trace output identifies the active path

**Kill if:**

- trace records are missing or unbounded
- trace decay requires scanning all nodes every tick in routine propagation
- feedback cannot find the used path

## Verdict

**Promote** — 4/4 trace tests pass; poodle two-hop path recorded with bounded ring and decay within tolerance. Evidence: [results.md](results.md), `trace.log`.

## Setup

stage: v0.2  
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
edge 0: poodle -> dog (used path)
edge 1: dog -> animal (used path)
trace_tag assigned per hop
pre/post decay within 5% per tick
```

Run: `.\build.bat` (includes unit tests).

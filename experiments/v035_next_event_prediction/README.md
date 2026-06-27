# v0.3.5 Next-Event Prediction

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- A alone produces B expected (pre-charge below fire threshold, B does not fire from prediction)
- actual B confirms prediction and strengthens the predicting edge through mutation log
- actual C misses prediction, weakens predicting edge, surprise trace visible
- expected events are tagged separately from actual propagation
- confirm/miss write-back uses mutation queue

**Kill if:**

- expectation equals propagation (B fires from prediction alone)
- predictions not visible in trace.log / prediction.log
- confirm/miss bypasses mutation system
- prediction scan walks all nodes every tick

## Verdict

**Promote** — 6/6 prediction tests pass; expect-then-verify loop with pre-charge, confirm/miss mutations, and window expiry. Evidence: [results.md](results.md), `prediction.log`.

## Setup

stage: v0.3.5  
graph: A -> B (kind_of), optional A -> C for miss test  
parameters: default test config (`nerva_config_test`)  
train: 3 propagation episodes (prediction mode off)  
test: prediction mode on, activate A only

## Commands

```text
NODE A
NODE B
EDGE A kind_of B
TRAIN: ACTIVATE A, TICK 4 (x3, propagation mode)
TEST: PREDICTION_MODE on, ACTIVATE A, TICK 2
EXPECT: A fired, B expected (pre-charged, not fired)
INJECT actual B -> CONFIRM -> APPLY_MUTATIONS
INJECT actual C -> MISS -> APPLY_MUTATIONS
```

## Expected trace

```text
tick 0: A actual fired
tick 1: B expected (pre-charge, flags EXPECTED)
confirm: B actual -> edge strengthened via mutation log
miss: C actual -> A->B weakened, SURPRISE trace
```

Run: `.\build.bat` (includes unit tests).

# v1.2.1.2 TagWorld Coverage Exploration

**Base:** `v1.2.1.1`  
**Status:** Repeat (strong) — see [results.md](results.md)

## Problem

v1.2.1.1 proves eligibility credit acquires PUSH policy when exploration observes push→block.
Seed 11 fails because that consequence is never sampled during learning — cold-start coverage,
not mechanism absence.

## Target

**Coverage exploration** during early learn phase: ensure each valid action is sampled enough to
observe intermediate consequences, then return to normal epsilon. Label honestly as coverage
exploration, not discovery magic.

```text
early phase:  higher epsilon or forced coverage to sample each valid action
later phase:  normal epsilon
eval phase:   epsilon = 0
```

Do **not** increase semantic scaffolding (no stronger pretrain, no new oracle chains, no push bias).

## Gate

See [gate.md](gate.md).

## Reproduce

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed 11 --learn-episodes 400 --fast --baseline
.\build\test_runner.exe
```

See [results.md](results.md) for full gate table.

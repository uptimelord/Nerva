# v1.2.1.2 TagWorld Coverage Exploration — Gate

**Status:** Draft (not run)

## Target claim

> Robust pure-feedback PUSH acquisition on held-out map G when coverage exploration guarantees
> intermediate consequence observations across all gate seeds.

## Promote if

- Every gate seed observes at least **N** push→block consequences during learning (N TBD, e.g. 1)
- PUSH selection rises after those observations
- Frozen eval beats random by >= 20 pp on G across seeds **1, 2, 3, 5, 7, 11**
- Dynamics pretrain caveat remains documented (not removed or expanded)
- Removing eligibility credit breaks the result (ablation)
- No oracle escape-chain `train_pair` during online learning

## Kill if

- Coverage exploration fixes seed 11 only by reintroducing oracle or push bias
- Eval improvement does not correlate with observed push→block credit events

## Not in scope

- Removing dynamics pretrain (separate future gate)
- Learning eligibility rules from data (rules remain engineered for v1.2.1.2)

## Command

TBD — will extend:

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed 11 --fast --baseline
```

with coverage-exploration flags once implemented.

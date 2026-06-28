# v1.2.1.2 TagWorld Coverage Exploration — Gate

**Status:** Repeat (strong) — see [results.md](results.md)

## Target claim

> Pure-feedback PUSH+RUN acquisition on held-out map G from zero-weight policy edges when coverage
> exploration guarantees intermediate consequence observations across all gate seeds.

## Promote if

- Every gate seed observes at least one push→block consequence during learning
- PUSH and RUN policy credit fires through the mutation queue
- Frozen eval beats random by >= 20 pp on G across seeds **1, 2, 3, 5, 7, 11**
- No oracle escape-chain `train_pair` during online learning
- Removing eligibility credit breaks the result (ablation)
- Dynamics pretrain skipped in `--pure-feedback` mode (documented, not hidden)

## Kill if

- Coverage exploration fixes seeds only by reintroducing oracle or push bias
- Eval improvement does not correlate with observed push→block credit events

## Not in scope

- Learning eligibility rules from data (rules remain engineered)
- Removing adapter events or graph topology (separate future gates)

## Command

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed <N> --learn-episodes 400 --fast --baseline
```

Optional: `--coverage-episodes N` (defaults to learn window in pure-feedback mode).

## Outcome

All gate seeds pass frozen eval at 100%. Classified **Repeat (strong)** because eligibility
rules, adapter events, coverage schedule, and action-validity masks remain engineered scaffolding.

# v1.2.1.3 TagWorld Coverage Ablation — Gate

**Status:** Draft (not run)

## Target claim

> Coverage exploration is a **bootstrap mechanism**, not the whole policy — at least one
> reduced-coverage setting still acquires zero-start push+run on held-out map G.

## Promote if

At least **one** reduced-coverage setting (not full 400-episode coverage):

- reaches **100%** frozen eval **or** ≥ random + 20 pp on map G across seeds **1, 2, 3, 5, 7, 11**
- uses **no** dynamics pretrain (`--pure-feedback`)
- uses **no** oracle `train_pair` during learn (`oracle_train_pair_rounds = 0`)
- frozen eval has `eval_avg_mutations_per_episode = 0`
- logs enough `push_block_observations` and `push_credit_count` to explain success on passing seeds
- diagnostic curve shows credit correlates with eval push/escape (not coverage alone)

## Kill if

- Every reduced-coverage variant fails all gate seeds while full coverage still passes (coverage is
  load-bearing, not bootstrap)
- Any variant passes only by reintroducing dynamics pretrain, oracle chains, or push bias
- Eval escape improves without corresponding push→block observations / push credit

## Not in scope

- Removing eligibility rules or adapter events
- Learning coverage schedule from data
- General tool use beyond TagWorld adapter events

## Commands

Baseline (v1.2.1.2 known-good):

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed <N> --learn-episodes 400 --coverage-episodes 400 --fast --baseline
```

Epsilon-only:

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed <N> --learn-episodes 400 --coverage-episodes 0 --fast --baseline
```

Until N push→block observations:

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed <N> --learn-episodes 400 --coverage-until-push-block <N> --fast --baseline
```

## Metrics to record per variant × seed

```text
coverage_episodes_resolved
coverage_until_push_block
train_push_block_observations
train_eligibility_push_block_strengthen
train_eligibility_run_escape_strengthen
train_coverage_explore_count
train_action_tie_zero_score_count
eval_escape_rate
eval_baseline_escape_rate
eval_action_push_doorway_count
eval_avg_mutations_per_episode
```

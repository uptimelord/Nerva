# v1.2.1 TagWorld Pure Feedback — Gate

**Status:** Not promoted (work in progress)

## Target claim

> Dynamics/world structure may be observed, but action credit comes from actual action traces and episode outcomes.

## Command

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map D --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11.

## Promote if

- No oracle `train_pair` chains run on escape (`--pure-feedback`)
- Oracle push→run escape >= 95% on the held-out pressure map
- Random valid-action baseline is between 20% and 80% (must leave headroom)
- Learned frozen eval >= random baseline + 20 pp
- Ablation of learned tool/chokepoint edges drops push or escape
- Trace shows credit from actual episode path: observed context → selected action → block state → outcome → mutation

## Held-out pressure map

Map D is retired as the primary eval pressure map for v1.2.1 because random baseline is saturated at 100%.
Use **map G**: held-out, novel topology, oracle wins, random baseline 20-80%.

> Gate requirement: random baseline must leave headroom. Promote only if random valid-action
> baseline is between 20% and 80%, oracle push→run escape >= 95%, and learned frozen eval
> >= random + 20 pp. This avoids another saturated-baseline promote.

## Unit tests

- `test_tagworld_pure_feedback_no_oracle_train_pairs`
- `test_tagworld_pure_feedback_records_action_traces`

## Not in scope for v1.2.1

- Map E spawn-variant stress ([#2](https://github.com/uptimelord/Nerva/issues/2))
- Retuning map D random baseline below 100% (optional follow-up)

# v1.2.1 TagWorld Pure Feedback — Gate

**Status:** Not promoted (work in progress)

## Target claim

> Dynamics/world structure may be observed, but action credit comes from actual action traces and episode outcomes.

## Command

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11. Map **G** is the held-out pressure map (oracle 100%,
random baseline ~0.62-0.72, not saturated). Map D is retired as primary eval pressure
because its random baseline is 100%.

## Promote if

- No oracle `train_pair` chains run on escape (`--pure-feedback`) — **met**
- Oracle push→run escape >= 95% on the held-out pressure map — **met (100%)**
- Random valid-action baseline is between 20% and 80% (must leave headroom) — **met (~0.62-0.72)**
- Trace shows credit from actual episode path: observed context → selected action → block state →
  outcome → mutation — **met (episode-local decision records + outcome-driven weight deltas)**
- Push selection rises during online learning — **NOT met (flat)**
- Learned frozen eval >= random baseline + 20 pp — **NOT met (eval 0%)**
- Ablation of learned tool/chokepoint edges drops push or escape — **NOT met (push already 0)**

## Held-out pressure map

Map D is retired as the primary eval pressure map for v1.2.1 because random baseline is saturated at 100%.
Use **map G**: held-out, distinct vertical-lane topology (not a clone of A-F), oracle escape 100%,
random escape baseline ~0.62-0.72 (measured seeds 1-11, 200 episodes). Pressure comes from the block
obstructing the sole short corridor: random play frequently times out, the oracle push→run does not.

> Gate requirement: random baseline must leave headroom. Promote only if random valid-action
> baseline is between 20% and 80%, oracle push→run escape >= 95%, and learned frozen eval
> >= random + 20 pp. This avoids another saturated-baseline promote.

## Unit tests

- `test_tagworld_pure_feedback_no_oracle_train_pairs`
- `test_tagworld_pure_feedback_records_action_traces`
- `test_pure_feedback_escape_strengthens_selected_push_trace`
- `test_pure_feedback_timeout_weakens_selected_wait_trace`
- `test_pure_feedback_trace_links_context_action_outcome_mutation`
- `test_pure_feedback_push_score_increases_after_successful_episode`
- `test_tagworld_map_g_headroom` (oracle escapes, random baseline in [0.20, 0.80])
- `test_tagworld_map_g_not_clone_of_a`

## Not in scope for v1.2.1

- Map E spawn-variant stress ([#2](https://github.com/uptimelord/Nerva/issues/2))
- Retuning map D random baseline below 100% (optional follow-up)

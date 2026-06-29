# v1.3 Honest TagWorld - Results

**Decision:** Repeat (bounded Stage 3 evidence committed; not promoted yet)
Date: 2026-06-29
Gate: [benchmarks/tagworld_honest/gate.md](../../benchmarks/tagworld_honest/gate.md)

## Why This Exists

v1.2 / v1.2.1.x TagWorld "generalization" was engineering progress, not clean no-handholding
learning evidence:

- legacy tool maps froze or neutered pursuit
- abstract tool events exposed chokepoint predicates directly
- pure-feedback G depended on engineered coverage/eligibility and, under stricter accounting, eval
  action-score fallback contamination

v1.3 adds `--honest`: live seeker, primitive perception, zero-weight primitive-feature -> action
edges, native outcome feedback, no forced coverage.

## Stage 1 - Honest World Calibration

Maps `H`, `H2`, and `H3` are the calibrated honest pursuit family. `H` and `H2` are train maps;
`H3` is held out.

Calibration check: 300 random episodes and 200 scripted push->run episodes per map.

| Map | Oracle push->run | Run-only | Random escape | Random CAUGHT | Stage 1 |
|-----|------------------|----------|---------------|---------------|---------|
| H   | 1.000 | 0.000 | 0.487 | 154/300 | pass |
| H2  | 1.000 | 0.000 | 0.487 | 154/300 | pass |
| H3  | 1.000 | 0.000 | 0.487 | 154/300 | pass |

These maps satisfy the task requirement: the tool is necessary enough that run-only loses, sufficient
when pushed into the chokepoint, and random play has non-saturated headroom.

## Stage 2 - Zero Starting Knowledge

Primitive perception contains seeker bearing, seeker distance, block adjacency, wall adjacency, and
`RUNNER_AT_SAFE`. The five legacy chokepoint symbols are not emitted under `--honest`.

Checks:

| Check | Result |
|-------|--------|
| Chokepoint symbols absent | pass (`test_tagworld_honest_no_chokepoint_symbols`) |
| Primitive policy edges zero after pretrain | pass |
| Zero-weight model equals random baseline on H | 0.4967 == 0.4967 |

## Stage 2.5 - Reset/Fallback Hardening

Root cause fixed: `tagworld_nerva_quiesce_engine()` reset `tick` and `last_fired_tick` between
episodes but did not reset `activation_count`. Core refractory logic treats `activation_count > 0`
as "has fired before", so resetting `tick` to 0 left tick-0 honest primitive features refractory in
later episodes. That made eval action scores disappear.

Fix: reset `activation_count` with the rest of episode-local node state.

Guardrails:

- `test_tagworld_honest_quiesce_resets_refractory_activation_count`
- `test_tagworld_honest_eval_zero_scores_are_not_random_fallback`
- honest eval all-zero ties are deterministic, counted as `action_tie_zero_score_count`, and do not
  increment `action_score_fallback_count`
- `--honest` disables forced coverage (`coverage_episodes_resolved=0`)

## Legacy v1.2 G Re-Audit

The old pure-feedback G gate is retained only as legacy evidence. Under strict fallback accounting it
is contaminated:

| seed | eval_escape | eval_random | eval_action_score_fallback_count |
|------|-------------|-------------|----------------------------------|
| 1    | 1.000 | 0.590 | 100 |
| 5    | 0.000 | 0.740 | 6400 |
| 11   | 1.000 | 0.690 | 100 |

The unit test is now `test_tagworld_legacy_pure_feedback_g_records_fallback_contamination`, not a
promote gate.

## Stage 3 - Honest Learning + Held-Out Generalization

Train maps: `H`, `H2`.
Held-out eval map: `H3`.

Command:

```powershell
.\build\nerva_tagworld.exe --generalization --honest --pure-feedback --mode action --eval-map H3 --learn-episodes 1000 --eval-episodes 200 --fast --baseline --seed <N>
```

Evidence:

| seed | train_escape | eval_escape | eval_random | eval_mut/ep | eval_fallbacks | eval PUSH_BLOCK | eval PUSH_TO_DOORWAY |
|------|--------------|-------------|-------------|-------------|----------------|-----------------|----------------------|
| 1    | 0.721 | 1.000 | 0.500 | 0.00 | 0 | 200 | 0 |
| 5    | 0.618 | 1.000 | 0.505 | 0.00 | 0 | 200 | 0 |
| 11   | 0.831 | 1.000 | 0.520 | 0.00 | 0 | 0 | 200 |
| 17   | 0.669 | 1.000 | 0.535 | 0.00 | 0 | 200 | 0 |
| 23   | 0.643 | 1.000 | 0.485 | 0.00 | 0 | 200 | 0 |

Additional checks:

- `tagworld_debug_oracle_online_train_pair_rounds() == 0`
- `coverage_episodes_resolved == 0`
- `test_tagworld_honest_generalization_ablation_drops_eval`
- `test_tagworld_honest_no_chokepoint_symbols`
- full `.\build.ps1` test suite passes

## Claim Discipline Draft

```text
Supported if promoted:
               Nerva learns a primitive-feature -> action policy from native outcome feedback on
               calibrated honest TagWorld maps H/H2 and transfers to held-out H3, with no oracle
               train_pair, no forced coverage, no eval mutations, and zero eval action-score
               fallbacks.
Not supported: broad world/model generalization; unsupervised edge discovery; learned motor plans;
               arbitrary tool use; modalities beyond this TagWorld substrate.
Evidence:      Stage 1/2/3 metrics above, reset/fallback guardrail tests, ablation test, full suite.
Residuals:     feature->action edge skeleton is predeclared at weight 0; PUSH_BLOCK and
               PUSH_BLOCK_TO_DOORWAY can both solve H3 when the block is adjacent in the doorway
               direction; H2 is a timing/safe-target variant, not a wholly new topology.
```

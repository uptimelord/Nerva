# TagWorld Honest Tool-Learning - Gate (v1.3)

**Status:** Repeat; evidence committed for review before promotion (2026-06-29)
**Base:** v1.2.1.x kept as historical record; honest mode is flag-gated with `--honest` / `cfg.honest`.

## Why This Exists

v1.2 / v1.2.1.x "tool generalization" is handholding, not clean learning evidence:

1. Legacy tool maps were tuned around frozen or neutered pursuit.
2. Abstract tool events emitted chokepoint predicates directly.
3. Pure-feedback G used engineered coverage/eligibility and is fallback-contaminated under strict
   accounting.

Honest mode removes those crutches. The model must use primitive perception, learn a
perception->action policy from native outcome feedback only, and evaluate frozen on a held-out
honest map.

## Design Constraints

- Keep macro tool-actions (`PUSH_BLOCK_TO_DOORWAY` remains a scripted one-cell push; the model learns
  when to use the available actions, not a motor plan).
- Live seeker on tool maps under `--honest`.
- Primitive perception only: seeker bearing, seeker distance, block adjacency, wall adjacency,
  `RUNNER_AT_SAFE`.
- Predeclared dense primitive-feature -> action edges at weight 0 are allowed because this engine
  cannot grow those edges from co-activation yet.
- No chokepoint symbols, no oracle train_pair during honest learning, no forced coverage, no eval
  mutation, no action-score fallback.

## Stage 1 - Revive the Task

**Promote if** each calibrated honest map H/H2/H3 satisfies:

- random baseline escape is non-saturated, 20-80%
- `CAUGHT` occurs under random play (pursuit is real)
- oracle push->run escapes >= 95%
- run-only escape is meaningfully below oracle escape (tool use is necessary)

**Kill if** no seeker-speed / tick-budget / geometry calibration makes the tool both necessary and
sufficient with the fixed macro-action set.

## Stage 2 - Primitive Perception, Zero Starting Knowledge

**Promote if:**

- the five legacy chokepoint symbols are not emitted in honest mode
- primitive-feature -> action edges start at weight 0
- zero-start honest policy matches random baseline before learning

**Kill if** any primitive feature is just a renamed chokepoint answer.

## Stage 3 - Honest Learning + Generalization

Current calibrated family:

- train: H, H2
- held-out eval: H3

**Promote if** train + frozen eval under `--honest --pure-feedback`, >=3 seeds:

- `eval_escape_rate >= eval_baseline + 0.20`
- `eval_avg_mutations_per_episode == 0`
- `eval_action_score_fallback_count == 0`
- `coverage_episodes_resolved == 0`
- `tagworld_debug_oracle_online_train_pair_rounds() == 0`
- ablation of learned primitive->action edges drops eval behavior
- trace/test evidence shows primitive perception -> action -> outcome with no chokepoint symbol

**Kill if** held-out performance requires re-adding chokepoint symbols, oracle train_pair, forced
coverage, or fallback action selection.

## Claim Discipline

```text
Supported:     bounded primitive-policy learning on calibrated honest TagWorld H/H2 -> H3.
Not supported: arbitrary tool use, unsupervised edge discovery, learned motor plans, broad modality
               generalization.
Evidence:      experiments/v1.3_tagworld_honest/results.md, unit tests, full build.
Residuals:     zero-weight edge skeleton remains predeclared; H2/H3 are calibrated variants, not a
               large world family.
```

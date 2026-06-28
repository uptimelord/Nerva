# v1.2.1.1 Results

**Decision:** Repeat

v1.2.1.1 implements eligibility credit and demonstrates partial online acquisition of the PUSH
policy on held-out pressure map G.

Seeds 1, 2, 3, 5, and 7 reach 100% frozen eval and select PUSH consistently after online learning.
Seed 11 remains at 0% because exploration never observes a successful push→block consequence, so no
eligibility credit is available to bootstrap context→PUSH edges.

This is a **strong Repeat**: the failure is now specific — **cold-start exploration coverage** — not
"learning mechanism absent." Eligibility credit can acquire push policy when exploration samples
push→block consequences.

## Supported claim

```text
Nerva can learn PUSH policy edges online from zero without oracle escape train_pair chains
when the relevant intermediate consequence is observed.
```

Evidence:

- Eligibility credit fires through the mutation queue (`eligibility_push_block_strengthen`,
  `eligibility_run_escape_strengthen`, `eligibility_wait_timeout_weaken`,
  `eligibility_run_fail_weaken`).
- PUSH context→action edges start at weight zero; ablation of learned push edges drops push or
  escape (seed 1 unit test).
- No oracle online `train_pair` chains (`test_tagworld_pure_feedback_no_oracle_train_pairs`).
- Training on maps A/B/C, frozen eval on held-out G.

## Not supported

```text
This does not prove robust pure-feedback tool-schema acquisition.
Dynamics and PATH_BLOCKED_BY_TOOL→RUN are still pretrained.
Eligibility rules are engineered.
Cold-start exploration failure can collapse performance.
```

Specifically **not** supported:

- Robust pure-feedback tool acquisition across all seeds
- Fully unsupervised tool-schema discovery
- No-handholding learning
- Run-after-block learned from scratch (`PATH_BLOCKED_BY_TOOL → RUN` is dynamics-pretrained via
  `train_pair`; eligibility reinforces it online)

## Evidence

### Gate seeds (required)

| seed | train push first/last window | push_block credit | run_escape credit | eval escape | eval random | eval push |
|------|------------------------------|-------------------|-------------------|-------------|-------------|-----------|
| 1    | 6 / 12                       | 94                | 828               | 1.000       | 0.590       | 100       |
| 5    | 5 / 13                       | 46                | 634               | 1.000       | 0.740       | 100       |
| 11   | 4 / 8                        | 0                 | 487               | 0.000       | 0.690       | 0         |

### Additional variance seeds

| seed | push_block credit | eval escape | eval random |
|------|-------------------|-------------|-------------|
| 2    | 112               | 1.000       | 0.700       |
| 3    | 110               | 1.000       | 0.640       |
| 7    | 68                | 1.000       | 0.600       |

## What this teaches

```text
one action is not the behavior
the behavior is a sequence
```

Outcome-only credit was too blunt. Eligibility credit closes part of the loop when intermediate
consequences are observed. Seed 11 shows the circuit works when current flows through it; one seed
never got current.

## Next gate (v1.2.1.2)

Fix **coverage exploration**, not stronger semantic scaffolding. See
[v1.2.1.2 coverage exploration gate](../v1.2.1.2_tagworld_coverage_exploration/gate.md).

## Discipline claim

```text
Supported:     online acquisition of context→PUSH policy edges from zero via eligibility credit
               when push→block is observed; no oracle escape-chain train_pair injection
Not supported: robust pure-feedback tool-schema acquisition; run-after-block from scratch;
               no-handholding learning
Evidence:      metrics above, unit tests, ablation test seed 1
Residuals:     seed 11 cold-start; dynamics pretrain; engineered eligibility rules;
               PATH_BLOCKED_BY_TOOL→RUN pretrained
```

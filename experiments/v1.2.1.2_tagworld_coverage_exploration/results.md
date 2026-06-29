# v1.2.1.2 TagWorld Coverage Exploration - Results

**Decision:** Archive / legacy engineering evidence (strict v1.3 re-audit)
Original status: Repeat (strong)

## Original Result

v1.2.1.2 added coverage exploration and removed dynamics pretrain for `--pure-feedback` mode. The
original run record reported that gate seeds 1, 2, 3, 5, 7, and 11 reached 100% frozen eval on
held-out map G with policy edges starting at zero.

Original mechanism claim:

```text
coverage exploration guarantees the circuit gets sampled,
eligibility attaches at consequence time,
zero-start push+run policy reaches 100% frozen eval on G across seeds
```

Original evidence fields:

- `tagworld_pretrain_for_config` skipped dynamics pretrain when `pure_feedback`
- push/run policy edges started at weight 0
- no oracle online `train_pair` during pure-feedback learn
- coverage exploration sampled push->block consequences

## Strict v1.3 Re-Audit

This result is not clean no-handholding learning evidence. The legacy G eval path can report nonzero
`eval_action_score_fallback_count`, and seed 5 collapses under current stricter accounting.

| seed | current eval_escape | current eval_random | current eval_action_score_fallback_count |
|------|---------------------|---------------------|------------------------------------------|
| 1    | 1.000 | 0.590 | 100 |
| 5    | 0.000 | 0.740 | 6400 |
| 11   | 1.000 | 0.690 | 100 |

The old `test_tagworld_pure_feedback_g_all_gate_seeds` is replaced by
`test_tagworld_legacy_pure_feedback_g_records_fallback_contamination`.

## Current Claim

```text
Supported:     legacy engineering progress: coverage exploration and eligibility credit can charge
               policy edges in the old abstract TagWorld-G setup.
Not supported: no-handholding learning; acquisition without coverage; unsupervised schema discovery;
               clean frozen eval on G under strict fallback accounting.
Evidence:      historical v1.2.1.2 metrics plus strict v1.3 re-audit fallback counts.
Residuals:     fallback-contaminated eval path; engineered eligibility rules; adapter events;
               coverage schedule; superseded by v1.3 honest H/H2 -> H3 results.
```

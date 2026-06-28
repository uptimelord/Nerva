# v1.1.3.1 Action Scoring Range Discipline — Gate

**Status:** Promote (`v1.1.3.1`, 2026-06-28)

## Unit tests

- `test_i32_saturating_add_clips`
- `test_tagworld_action_score_trace_lists_contributors`
- `test_tagworld_action_score_stable_after_10k_train_pairs`
- `test_tagworld_frozen_eval_no_action_score_fallback`
- `test_tagworld_action_score_long_learn_1k`
- All existing TagWorld / frozen eval tests still pass

## CLI gate

```powershell
.\build\nerva_tagworld.exe --map tool --mode action --online-frozen --seed 1 --fast --baseline
```

Repeat seeds 5 and 11. Require:

- `eval_escape_rate >= baseline + 0.20`
- `eval_action_score_fallback_count == 0`
- `eval_avg_mutations_per_episode == 0`

## Promote if

All checks above pass.

## Kill if

- Frozen eval escape regresses below v1.1.3 gate
- Any frozen eval seed reports `action_score_fallback_count > 0`
- 10k train-pair fuzz test fails (run score non-positive)

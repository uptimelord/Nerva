# v1.2.1.1 TagWorld Eligibility Credit — Gate

## Target claim

> Tool-action acquisition from observed intermediate consequences and episode outcomes, without oracle edge injection.

## Command

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11 (required gate seeds). Additional seeds 2, 3, 7 recorded for variance.

## Promote if

- No oracle `train_pair` chains run on escape (`--pure-feedback`) — **met**
- PUSH selection rises during online learning on G — **met** (seeds 1–7: last window > first window)
- Frozen eval >= random baseline + 20 pp on G — **met** (seeds 1–7); **NOT met** (seed 11)
- Ablation of learned push edges drops push or escape — **met** (unit test seed 1)
- Trace shows intermediate eligibility credit (push+block, run+escape, wait+timeout) — **met**
- Oracle push→run escape >= 95% on G — **met** (inherited from map G)
- Random baseline 20–80% on G — **met** (inherited from map G)

## Kill if

- Eligibility credit fires but eval never exceeds random on any seed after 200 learn episodes
- Oracle `train_pair` chains run during pure-feedback learning

## Unit tests

- `test_pure_feedback_escape_strengthens_selected_push_trace` (push+block intermediate credit)
- `test_pure_feedback_timeout_weakens_selected_wait_trace`
- `test_pure_feedback_push_score_increases_after_successful_episode`
- `test_eligibility_push_without_block_neutral_on_timeout`
- `test_eligibility_run_escape_strengthens_run_edge`
- `test_tagworld_pure_feedback_eligibility_ablation_reduces_push`
- `test_tagworld_pure_feedback_no_oracle_train_pairs`

## Residual (seed 11)

Seed 11 records zero `eligibility_push_block_strengthen` events: exploration never observes a
successful push→block consequence during learning, so push edges never receive intermediate credit
and frozen eval stays at 0%. Treated as exploration-variance stress, not mechanism absence.

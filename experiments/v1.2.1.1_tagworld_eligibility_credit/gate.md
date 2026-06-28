# v1.2.1.1 TagWorld Eligibility Credit — Gate

**Decision:** Repeat (strong — mechanism proven where exploration observes push→block)

## Target claim

> Partial online acquisition of PUSH policy via eligibility credit on observed intermediate
> consequences, without oracle escape-chain injection.

## Command

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11 (required gate seeds). Additional seeds 2, 3, 7 recorded for variance.

## Promote if

*(Not met — see v1.2.1.2 for next gate.)*

- No oracle `train_pair` chains run on escape (`--pure-feedback`) — **met**
- PUSH selection rises during online learning on G — **met** (seeds 1–7)
- Frozen eval >= random baseline + 20 pp on G — **met** (seeds 1–7); **NOT met** (seed 11)
- Ablation of learned push edges drops push or escape — **met** (unit test seed 1)
- Trace shows intermediate eligibility credit — **met**
- **Robust across all gate seeds** — **NOT met** (seed 11 at 0%)

## Kill if

- Eligibility credit fires but eval never exceeds random on any seed after 200 learn episodes
- Oracle `train_pair` chains run during pure-feedback learning

## Handholding caveats (documented, not hidden)

- Dynamics pretrain via `train_pair` (world structure, including `PATH_BLOCKED_BY_TOOL → RUN`)
- Eligibility rules encode which consequences matter (engineered credit assignment)
- 15% epsilon exploration during learn
- Abstract adapter event vocabulary given

## Unit tests

- `test_pure_feedback_escape_strengthens_selected_push_trace` (push+block intermediate credit)
- `test_pure_feedback_timeout_weakens_selected_wait_trace`
- `test_pure_feedback_push_score_increases_after_successful_episode`
- `test_eligibility_push_without_block_neutral_on_timeout`
- `test_eligibility_run_escape_strengthens_run_edge`
- `test_tagworld_pure_feedback_eligibility_ablation_reduces_push`
- `test_tagworld_pure_feedback_no_oracle_train_pairs`

## Seed 11 diagnosis

Zero `eligibility_push_block_strengthen` events: exploration never observes push→block during
learning. Failure mode is **cold-start exploration coverage**, not absent learning mechanism.

## Next

[v1.2.1.2 coverage exploration](../v1.2.1.2_tagworld_coverage_exploration/gate.md) — controlled
coverage schedule, not stronger semantic scaffolding.

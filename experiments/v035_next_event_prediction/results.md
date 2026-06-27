# Results: v0.3.5 Next-Event Prediction

## Decision Rule

See [README.md](README.md).

## Setup

commit: 577e987  
date: 2026-06-27  
stage: v0.3.5  
graph: A/B chain, A/C decoy added after train for miss test  
parameters: `nerva_config_test()` defaults  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_prediction.c`:

- `test_prediction_normal_propagation_when_mode_off`
- `test_prediction_emits_expected_not_fire`
- `test_prediction_confirm_strengthens_edge`
- `test_prediction_miss_weakens_edge`
- `test_prediction_window_expires_pending`
- `test_prediction_ab_experiment_artifacts`

Build: `.\build.bat`

Trace artifact: `experiments/v035_next_event_prediction/prediction.log`

## Results

task_success_rate: 6/6 prediction tests + 7/7 learning + 4/4 trace + 8/8 event + 9/9 graph (34 total)  
avg_ticks_per_query: 2 (prediction test tick budget)  
trace_records_count: expected trace with `NERVA_TRACE_EXPECTED`; confirm adds `PRED_CONFIRMED`  
edge_mutations_count: confirm/miss via `NERVA_REASON_PREDICTION_*`  
predictions_emitted: 1 on A-only test  
predictions_confirmed: 1 on B inject test  
predictions_missed: 1 on C inject test; 1 on window expiry test  
variance/noise: deterministic run, no random seed used

## Trace Summary

```text
tick 0: A actual fired
tick 1: B expected (pre-charged, not fired, flags EXPECTED)
confirm: A->B weight +ltp_delta via mutation log
miss: A->B weight +ltd_delta, flags PRED_MISSED|SURPRISE
artifact: prediction.log shows expected + confirmed lines
```

## Decision

Promote

## Notes

- Prediction mode suppresses outbound propagation; expectations are tagged separately from used-path facts.
- Edge must reach `prediction_min_stability` before next-hop prediction activates.
- `prediction_window_ticks` auto-misses stale pending expectations at tick start.
- Post-hoc feedback (v0.3) and prediction confirm/miss (v0.3.5) share the mutation queue with distinct reason codes.
- Decoy A→C edge for miss test is added after training so prediction targets B only.

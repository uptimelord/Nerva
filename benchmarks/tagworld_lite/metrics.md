# TagWorld-lite metrics

Benchmark-specific metrics for [gate.md](gate.md). Global reporting fields: [discipline/reporting.md](../../discipline/reporting.md).

## Observer mode

```text
escape_rate
caught
timeouts
avg_ticks_per_episode
avg_events_per_episode
avg_mutations_per_episode
total_mutations_applied
max_event_queue_depth
max_mutation_queue_depth
learned_edge_count
```

## Prediction mode

```text
prediction_confirm_count
prediction_miss_count
surprise_count
```

Plus observer fields where applicable.

## Action mode

```text
escape_rate
baseline_escape_rate          (--baseline CLI)
action_wait_count
action_run_count
action_push_block_count
action_push_doorway_count
```

Margin requirement: `escape_rate - baseline_escape_rate >= 0.20` on seeds 1, 5, 11.

## Stress

```text
episodes = 100000
wall_time_seconds
viz_frames = 0   (fast mode)
```

## Sanity checks

```text
total_mutations_applied must not overflow (use applied delta, not queue depth)
prediction_confirm_count > 0 on bulk prediction runs at gate
caught > 0 on open-doorway variants (observer)
```

## Evidence artifacts

```text
experiments/v1.1_tagworld_lite_promote/metrics.json
runs/tagworld/run_*.log
worlds/tagworld/viewer/  (replay)
```

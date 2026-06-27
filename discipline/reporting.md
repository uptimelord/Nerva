# Reporting requirements

Do not report naked success claims.

## Bad vs good

Bad:

```text
The new trace system is better.
```

Good:

```text
trace correctness = 20/20 cases
avg ticks/query = 7.4 +/- 0.6
avg events/query = 18.2 +/- 2.1
peak queue depth = 9
memory used = 1.8 MB
all successful cases include path traces
```

## Standard metrics

Include when applicable:

```text
task_success_rate
avg_ticks_per_query
avg_events_per_query
peak_queue_depth
memory_used_mb
edge_mutations_count
trace_records_count
contradiction_events_count
schema_promotions_count
fluid_workspace_activations
```

## Variance

Deterministic run:

```text
variance: deterministic run, no random seed used
```

Randomized inputs:

```text
result +/- run_variance
seed_count
seed_list
```

## Efficiency metrics

Nerva is judged by useful behavior per hardware cost.

Primary early metric:

```text
success_per_mb = task_success_rate / memory_used_mb
```

When event counts are available:

```text
success_per_event_mb = task_success_rate / (avg_events_per_query * memory_used_mb)
```

A result that is more accurate but explodes event count, queue depth, or memory use is not automatically better.

## results.md template

Every experiment should include `results.md`:

```text
# Results: <experiment name>

## Decision Rule
Promote if: ...
Kill if: ...

## Setup
commit:
date:
benchmark:
stage:
parameters:
seed:

## Commands
...

## Results
(task-specific metrics)

## Trace Summary
...

## Decision
Draft | Repeat | Promote | Kill | Archive

## Claims (required for Promote)
Supported:
Not supported:
Evidence:
Residuals:

## Notes
What surprised us?
What failed?
What should not be generalized?
```

Benchmark-specific metric names may be defined in `benchmarks/<name>/metrics.md`.

# TagWorld-lite gate

Global discipline: [DISCIPLINE.md](../../DISCIPLINE.md). Lifecycle: [experiment_lifecycle.md](../../discipline/experiment_lifecycle.md).

## Promote if
- Observer mode learns `BLOCK_AT_DOORWAY → PATH_BLOCKED`
- Prediction mode confirms/misses expected path events (distinct expected vs actual traces)
- Action mode improves escape rate over random valid-action baseline by ≥20 pp (seeds 1, 5, 11)
- Full trace explains at least one successful learned block episode
- Fast mode and viz/replay produce identical learning outcomes (same seed/config, except rendering)
- No hot-loop allocation or direct graph mutation outside mutation queue

## Kill if

- Simulator directly labels the correct action
- Runner wins because behavior is scripted rather than selected through Nerva action paths
- Expected events fire as actual events
- Visualization changes learning or simulator state
- Escape-rate improvement disappears under at least 3 fixed seeds
- Full trace cannot show why an action was selected

## Unit tests

`tests/test_tagworld.c` — corridor catch pressure, prediction confirm/mismatch, action vs random baseline.

## Config defaults

| Field | Value |
|-------|------:|
| grid | 7 |
| max_ticks | 64 |
| episodes (gate) | 1000 |
| seeds | 1, 5, 11 |

See `configs/default.json`.

# v1.1.1 Tool-Action Pressure

**Status:** OPEN (follows promoted [TagWorld-lite](../tagworld_lite/README.md))

## Goal

Force action mode to require `ACTION_PUSH_BLOCK_TO_DOORWAY` on some maps.

## Map family requirement

```text
RUN_TO_SAFE alone  → loses
WAIT               → loses
PUSH_BLOCK_TO_DOORWAY then RUN_TO_SAFE → wins
```

## Promote if

- Trained policy selects `PUSH_BLOCK_TO_DOORWAY` when `seeker_near + doorway_open + block_available`
- Trained escape rate beats random by ≥20 pp (seeds 1, 5, 11)
- Trace shows: `PUSH_BLOCK_TO_DOORWAY → BLOCK_AT_DOORWAY → PATH_BLOCKED → RUN_TO_SAFE → RUNNER_ESCAPED`

## Tests to add

- `test_tagworld_run_alone_loses_on_tool_map`
- `test_tagworld_push_doorway_then_run_wins`
- `test_tagworld_action_selects_push_when_required`

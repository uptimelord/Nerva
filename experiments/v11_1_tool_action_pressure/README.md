# v1.1.1 Tool-Action Pressure

**Status:** OPEN (follows promoted [v1.1 TagWorld-lite](../v11_tagworld_lite/README.md))

## Goal

Force action mode to require `ACTION_PUSH_BLOCK_TO_DOORWAY` on some maps — upgrading the claim from *learns action policy* to *learns tool-use action sequence*.

## Map family requirement

On at least one variant:

```text
RUN_TO_SAFE alone  → loses
WAIT               → loses
PUSH_BLOCK_TO_DOORWAY then RUN_TO_SAFE → wins
```

## Promote if

- Trained policy selects `PUSH_BLOCK_TO_DOORWAY` when `seeker_near + doorway_open + block_available`
- Trained `escape_rate` beats random by ≥20 pp (seeds 1, 5, 11)
- Trace shows full sequence:

```text
ACTION_PUSH_BLOCK_TO_DOORWAY
→ BLOCK_AT_DOORWAY
→ PATH_BLOCKED
→ ACTION_RUN_TO_SAFE
→ RUNNER_ESCAPED
```

## Tests to add

- `test_tagworld_run_alone_loses_on_tool_map`
- `test_tagworld_push_doorway_then_run_wins`
- `test_tagworld_action_selects_push_when_required` (bulk / multi-seed)

## Non-goals

- Smarter seeker AI
- Full physics or GUI beyond existing replay viewer
- Hardcoded winning action in the simulator

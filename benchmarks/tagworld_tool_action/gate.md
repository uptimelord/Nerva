# v1.1.1 Tool-Action Pressure — Gate

**Status:** PROMOTED (`v1.1.1`)

## Map

`--map tool` (tool_pressure)

## Required dynamics

| Policy | Outcome |
|--------|---------|
| `RUN_TO_SAFE` alone | lose (caught or timeout) |
| `WAIT` | lose |
| `PUSH_BLOCK_TO_DOORWAY` then `RUN_TO_SAFE` | win (escape) |

## Promote if

- Trained action mode selects `PUSH_BLOCK_TO_DOORWAY` at least once per run (`action_push_doorway_count > 0`)
- Trained escape rate beats random baseline by ≥20 pp on seeds **1, 5, 11** (100 episodes each)
- Trace/replay shows causal chain: push → block at doorway → path blocked → run → escape

## CLI gate command

```powershell
.\build\nerva_tagworld.exe --map tool --mode action --episodes 100 --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11.

## Unit tests

- `test_tagworld_run_alone_loses_on_tool_map`
- `test_tagworld_push_doorway_then_run_wins`
- `test_tagworld_action_selects_push_when_required`
- `test_tagworld_tool_action_beats_random_baseline`

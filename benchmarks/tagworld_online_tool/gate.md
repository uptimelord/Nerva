# v1.1.2 Online Tool Acquisition — Gate

**Status:** PROMOTED (`v1.1.2`)

## Map

`--map tool --online-tool`

## Required dynamics

Same as v1.1.1 tool-pressure map:

| Policy | Outcome |
|--------|---------|
| `RUN_TO_SAFE` alone | lose |
| `WAIT` | lose |
| `PUSH_BLOCK_TO_DOORWAY` then `RUN_TO_SAFE` | win |

## Promote if

- Dynamics-only pretrain leaves all action enable edges at weight zero
- 200 episodes, seeds **1, 5, 11**:
  - `episodes_with_push_last_window > episodes_with_push_first_window` (20-episode windows)
  - `escaped_last_window > escaped_first_window` (late escape rate improves vs early)
  - Seed **1**: full-run `escape_rate >= baseline_escape_rate` (random valid-action baseline)
- `action_push_doorway_count > 0` on each seed run
- No policy snapshot restore during eval (`--online-tool`)

## Kill if

- Push selection flat across windows on all seeds
- Late-window escape does not exceed early-window on all seeds
- Action pretrain edges nonzero at episode 0
- Seed 1 escape rate below random baseline

## Residual (not required for promote)

- ≥20 pp margin over random on all seeds (v1.1.1 pretrain bar; online exploration limits headroom)

## CLI gate command

```powershell
.\build\nerva_tagworld.exe --map tool --mode action --online-tool --episodes 200 --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11 (window metrics); seed 1 must beat or match random.

## Unit tests

- `test_tagworld_online_action_edges_zero_after_pretrain`
- `test_tagworld_online_push_increases_over_episodes`
- `test_tagworld_online_beats_random_baseline`

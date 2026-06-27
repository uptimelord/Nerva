# v1.1 TagWorld-lite

**Status: PROMOTED** (`v1.1.0`) — see [results.md](results.md).  
**Next:** [v1.1.1 Tool-Action Pressure](../v11_1_tool_action_pressure/README.md)

## Purpose
Test whether Nerva can learn that placing a movable block at a chokepoint changes the future path of a seeker and improves escape outcome.

## Non-goals
- No full physics.
- No GUI.
- No complex planner.
- No hardcoded winning strategy.
- No simulator-provided correct action.
- No graph mutation outside mutation queue.

## Decision Rule

Promote if:
- Observer mode learns BLOCK_AT_DOORWAY -> PATH_BLOCKED.
- Prediction mode confirms/misses expected path events using distinct expected vs actual traces.
- Action mode improves escape rate over random valid action baseline.
- Full trace can explain at least one successful learned block episode.
- Fast mode and viz mode produce identical learning outcomes when run with same seed/config except rendering.
- No hot-loop allocation or direct graph mutation is introduced.

Kill if:
- The simulator directly labels the correct action.
- The runner wins because behavior is scripted rather than selected through Nerva action paths.
- Expected events fire as actual events.
- Visualization changes learning or simulator state.
- Escape-rate improvement disappears under at least 3 fixed seeds.
- Full trace cannot show why the block action was selected.

## Setup

stage: v1.1  
runner: `build/nerva_tagworld.exe`  
config: `nerva_config_default()` (tests use `nerva_config_test()`)  
artifacts: `experiments/v11_tagworld_lite/sample_replay.log`, optional `prediction_trace.log` (gitignored)

Build:

```powershell
.\build.bat
```

Run examples:

```powershell
.\build\nerva_tagworld.exe --help
.\build\nerva_tagworld.exe --episodes 1 --viz
.\build\nerva_tagworld.exe --episodes 100000 --fast --seed 1
.\build\nerva_tagworld.exe --episodes 1000 --mode observer --seed 1
.\build\nerva_tagworld.exe --episodes 1000 --mode prediction --seed 1
.\build\nerva_tagworld.exe --episodes 1000 --mode action --seed 1 --baseline
.\build\nerva_tagworld.exe --replay experiments/v11_tagworld_lite/sample_replay.log --viz
```

Makefile:

```bash
make tagworld
./build/nerva_tagworld --episodes 1000 --mode observer --seed 1 --fast
```

## World rules

- 7x7 grid (configurable via `--grid`), border walls, doorway `D`, safe zone `Z`.
- Catch condition: seeker on same cell as runner, or adjacent (manhattan ≤ 1).
- Escape: runner reaches safe zone cell.
- Block push: runner must be adjacent; destination must be empty.
- Seeker: deterministic greedy Manhattan step toward runner (tie-break: +x, -x, +y, -y).
- Block at doorway blocks seeker from entering that cell; emits `PATH_BLOCKED`.
- Run-to-safe uses BFS pathfinding around walls/block.

Episode variants (from `seed ^ episode`):
- Variant 0: block parked away, doorway open, scripted wait → caught quickly.
- Variant 1: block pre-placed at doorway, scripted run-to-safe → escaped (top-route BFS).
- Variant 2: block adjacent to runner, scripted push-to-doorway then run → escaped.

Action mode selects among affordance-masked actions via Nerva node activation after state event propagation.

## Replay viewer (browser UI)

Record a game, then watch it in the browser with play/pause, scrubbing, and speed control.

```powershell
# Record 3 action-mode episodes and open the viewer
.\scripts\watch_tagworld.ps1

# Custom run
.\scripts\watch_tagworld.ps1 -Episodes 5 -Mode observer -Seed 42

# Manual: record replay then open viewer/index.html
.\build\nerva_tagworld.exe --episodes 3 --mode action --seed 1 --write-replay experiments/v11_tagworld_lite/viewer/demo.jsonl --fast
```

Viewer lives at `experiments/v11_tagworld_lite/viewer/index.html`. Load any `.jsonl` replay (drag-and-drop or file picker). Keyboard: Space = play/pause, ←/→ = step frames.

## Results

**Decision: Promote** — full metrics and evidence in [results.md](results.md).

## Expected trace (successful block episode)

```text
SEEKER_NEAR_RUNNER actual
DOORWAY_OPEN actual
ACTION_PUSH_BLOCK_TO_DOORWAY selected
BLOCK_AT_DOORWAY actual
PATH_BLOCKED expected/confirmed
RUNNER_ESCAPED actual
mutation: BLOCK_AT_DOORWAY -> PATH_BLOCKED +delta
mutation: PATH_BLOCKED -> RUNNER_ESCAPED +delta
action path strengthened
```

Action mode test (`test_tagworld_action_beats_random_baseline`) requires trained escape rate to beat random valid-action baseline by ≥20 percentage points on seeds 1, 5, and 11. Use `--baseline` on the CLI to track random valid-action escape rate at runtime.

Run gate: `.\build.bat` (98 tests including `tests/test_tagworld.c`).

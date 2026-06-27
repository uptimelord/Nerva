# TagWorld

Grid-world environment for Nerva: runner, seeker, movable block, doorway chokepoint, safe zone.

## Layout

```text
worlds/tagworld/
  tagworld.c / tagworld.h   — sim + Nerva bridge
  tagworld_viz.c / .h       — terminal + replay frame helpers
  viewer/index.html         — browser replay viewer
  maps/                     — future map families (v1.1.1+)
```

CLI entrypoint stays thin: `tools/tagworld_cli.c` → `build/nerva_tagworld.exe`.

## World rules

- 7×7 grid (configurable), border walls, doorway `D`, safe zone `Z`
- Catch: seeker on same cell as runner, or adjacent (manhattan ≤ 1)
- Escape: runner reaches safe zone
- Seeker: greedy Manhattan step (no pathfinding); runner run-to-safe uses BFS
- Block at doorway blocks seeker path; emits `PATH_BLOCKED` to Nerva

## Episode variants (`seed ^ episode`)

| Variant | Setup | Scripted outcome |
|--------:|-------|------------------|
| 0 | Open doorway, block away | Wait → caught |
| 1 | Block at doorway | Run → escaped |
| 2 | Block adjacent | Push to doorway, run → escaped |

## Replay viewer

```powershell
.\scripts\watch_tagworld.ps1
```

Or open `worlds/tagworld/viewer/index.html` and load a `.jsonl` replay.

## Benchmarks

Stable gates live under `benchmarks/tagworld_lite/`, not here.

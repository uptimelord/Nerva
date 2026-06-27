# TagWorld-lite benchmark

Stable task definition and promote/kill gate for the corridor TagWorld environment.

- **World:** `worlds/tagworld/`
- **Runner:** `build/nerva_tagworld.exe`
- **Sample replay:** [sample_replay.log](sample_replay.log)
- **Viewer:** `worlds/tagworld/viewer/index.html`

See [gate.md](gate.md) for promote/kill criteria and [metrics.md](metrics.md) for benchmark-specific fields.

## Run gate

```powershell
.\build.bat                                    # 98 tests
.\build\nerva_tagworld.exe --episodes 1000 --mode observer --seed 1 --fast
.\build\nerva_tagworld.exe --episodes 1000 --mode prediction --seed 1 --fast
.\build\nerva_tagworld.exe --episodes 1000 --mode action --seed 1 --fast --baseline
.\build\nerva_tagworld.exe --episodes 100 --mode action --seed 5 --fast --baseline
.\build\nerva_tagworld.exe --episodes 100 --mode action --seed 11 --fast --baseline
```

Raw logs → `runs/tagworld/` (gitignored).

## Promoted result

[v1.1 TagWorld-lite promote snapshot](../../experiments/v1.1_tagworld_lite_promote/README.md) — tag `v1.1.0`.

## Next gate

[v1.1.1 Tool-Action Pressure](../tagworld_tool_action/README.md)

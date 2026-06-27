# Results — v1.1.1 Tool-Action Pressure promote

Date: 2026-06-28  
Machine: Windows 10, gcc WinLibs UCRT  
Tag: `v1.1.1`  
Benchmark: [benchmarks/tagworld_tool_action/](../../benchmarks/tagworld_tool_action/README.md)  
Raw logs: `runs/tagworld/` (gitignored)

## Decision Rule

See [benchmarks/tagworld_tool_action/gate.md](../../benchmarks/tagworld_tool_action/gate.md).

## Commands

See [commands.sh](commands.sh) or:

```powershell
.\build.ps1
.\build\nerva_tagworld.exe --map tool --mode action --episodes 100 --seed 1 --fast --baseline
.\build\nerva_tagworld.exe --map tool --mode action --episodes 100 --seed 5 --fast --baseline
.\build\nerva_tagworld.exe --map tool --mode action --episodes 100 --seed 11 --fast --baseline
```

## Results

See [metrics.json](metrics.json) for machine-readable summary.

| Run | Key result |
|-----|------------|
| Action seed 1 | 100% escape vs 59% random (+41 pp); push 100/100 |
| Action seed 5 | 100% escape vs 74% random (+26 pp); push 100/100 |
| Action seed 11 | 100% escape vs 69% random (+31 pp); push 100/100 |
| Unit tests | 102/102 pass |

## Trace

See [trace_replay.jsonl](trace_replay.jsonl) — tick 0 push doorway, then run-to-safe, escape.

## Decision

**Promote — v1.1.1**

## Claims

**Supported:**

- The tool map creates real action pressure: run-alone loses, push-then-run wins.
- Action selection does **not** use a hardcoded map-specific policy branch in `tagworld_nerva_select_action`.
- Push selection is driven by learned graph edge weights from active context nodes (`DOORWAY_OPEN`, `PATH_OPEN`, `SEEKER_NEAR_RUNNER` → `ACTION_PUSH_BLOCK_TO_DOORWAY`).
- Trained policy beats random valid-action baseline by ≥20 pp on seeds 1, 5, and 11.
- Benchmark evaluates a **frozen post-pretrain policy snapshot** restored each episode (stable multi-episode eval).

**Not supported:**

- Online tool-use discovery across episodes (outcome-driven acquisition without action-context pretrain).
- Generalization to unseen tool maps.
- Reward-only discovery without action-context pretraining.

**Evidence:**

- [metrics.json](metrics.json), [trace_replay.jsonl](trace_replay.jsonl)
- Gate: [benchmarks/tagworld_tool_action/gate.md](../../benchmarks/tagworld_tool_action/gate.md)
- Tests: `test_tagworld_tool_action_beats_random_baseline`, `test_tagworld_action_selects_push_when_required`

**Next gate:**

- v1.1.2 Online Tool Acquisition — no direct `DOORWAY_OPEN → ACTION_PUSH` pretrain; outcome feedback must raise push selection over episodes.

# Results — v1.1.2 Online Tool Acquisition promote

Date: 2026-06-28  
Machine: Windows 10, gcc WinLibs UCRT  
Tag: `v1.1.2`  
Benchmark: [benchmarks/tagworld_online_tool/](../../benchmarks/tagworld_online_tool/README.md)  
Raw logs: `runs/tagworld/` (gitignored)

## Decision Rule

See [benchmarks/tagworld_online_tool/gate.md](../../benchmarks/tagworld_online_tool/gate.md).

## Commands

See [commands.sh](commands.sh) or:

```powershell
.\build.ps1
.\build\nerva_tagworld.exe --map tool --mode action --online-tool --episodes 200 --seed 1 --fast --baseline
.\build\nerva_tagworld.exe --map tool --mode action --online-tool --episodes 200 --seed 5 --fast --baseline
.\build\nerva_tagworld.exe --map tool --mode action --online-tool --episodes 200 --seed 11 --fast --baseline
```

## Results

See [metrics.json](metrics.json) for machine-readable summary.

| Run | Key result |
|-----|------------|
| Online seed 1 | 68.0% escape vs 62.5% random (+5.5 pp); push eps 11→18/20; escape eps 9→10/20 |
| Online seed 5 | 67.5% escape vs 71.5% random (−4.0 pp); push eps 10→19/20; escape eps 0→14/20 |
| Online seed 11 | 68.0% escape vs 66.0% random (+2.0 pp); push eps 12→18/20; escape eps 3→13/20 |
| Unit tests | 105/105 pass |

## Trace

See [trace_replay.jsonl](trace_replay.jsonl) — one online-mode episode (exploration + learned edge scoring).

## Decision

**Promote — v1.1.2**

## Claims

**Supported:**

- Dynamics-only pretrain leaves action enable edges at weight zero before episode 1.
- Push doorway selection rises from first to last 20-episode window on all gate seeds.
- Late-window escape rate exceeds early-window escape rate on all gate seeds.
- Seed 1 full-run escape rate beats random valid-action baseline (68.0% vs 62.5%).
- No frozen post-pretrain policy snapshot restore during eval (`--online-tool`).
- No hardcoded map-specific push branch in `tagworld_nerva_select_action`.

**Not supported:**

- ≥20 pp margin over random on all seeds (online exploration + ongoing learning limit headroom).
- v1.1.1-level 100% escape without action-context pretrain.
- Generalization to unseen tool maps.

**Evidence:**

- [metrics.json](metrics.json), [trace_replay.jsonl](trace_replay.jsonl)
- Gate: [benchmarks/tagworld_online_tool/gate.md](../../benchmarks/tagworld_online_tool/gate.md)
- Tests: `test_tagworld_online_*`

**Residuals:**

- Seed 5 full-run escape still below random; late-window learning curve is strong (0→14 escapes).
- Push spam after block placement remains a learning pressure (high `action_push_doorway_count`).
- Next: exploration schedule tuning, run-after-block reinforcement, long-run stability.

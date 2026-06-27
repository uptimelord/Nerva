# Results — v1.1 TagWorld-lite promote

Date: 2026-06-28  
Machine: Windows 10, gcc WinLibs UCRT  
Tag: `v1.1.0`  
Benchmark: [benchmarks/tagworld_lite/](../../benchmarks/tagworld_lite/README.md)  
Raw logs: `runs/tagworld/` (gitignored)

## Decision Rule

See [benchmarks/tagworld_lite/gate.md](../../benchmarks/tagworld_lite/gate.md).

## Commands

See [commands.sh](commands.sh) or:

```powershell
.\build.bat
.\build\nerva_tagworld.exe --episodes 1000 --mode observer --seed 1 --fast
.\build\nerva_tagworld.exe --episodes 1000 --mode prediction --seed 1 --fast
.\build\nerva_tagworld.exe --episodes 1000 --mode action --seed 1 --fast --baseline
.\build\nerva_tagworld.exe --episodes 100 --mode action --seed 5 --fast --baseline
.\build\nerva_tagworld.exe --episodes 100 --mode action --seed 11 --fast --baseline
.\build\nerva_tagworld.exe --episodes 100000 --mode observer --seed 1 --fast
```

## Results

See [metrics.json](metrics.json) for machine-readable summary.

| Run | Key result |
|-----|------------|
| Observer 1k | 67% escape, 333 caught, 0 timeouts |
| Prediction 1k | 5002 confirms, 1001 misses |
| Action 1k vs random | 100% vs 39.3% (+60.7 pp) |
| Action seeds 5/11 | 100% vs 39% / 30% |
| Observer 100k | ~14 s, 33335 caught |

## Trace

See [trace_excerpt.log](trace_excerpt.log).

## Decision

**Promote — v1.1.0**

## Claims

**Supported:**

- Nerva can operate inside TagWorld-lite: observe world events, predict futures, confirm/miss, learn from outcomes
- Closed loop: world event → expectation → actual → confirm/miss → mutation → changed behavior
- Trained action beats random valid-action baseline by ≥20 pp on seeds 1, 5, and 11
- Block-at-doorway → PATH_BLOCKED causal path is learned, traced, and replayable
- 98/98 unit tests; metrics sane (no mutation counter overflow)

**Not supported:**

- Nerva independently discovered block-pushing tool use in bulk action mode
- Generalized tool use across arbitrary grid layouts
- Strategy discovery without environmental shaping (variants, pretrain, corridor map)

**Evidence:**

- [metrics.json](metrics.json), [trace_excerpt.log](trace_excerpt.log), tag `v1.1.0`
- Gate: [benchmarks/tagworld_lite/gate.md](../../benchmarks/tagworld_lite/gate.md)

**Residuals:**

- Bulk action favors `RUN_TO_SAFE`; `action_push_doorway_count=0` at scale
- Next gate: [benchmarks/tagworld_tool_action/](../../benchmarks/tagworld_tool_action/README.md) (v1.1.1)

## Closed loop

```text
world event → expectation → actual outcome → confirm/miss → mutation → changed future behavior
```

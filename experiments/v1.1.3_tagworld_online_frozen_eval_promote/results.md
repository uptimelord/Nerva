# Results — v1.1.3 Online Frozen Eval promote

Date: 2026-06-28  
Machine: Windows 10, gcc WinLibs UCRT  
Tag: `v1.1.3`  
Benchmark: [benchmarks/tagworld_online_frozen_eval/](../../benchmarks/tagworld_online_frozen_eval/README.md)  
Raw logs: `runs/tagworld/` (gitignored)

## Decision Rule

See [benchmarks/tagworld_online_frozen_eval/gate.md](../../benchmarks/tagworld_online_frozen_eval/gate.md).

## Commands

See [commands.sh](commands.sh) or:

```powershell
.\build.ps1
.\build\nerva_tagworld.exe --map tool --mode action --online-frozen --seed 1 --fast --baseline
.\build\nerva_tagworld.exe --map tool --mode action --online-frozen --seed 5 --fast --baseline
.\build\nerva_tagworld.exe --map tool --mode action --online-frozen --seed 11 --fast --baseline
```

## Results

See [metrics.json](metrics.json) for machine-readable summary.

| Run | Learn escape | Eval escape | Random baseline | Eval margin |
|-----|--------------|-------------|-----------------|-------------|
| Seed 1 | 86.0% | 100.0% | 59.0% | +41.0 pp |
| Seed 5 | 78.5% | 100.0% | 74.0% | +26.0 pp |
| Seed 11 | 84.0% | 100.0% | 69.0% | +31.0 pp |
| Unit tests | 110/110 pass | | | |

Learn phase push selection rises on all seeds (20-episode windows). Eval phase applies zero mutations per episode. Ablation test passes.

## Trace

See [trace_replay.jsonl](trace_replay.jsonl) — seed 1 learn + eval replay.

## Decision

**Promote — v1.1.3**

## Claims

**Supported:**

```text
Nerva v1.1.3 demonstrates online acquisition of a tool-action policy in TagWorld followed by frozen-policy evaluation. After dynamics prior and online feedback, learned push/run behavior transfers into a no-mutation evaluation phase and beats random baseline across seeds 1, 5, and 11.
```

- Phase A (200 ep, ε=15%): push selection and escape improve from first to last 20-episode window.
- Phase B (100 ep, ε=0%): frozen eval escape ≥ baseline + 20 pp on seeds 1, 5, 11.
- Phase B: `avg_mutations_per_episode == 0`.
- Ablation of learned push enable edges reduces push usage or escape (unit test).
- No hardcoded map-specific push branch in `tagworld_nerva_select_action`.

**Not supported:**

```text
Not supported:
- generalized tool use across unseen maps
- transfer to new object types/chokepoints
- text-domain tool/action reasoning
- broad self-play/autocurriculum
```

- 100% escape during online learn without exploration (learn rates 78–86%, not 100%).
- Transfer of learned weights across seeds without re-learning.

**Implementation notes (discipline, not hardcoded policy):**

- Dynamics pretrain teaches `path_blocked→run_safe`; push enable edges start at zero and are acquired online.
- Action scoring uses registered TagWorld policy edges and edge weight only (stability bonus removed to avoid int16 overflow after long online training).
- Per-tick event queue reset and action-node potential reset before selection prevent stale activations from blocking run-after-push.

**Evidence:**

- [metrics.json](metrics.json), [trace_replay.jsonl](trace_replay.jsonl)
- Gate: [benchmarks/tagworld_online_frozen_eval/gate.md](../../benchmarks/tagworld_online_frozen_eval/gate.md)
- Tests: `test_tagworld_online_frozen_*`, `test_tagworld_post_push_selects_run_after_dynamics_pretrain`

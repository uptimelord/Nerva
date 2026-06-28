# v1.1.3 Online Frozen Eval — promote snapshot

**Lifecycle:** Promote  
**Decision:** Promote (`v1.1.3`)  
**Date:** 2026-06-28  
**Benchmark:** [benchmarks/tagworld_online_frozen_eval](../../benchmarks/tagworld_online_frozen_eval/README.md)  
**World:** [worlds/tagworld](../../worlds/tagworld/README.md)  
**Prior:** [experiments/v1.1.2_tagworld_online_tool_promote](../v1.1.2_tagworld_online_tool_promote/README.md)

Evidence bundle for two-phase online learning with frozen greedy evaluation on the tool-pressure map. Raw run logs live in `runs/tagworld/` (gitignored).

| Artifact | Description |
|----------|-------------|
| [results.md](results.md) | Full metrics and claims |
| [metrics.json](metrics.json) | Key numbers (machine-readable) |
| [commands.sh](commands.sh) | Reproduce gate commands |
| [trace_replay.jsonl](trace_replay.jsonl) | Learn + eval replay (seed 1) |

## Supported claim

```text
Nerva v1.1.3 demonstrates online acquisition of a tool-action policy in TagWorld followed by frozen-policy evaluation. After dynamics prior and online feedback, learned push/run behavior transfers into a no-mutation evaluation phase and beats random baseline across seeds 1, 5, and 11.
```

## Not supported yet

```text
- generalized tool use across unseen maps
- transfer to new object types/chokepoints
- text-domain tool/action reasoning
- broad self-play/autocurriculum
```

## Next

Longer learn schedules; cross-seed weight transfer; prune spurious mutation edges at snap time.

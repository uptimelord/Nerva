# v1.1.2 Online Tool Acquisition — promote snapshot

**Lifecycle:** Promote  
**Decision:** Promote (`v1.1.2`)  
**Date:** 2026-06-28  
**Benchmark:** [benchmarks/tagworld_online_tool](../../benchmarks/tagworld_online_tool/README.md)  
**World:** [worlds/tagworld](../../worlds/tagworld/README.md)  
**Prior:** [experiments/v1.1.1_tagworld_tool_action_promote](../v1.1.1_tagworld_tool_action_promote/README.md)

Evidence bundle for outcome-driven tool-action acquisition on the tool-pressure map. Raw run logs live in `runs/tagworld/` (gitignored).

| Artifact | Description |
|----------|-------------|
| [results.md](results.md) | Full metrics and claims |
| [metrics.json](metrics.json) | Key numbers (machine-readable) |
| [commands.sh](commands.sh) | Reproduce gate commands |
| [trace_replay.jsonl](trace_replay.jsonl) | One-episode online action replay |

## Supported claim

```text
Nerva can acquire tool-action selection online from episode outcome feedback:
dynamics-only pretrain (no action-context links), epsilon exploration,
escape/caught feedback strengthens context→push and path_blocked→run edges
without frozen policy snapshot restore.
```

## Next

Further gates: ≥20 pp margin over random on all seeds; reduced exploration decay; anti-forgetting for long online runs.

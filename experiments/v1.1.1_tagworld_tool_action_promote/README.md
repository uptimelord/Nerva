# v1.1.1 Tool-Action Pressure — promote snapshot

**Lifecycle:** Promote  
**Decision:** Promote (`v1.1.1`)  
**Date:** 2026-06-28  
**Benchmark:** [benchmarks/tagworld_tool_action](../../benchmarks/tagworld_tool_action/README.md)  
**World:** [worlds/tagworld](../../worlds/tagworld/README.md)  
**Prior:** [experiments/v1.1_tagworld_lite_promote](../v1.1_tagworld_lite_promote/README.md)

Evidence bundle for pretrain-learned tool-action selection on the tool-pressure map. Raw run logs live in `runs/tagworld/` (gitignored).

| Artifact | Description |
|----------|-------------|
| [results.md](results.md) | Full metrics and claims |
| [metrics.json](metrics.json) | Key numbers (machine-readable) |
| [commands.sh](commands.sh) | Reproduce gate commands |
| [trace_replay.jsonl](trace_replay.jsonl) | One-episode action replay (push → run → escape) |

## Supported claim

```text
Nerva can learn a tool-action policy from graph training:
doorway/path/seeker context → push action,
then select that action by scoring learned graph edges
(not a map-specific hardcoded policy branch).
```

## Next

[v1.1.2 Online Tool Acquisition](../../benchmarks/tagworld_online_tool/README.md) — outcome-driven tool acquisition without direct action pretrain.

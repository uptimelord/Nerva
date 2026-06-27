# v1.1 TagWorld-lite — promote snapshot

**Lifecycle:** Promote  
**Decision:** Promote (`v1.1.0`)  
**Date:** 2026-06-28  
**Benchmark:** [benchmarks/tagworld_lite](../../benchmarks/tagworld_lite/README.md)  
**World:** [worlds/tagworld](../../worlds/tagworld/README.md)  
**Discipline:** [DISCIPLINE.md](../../DISCIPLINE.md)

Evidence bundle for the first closed-loop TagWorld result. Raw run logs live in `runs/tagworld/` (gitignored).

| Artifact | Description |
|----------|-------------|
| [results.md](results.md) | Full metrics and claims |
| [metrics.json](metrics.json) | Key numbers (machine-readable) |
| [commands.sh](commands.sh) | Reproduce commands |
| [trace_excerpt.log](trace_excerpt.log) | Action-mode block escape trace |

## Closed loop proved

```text
world event → expectation → actual → confirm/miss → mutation → changed behavior
```

## Next

[v1.1.1 Tool-Action Pressure](../../benchmarks/tagworld_tool_action/README.md)

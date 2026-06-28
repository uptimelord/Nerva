# v1.2 TagWorld Generalization (RC)

**Base:** `v1.1.3.1` (action-score hardening)  
**Decision:** Repeat — harness landed; final promote blocked  
**Tag:** none (do not tag `v1.2` final yet)

**Question:** Did Nerva learn coordinates of one tool map, or a reusable chokepoint/tool schema?

**Approach:** Train on maps A/B/C with abstract adapter events; frozen eval on held-out D/E/F.

## Classification

```text
v1.2 current (this commit):
  abstract adapter + supervised transfer harness

v1.2 final (blocked):
  abstract transfer across genuinely held-out topology

v1.2.1 (next):
  pure feedback / less supervised tool-schema acquisition
```

## Reproduce

```powershell
./build.ps1
./experiments/v1.2_tagworld_generalization/commands.sh
```

## Status

See [results.md](results.md) and [benchmarks/tagworld_generalization/gate.md](../../benchmarks/tagworld_generalization/gate.md).

# v1.2 TagWorld Generalization

**Base:** `v1.1.3.1`  
**Decision:** Promote (`v1.2` final)  
**RC:** `74f1e4f`

## Claim

Supervised abstract tool-schema transfer across maps via adapter-emitted chokepoint events + frozen eval.

**Not claimed:** pure feedback, zero-shot invention, generalization beyond adapter events.

## Reproduce

```powershell
./build.ps1
./experiments/v1.2_tagworld_generalization/commands.sh
```

Invariance: frozen eval on `--eval-map D'` must match `--eval-map D`.

## Status

See [results.md](results.md) and [benchmarks/tagworld_generalization/gate.md](../../benchmarks/tagworld_generalization/gate.md).

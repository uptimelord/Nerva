# v1.2 TagWorld Generalization

**Status:** Repeat (RC harness)

Supervised abstract tool-schema transfer: train on maps A/B/C with adapter-emitted chokepoint events; frozen eval on held-out D/E/F.

Not final promote until held-out topology and invariance gaps are closed. See [gate.md](gate.md).

## CLI

```powershell
.\build\nerva_tagworld.exe --generalization --mode action --eval-map D --seed 1 --fast --baseline
```

Defaults: 200 rotating train episodes (A/B/C), 100 frozen eval episodes on map D.

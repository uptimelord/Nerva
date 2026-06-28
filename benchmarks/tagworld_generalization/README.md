# v1.2 TagWorld Generalization

Supervised abstract tool-schema transfer: train on maps A/B/C with adapter-emitted chokepoint events; frozen eval on held-out D/E/F.

## Supported claim

Supervised abstract tool-schema transfer across maps via adapter events + frozen eval.

## Not supported

- Pure feedback acquisition
- Zero-shot tool invention
- Generalization beyond adapter-emitted chokepoint events

See [gate.md](gate.md).

## CLI

```powershell
.\build\nerva_tagworld.exe --generalization --mode action --eval-map D --seed 1 --fast --baseline
```

Defaults: 200 rotating train episodes (A/B/C), 100 frozen eval episodes on map D.

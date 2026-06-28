# v1.2.1 TagWorld Pure Feedback

**Base:** `v1.2`  
**Status:** In progress  
**GitHub:** [#3](https://github.com/uptimelord/Nerva/issues/3)

## Target

Dynamics and world structure may be observed via adapter events, but **action credit must come from actual action traces and episode outcomes** — no oracle `train_pair` chains on escape.

## Claim (when promoted)

Online tool acquisition from observed context → selected action → block state → outcome → mutation, without injecting the desired edge chain at episode end.

## Reproduce

```powershell
./build.ps1
./experiments/v1.2.1_tagworld_pure_feedback/commands.sh
```

Use `--pure-feedback` with `--generalization` (or `--online-tool` / `--online-frozen`).

## Promote gate

See [gate.md](gate.md).

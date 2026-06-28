# v1.1.3.1 Action Scoring Range Discipline

**Status:** Promote (`v1.1.3.1`, 2026-06-28)

## Goal

Prevent action-score overflow and silent fallback collapse during long online training and frozen eval.

## Context

v1.1.3 frozen eval hit 0% escape when `stability << 2` overflowed int16 action scores, causing WAIT to beat negative RUN scores. This benchmark locks the fix.

## Requirements

- Action score accumulation uses `int32_t` with explicit saturation (`nerva_i32_saturating_add`)
- Action selection sums registered TagWorld policy edges only (weight, no stability shift)
- Non-positive winning scores increment `action_score_fallback_count` (no silent secondary fallback path)
- `action_score_fallback_count` increments only when any valid action edge score is negative (overflow/invalid)
- Optional `--action-score-trace` prints contributing edges on fallback
- Unit tests: saturation, 10k train-pair fuzz, 1k-episode learn + frozen eval

## Gate

```powershell
.\build.ps1
.\build\test_runner.exe
.\build\nerva_tagworld.exe --map tool --mode action --online-frozen --seed 1 --fast --baseline
```

Promote if all tests pass and frozen eval reports `eval_action_score_fallback_count=0` on seeds 1, 5, 11.

## Prior

- [v1.1.3 Online Frozen Eval](../tagworld_online_frozen_eval/README.md)

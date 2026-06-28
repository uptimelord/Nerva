# Results — v1.1.3.1 Action Scoring Range Discipline

Date: 2026-06-28  
Tag: `v1.1.3.1` (pending)  
Benchmark: [benchmarks/tagworld_action_score/](../../benchmarks/tagworld_action_score/README.md)

## Root cause (v1.1.3)

`stability << 2` in int16 action scoring overflowed after long online training → negative RUN score → WAIT won → frozen eval collapse.

## Fix

- `nerva_i32_saturating_add` for policy-edge score accumulation
- Weight-only policy edge scoring (no stability shift)
- Removed silent secondary WAIT fallback path
- `action_score_fallback_count` logs ticks with negative edge scores only
- `TagWorldActionScoreTrace` + `--action-score-trace` for edge contributors on invalid scores

## Gate

| Check | Result |
|-------|--------|
| Unit tests | 115/115 pass |
| Frozen eval seeds 1,5,11 | 100% escape, +20pp vs random |
| `eval_action_score_fallback_count` | 0 on all gate seeds |
| 10k train-pair fuzz | Run score stays positive, selects RUN |
| 1k learn + frozen eval | Passes gate |

## Decision

**Promote — v1.1.3.1**

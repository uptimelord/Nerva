# v1.1.3 Online Frozen Eval — Gate

**Status:** Promote (`v1.1.3`, 2026-06-28)

## Map

`--map tool --online-frozen`

## CLI gate command

```powershell
.\build\nerva_tagworld.exe --map tool --mode action --online-frozen --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11.

Defaults: 200 learn episodes, 100 frozen eval episodes.

## Promote if

- Learn phase: `eval.online_split.learn` window push and escape improve (20-episode windows)
- Eval phase: `eval.online_split.eval.escape_rate >= baseline + 0.20` on seeds 1, 5, 11
- Eval phase: `avg_mutations_per_episode == 0`
- Ablation test passes in unit suite
- Trace shows learned push edge weight > 0 after learn, eval selects push via edge scoring

## Unit tests

- `test_tagworld_online_frozen_learn_push_increases`
- `test_tagworld_online_frozen_eval_beats_random`
- `test_tagworld_online_frozen_eval_no_mutations`
- `test_tagworld_online_frozen_ablation_reduces_push`

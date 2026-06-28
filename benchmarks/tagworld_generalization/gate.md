# v1.2 TagWorld Generalization — Gate

**Status:** Promote (`v1.2`, 2026-06-28)

## Supported claim

> v1.2 demonstrates **supervised abstract tool-schema transfer** across TagWorld maps using adapter-emitted chokepoint events and frozen graph evaluation.

## Not supported (explicit)

- No pure feedback acquisition (oracle `train_pair` chains remain on escape)
- No zero-shot tool invention
- No broad generalization beyond adapter-emitted chokepoint events

## Command

```powershell
.\build\nerva_tagworld.exe --generalization --mode action --eval-map D --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11. Optional: `--eval-map E`, `--eval-map F`. Invariance: `--eval-map D'`.

## Promote if

- All unit tests pass (including v1.1.3 frozen regression)
- Map D geometry differs from map A
- Rename/copy invariance: `D'` frozen eval matches D metrics
- Held-out novel map D: beats random by >=20pp on seeds 1, 5, 11 (`tagworld_generalization_beats_random_gate` handles saturated baselines)
- Frozen eval: `avg_mutations_per_episode == 0`
- Ablation of learned chokepoint push edges reduces push or escape
- Trace shows abstract chokepoint → push → block at chokepoint → path blocked by tool → run → escaped

## Unit tests

- `test_tagworld_generalization_train_push_increases`
- `test_tagworld_generalization_eval_beats_random_on_D`
- `test_tagworld_generalization_eval_no_mutations`
- `test_tagworld_map_d_not_clone_of_a`
- `test_tagworld_generalization_rename_copy_invariance`
- `test_tagworld_generalization_ablation_reduces_push`
- `test_tagworld_generalization_abstract_trace_path`
- `test_tagworld_held_out_maps_push_then_run_wins`

## v1.2.1 preview

Pure feedback: remove oracle `train_pair` chains on escape.

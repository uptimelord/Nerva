# v1.2 TagWorld Generalization — Gate

**Status:** Repeat (RC harness; not final promote)

## Approved claim (current artifact)

> v1.2 demonstrates supervised abstract tool-schema transfer across TagWorld maps using adapter-emitted chokepoint events and frozen graph evaluation.

Do **not** claim: “Nerva discovered general tool use from scratch.”

## Command

```powershell
.\build\nerva_tagworld.exe --generalization --mode action --eval-map D --seed 1 --fast --baseline
```

Repeat for seeds 5 and 11. Optional held-out checks: `--eval-map E`, `--eval-map F`.

## RC gate (passes today)

- Train maps A/B/C: push selection and escape improve over 20-episode windows
- Held-out map D: `eval_escape_rate >= eval_baseline_escape_rate + 0.20` on seeds 1, 5, 11
- Frozen eval: `avg_mutations_per_episode == 0`
- Policy does not branch on map id or coordinates
- Held-out maps D/E/F winnable via push-then-run (geometry sanity)

## Final promote blockers

- Map D must be a **genuinely new** chokepoint topology (not A geometry clone)
- Rename/copy invariance test (layout-preserving relabel)
- Claim scope: supervised abstract transfer unless oracle `train_pair` feedback is removed

## Kill if

- Policy branches on map id or coordinates
- Held-out success collapses under layout-preserving rename
- Trace shows only concrete coordinates, not chokepoint relations

## Unit tests

- `test_tagworld_generalization_train_push_increases`
- `test_tagworld_generalization_eval_beats_random_on_D`
- `test_tagworld_generalization_eval_no_mutations`
- `test_tagworld_held_out_maps_push_then_run_wins`

## v1.2.1 (pure feedback) gate preview

Promote if:

- Push selection rises over episodes without oracle edge pair injection on escape
- Held-out novel map beats random after frozen eval
- Ablation of learned chokepoint/tool edges drops success
- Trace shows credit from actual action/outcome path, not injected pair labels

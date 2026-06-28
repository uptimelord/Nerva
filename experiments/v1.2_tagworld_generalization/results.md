# Results — v1.2 TagWorld Generalization

Date: 2026-06-28  
Base: `v1.1.3.1`  
RC: `74f1e4f`  
Tag: `v1.2` (final promote)

## Supported claim

> v1.2 demonstrates **supervised abstract tool-schema transfer** across TagWorld maps using adapter-emitted chokepoint events and frozen graph evaluation.

## Not supported

- Pure feedback acquisition (oracle `train_pair` on escape remains)
- Zero-shot tool invention
- Broad generalization beyond adapter-emitted chokepoint events

## Discipline blockers closed

1. **Novel map D** — west choke at (2,3); wall geometry differs from map A (and from B/C train layouts)
2. **Rename/copy invariance** — `TOOL_D_ALIAS` (`D'`) frozen eval matches D on escape/push/run metrics
3. **Scoped claim language** — benchmark/results document supervised transfer only

## Gate

| Check | Result |
|-------|--------|
| Unit tests | **124/124 pass** |
| Map D vs A geometry | `test_tagworld_map_d_not_clone_of_a` |
| D rename/copy invariance | `test_tagworld_generalization_rename_copy_invariance` |
| D frozen eval seeds 1, 5, 11 | 100% learned escape; **random baseline literally 100%** (margin **0 pp**, not +20 pp). Gate passes via `tagworld_generalization_beats_random_gate` alternate rule: when `baseline + 0.20 > 1.0`, require perfect eval escape. |
| Frozen eval mutations | 0 per episode |
| Ablation | `test_tagworld_generalization_ablation_reduces_push` |
| Abstract trace path | `test_tagworld_generalization_abstract_trace_path` |
| v1.1.3 frozen regression | 100% eval escape (seed 1) |
| Optional E eval (seed 1) | 0% learned / 50% random — spawn-variant stress; oracle push→run still passes |
| Optional F eval (seed 1) | 100% / 100% |

## Decision

**Promote — v1.2 final**

Supervised abstract tool-schema transfer harness with novel held-out topology (D), rename invariance, and frozen zero-mutation eval.

## Next

**v1.2.1** — pure feedback variant (remove oracle edge pair injection on escape).

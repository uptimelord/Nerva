# Results — v1.2 TagWorld Generalization (RC)

Date: 2026-06-28  
Tag: none (Repeat — not final promote)  
Base: `v1.1.3.1`  
Benchmark: [benchmarks/tagworld_generalization/](../../benchmarks/tagworld_generalization/README.md)

## What this proves

- Abstract adapter events can drive a learned policy
- Policy does not branch on map id or coordinates during frozen eval
- Frozen eval works with zero mutations
- Multi-map train (A/B/C) → held-out eval (D/E/F) works under the current scaffold

## What this does not yet prove

- Reusable chokepoint/tool schema transfer to a **genuinely novel** topology (map D shares A geometry)
- Rename/copy invariance under layout-preserving relabel
- Tool-schema acquisition without oracle edge labeling (`tagworld_nerva_train_pair` on escape)
- Action scoring without a fixed policy-edge whitelist

## Gate (current harness)

| Check | Result |
|-------|--------|
| Unit tests | 119/119 pass |
| Generalization seeds 1, 5, 11 (map D) | 100% frozen eval escape, ≥+20pp vs random |
| Frozen eval mutations | 0 per episode |
| v1.1.3 frozen regression | Pass |

## Decision

**Repeat**

Abstract generalization harness works, but final promote is blocked by held-out topology and invariance gaps.

## Approved claim (if re-run passes hardening)

> v1.2 demonstrates supervised abstract tool-schema transfer across TagWorld maps using adapter-emitted chokepoint events and frozen graph evaluation.

Not: “Nerva discovered general tool use from scratch.”

## Blockers before final `v1.2` tag

1. Add rename/copy invariance test
2. Make map D a genuinely new chokepoint topology (not A clone)
3. Keep claim explicitly “supervised abstract transfer” unless oracle pair feedback is removed

## Next: v1.2.1 (pure feedback)

Remove `tagworld_nerva_train_pair` chains on escape; learn only from traces + outcome feedback.

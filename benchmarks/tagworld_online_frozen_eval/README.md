# v1.1.3 Online Tool Acquisition → Frozen Policy Eval

**Status:** Promote (`v1.1.3`, 2026-06-28)

## Goal

Separate **online learning** from **frozen evaluation** so policy quality is measurable without exploration noise.

## Phases

| Phase | Episodes | ε | Mutations | Snapshot |
|-------|----------|---|-----------|----------|
| A — learn | 200 | 15% | yes | save graph after phase |
| B — eval | 100 | 0% | no | restore saved graph each episode |

## Allowed pretrain (Phase A only)

Same dynamics-only pretrain as v1.1.2 — no push enable links. Includes `path_blocked→run_safe` dynamics prior (run-after-block); push enable edges start at zero.

## Promote if

- Phase A: push selection rises (last 20 episodes > first 20)
- Phase B: frozen eval escape beats random by ≥20 pp on seeds **1, 5, 11**
- Phase B: `avg_mutations_per_episode == 0` (no learning during eval)
- Ablation of learned push enable edges reduces eval push usage or escape
- No hardcoded map-specific push branch in `tagworld_nerva_select_action`

## Kill if

- Phase A push flat on all seeds
- Phase B fails ≥20 pp margin on any gate seed
- Eval phase applies mutations
- Ablation does not reduce push/escape

## Prior

- [v1.1.2 Online Tool Acquisition](../tagworld_online_tool/README.md)

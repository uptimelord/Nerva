# v1.3 Honest TagWorld — Stage 1 Results (revive the task)

**Decision:** Repeat (Stage 2 passes; Stage 3 wiring in progress)
Date: 2026-06-29
Gate: [benchmarks/tagworld_honest/gate.md](../../benchmarks/tagworld_honest/gate.md)

## What Stage 1 did

Added `--honest` / `cfg.honest` (carried onto `TagWorld.honest`). Behind the flag, the two
seeker-freeze sites are disabled:
- avoidance-bubble in `tagworld_cell_walkable_mut` (tagworld.c:334) — froze the seeker and barred
  the runner from ever getting adjacent (so CAUGHT was impossible)
- tool-map early-return in `tagworld_step_seeker` (tagworld.c:604)

With both off, the seeker pursues on tool maps exactly like it already does on the corridor map.

## Measurement (random baseline, 300 episodes/seed)

| Map | legacy random escape | honest random escape | honest caught | honest timeout |
|-----|----------------------|----------------------|---------------|----------------|
| A   | 0.637                | **0.000**            | 0             | 300            |
| B   | 1.000                | **0.000**            | 300           | 0              |
| C   | 1.000                | **0.000**            | 0             | 300            |
| G   | 0.637                | **0.077**            | 0             | 300            |

Pretrained ("oracle-ish") policy under honest also collapses to 0% escape: on A/G it loops selecting
a no-op `PUSH_BLOCK_TO_DOORWAY` (~62×/episode) and times out; the chokepoint-symbol policy trained
under the frozen world mis-fires once the seeker actually moves.

## Diagnosis (the trace says)

The avoidance-bubble was not only neutering the seeker — it was **the mechanism that made the maps
solvable**. Remove it and:

- random play never reaches `safe` within the 64-tick budget (A/C/G → timeout), or the seeker
  simply catches the runner (B → caught). Baseline collapses to ~0 on every map.
- the failure mode is **different per map** (timeout vs caught), confirming none of A/B/C/G was
  designed or calibrated for live pursuit.

So the Stage 1 gate (random baseline 20–80%, caught > 0, oracle ≥ 95%) **cannot be met by simply
unfreezing the existing maps**. The geometry, spawns, seeker cadence, and tick budget were all tuned
around a static seeker.

## Calibration attempt: half-speed seeker (every-other-tick)

Added `TagWorld.seeker_tick_parity` — under honest the seeker moves every other tick (the standard
pursuit slowdown). Re-measured random escape (300 eps):

| Map | honest full-speed | honest half-speed |
|-----|-------------------|-------------------|
| A   | 0.000             | 0.000             |
| B   | 0.000             | 0.000             |
| C   | 0.000             | 0.000             |
| G   | 0.077             | 0.243             |

Half-speed lifts only G (0.08 → 0.24). A/B/C stay at **0** regardless of seeker speed → their
failure is **reachability/geometry**, not chase speed. Seeker cadence is a real knob (kept) but it
cannot rescue maps whose `safe` is simply not reachable by a random player in budget once the
avoidance-bubble (which also freed the runner) is gone.

**Conclusion:** the existing A/B/C/G maps cannot host the honest task. Stage 1 needs a purpose-built
pursuit map, not a retrofit.

## This is honest, not a dead end

The `--honest` plumbing works and is correct (the seeker really does pursue; the block still seals
routes via the existing block-cell non-walkable check). What's missing is a **map actually designed
for tool-use under pursuit**: a single chokepoint the seeker must cross, a `safe` the runner can
reach *without* the chokepoint, a block that can seal it, and timing where running-without-sealing
sometimes loses but sealing-then-running reliably wins.

## Next (Stage 1b — required before Stage 2/3)

Design **one** dedicated honest pursuit map (call it `H`) and calibrate jointly until the scripted
push→run oracle escapes ≥95% AND random escape lands 20–80% AND caught > 0. Likely knobs:
- seeker cadence (every-other-tick is the standard pursuit slowdown; needs a cadence field, not
  `w->tick`, because the baseline loop doesn't set `w->tick`)
- runner/seeker/block/safe spawn distances vs tick budget
- one true chokepoint with a non-chokepoint route to `safe`

Stages 2 (primitive perception) and 3 (native-feedback credit) are unblocked only once a Stage-1
map exists where the task is real.

## Stage 1b — purpose-built map H (2026-06-29)

Map `H` (`TAGWORLD_MAP_TOOL_H`, `--map H`) replaces A/B/C/G for honest pursuit calibration.

**Geometry:** wall row at y=2 with single doorway (3,2). Block starts at (4,3) — east of the choke
column — so the seeker can cross while the doorway is open. Runner (5,3) must reach the block before
sealing. Safe (1,5) stays in the runner region.

**Calibration:** `seeker_steps_per_tick=2` on map H (set in `tagworld_reset_tool_spawns`).

**Gate measurement** (`test_tagworld_map_h_honest_stage1_gate`, 300 eps random / 200 eps oracle):

| Metric | Value | Stage 1 gate |
|--------|-------|--------------|
| Oracle push→run escape | 100% | ≥95% |
| Random escape | 48.7% | 20–80% |
| Random CAUGHT | 154/300 | >0 |
| Run-only escape | 0% | below oracle −10pp |

**Root cause fixed:** block at (3,3) blocked the only south exit from the doorway column, so the
seeker could never enter the runner region (pursuit was fake). Moving block to (4,3) fixes crossing.

## Stage 2 — primitive perception, zero starting knowledge (2026-06-29)

**Implementation:** 14 geometry feature nodes (seeker bearing/dist, block-adjacency, wall-adjacency)
plus `RUNNER_AT_SAFE`, dense `feature→action` edges at weight 0, no oracle pretrain under `--honest`.

**Gate measurement** (`test_tagworld_honest_*`, map H, seed 1):

| Check | Result |
|-------|--------|
| Chokepoint symbols absent | pass (simulated episode scan) |
| Primitive policy edges zero after pretrain | pass |
| Zero-weight escape == random baseline | 0.4967 == 0.4967 |

**Next:** Stage 3 — native outcome credit on honest primitive traces (`--honest --pure-feedback`),
frozen eval generalization. Legacy A/B/C/G may Kill unless pursuit-calibrated; map H is the train
anchor.

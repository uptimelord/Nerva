# TagWorld Honest Tool-Learning — Gate (v1.3)

**Status:** Draft (written before code, per Discipline.md "write before running")
**Base:** v1.2.1.x (kept intact as historical record; honest mode is flag-gated, `--honest` / `cfg.honest`)

## Why this exists

v1.2 / v1.2.1.x "tool generalization" is handholding, not learning. Verified in code:

1. **Dead task.** On tool maps the seeker is frozen — `tagworld_cell_walkable_mut`
   (`worlds/tagworld/tagworld.c:334-339`) marks every cell within Manhattan-1 of the seeker
   non-walkable, so the seeker can never move and the runner can never get adjacent. `CAUGHT`
   (`tagworld.c:624`) is therefore impossible; outcomes are only ESCAPED/TIMEOUT. There is no pursuit.
2. **Blindfolded model fed the answer.** In abstract mode the world emits 5 pre-computed predicates
   (`tagworld_emit_abstract_tool_events`, `tagworld.c:1165-1185`) that are literally renamed
   doorway-position checks (`maps/tagworld_maps.c:300-344`). "Generalization to held-out G" is true
   by construction: every map emits the identical symbol stream.
3. **Hand-coded credit + forced coverage.** Credit is per-(action,outcome,world-flag)
   (`tagworld_pure_feedback_apply_credit`, `tagworld.c:1331-1397`); coverage forces systematic
   action sampling for the whole learn window (`tagworld.c:144-156`, `759-771`).

Honest mode removes all three. The model must perceive primitive geometry, learn a
perception→action policy from native outcome feedback only, and generalize to a held-out map whose
**perceptual stream actually differs**.

## Design (decided)

- **Keep macro tool-actions** (`PUSH_BLOCK_TO_DOORWAY` stays a scripted one-cell push; the model
  learns *when*, not the motor plan). Action set unchanged.
- **Live seeker** on tool maps under `--honest` (reuse corridor's existing pursuit mechanics;
  `step_seeker` already moves on non-tool maps).
- **Primitive perception** replaces the 5 chokepoint symbols: seeker direction bucket, seeker
  distance bucket, block-adjacency direction, wall-adjacency direction, at-safe. The model must
  combine these — none of them is "chokepoint".
- **Pre-declared dense primitive-feature→action edges at weight 0**, tuned only by the engine's
  native `nerva_feedback_correct/wrong` (`src/nerva_learning.c`) via ε-greedy. No hand-coded
  eligibility rules, no forced coverage. (The engine cannot grow edges from co-activation, so a
  zero-weight skeleton is the minimum honest substrate — the model discovers which features matter,
  not which edges exist.)
- New benchmark/experiment line; v1.2.x untouched.

## Stage gates (each must pass before the next; Kill is a valid honest outcome)

### Stage 1 — Revive the task
**Promote if** (under `--honest`, purpose-built map **H**, ≥3 seeds):

Map H is the honest pursuit calibration map (`--map H`). Legacy maps A/B/C/G fail this gate by
design (they were tuned for frozen seeker); do not retrofit them.

Legacy check (Repeat evidence only, not Promote): maps A/B/C/G collapse to ~0 random escape under
`--honest` — see `experiments/v1.3_tagworld_honest/results.md`.

**Promote if** (under `--honest`, map **H**, ≥3 seeds):
- random baseline escape is **non-saturated, 20–80%**, and **meaningfully below** oracle push→run
- `CAUGHT` occurs a **nonzero** fraction under random play (pursuit is real)
- oracle push→run still escapes **≥95%**

**Kill if** no seeker-speed / tick-budget / geometry calibration makes the tool both necessary
(running without sealing loses enough) and sufficient (sealing + run wins) — i.e. the world cannot
host a real tool-use-under-pursuit task with macro actions.

### Stage 2 — Primitive perception, zero starting knowledge
**Promote if:**
- in honest mode the 5 chokepoint symbols are **not emitted** (test asserts absence)
- with the primitive→action table at weight 0 and no oracle, **frozen-eval escape == baseline**
  (proves the model starts knowing nothing)

**Kill if** primitive features cannot be defined without one of them being a renamed "chokepoint"
answer (audit: no single feature ≈ `block_at_chokepoint`).

### Stage 3 — Honest learning + generalization (the real gate)
**Promote if** (train on honest-viable maps, **frozen-eval on held-out map with different primitive
geometry**, ≥3 seeds):

Honest train maps must pass Stage 1 pursuit calibration (map **H** is the current anchor; legacy
A/B/C/G fail live pursuit — do not use them until recalibrated). Eval map must emit a **different**
primitive stream than train maps (not identical symbol rename).

**Promote if** (train + frozen eval under `--honest --pure-feedback`, ≥3 seeds):
- `eval_escape_rate >= eval_baseline + 0.20`
- `eval_avg_mutations_per_episode == 0`
- ablation of the learned primitive→action edges **drops eval escape** (weights are causal)
- a saved trace shows `perception primitives → action → outcome` with **no chokepoint symbol** and
  **no oracle `train_pair`** on the path
- credit is engine-native only: `tagworld_debug_oracle_online_train_pair_rounds() == 0` and no
  hand-coded per-action eligibility executed in honest mode

**Kill if** after honest tuning (ε schedule, ltp/ltd, episodes) the held-out gate cannot be met —
report which trace revealed the failure and whether the idea (native-feedback policy learning over
primitive perception) or only this implementation is dead. **Do not** rescue it by re-adding
chokepoint symbols, oracle pairs, or forced coverage.

## Claim discipline (to be filled at Promote/Kill)

```
Supported:     <what the trace+metrics actually prove>
Not supported: <no perceptual scaling beyond TagWorld; macro actions still scripted; engine still
               needs a pre-declared zero-weight edge skeleton>
Evidence:      <metrics table, ablation, trace file>
Residuals:     <next gates>
```

# v1.2.1 Results

**Decision:** Repeat (superseded for learning by v1.2.1.1 eligibility credit)

v1.2.1 established outcome-only pure-feedback credit wiring. That mechanism is honest but too blunt
for multi-step tool use. See [v1.2.1.1 results](../v1.2.1.1_tagworld_eligibility_credit/results.md)
for eligibility credit and promote evidence.

## Still TBD (blocks promote)

- push selection rises during online learning (currently flat; sparse-reward credit not converging)
- frozen eval beats baseline on a nontrivial held-out map (G eval still 0% vs ~0.59-0.74 random)
- ablation drops push/escape
- (full credit trace now present — see below)

## Credit trace wiring (landed)

- `--pure-feedback` disables oracle `train_pair` chains in online tool acquisition.
- Episode-local decision records (`TagWorldCreditTrace` / `TagWorldDecision`): per decision capture
  `episode_id`, `decision_tick`, active context nodes, selected action node, contributing policy
  edges, action score, valid mask, `explored`, `tie_zero_score`, and consequence flags
  (`led_to_block_at_chokepoint`, `led_to_path_blocked_by_tool`).
- Outcome credit through the mutation queue only: ESCAPED strengthens (`+ltp`), TIMEOUT/CAUGHT
  weakens (`-ltd`) the policy edges actually used by the episode's selected actions. No oracle pairs.
- Exploration: epsilon during learn (`online_explore_pct`); frozen eval uses epsilon=0, zero
  mutations, learned scores only. `ACTION_TIE_ZERO_SCORE` is counted (and logged under
  `--action-score-trace`) when every valid action scores zero.
- New abstract policy edge `CHOKEPOINT_DETECTED -> ACTION_WAIT` lets feedback weaken WAIT in
  chokepoint context (a scoring/feedback rule for the all-WAIT trap, not a policy override).
- Dynamics pretrain (`tagworld_pretrain_abstract_dynamics`) unchanged — world structure
  observation only.

## Measured pure-feedback runs on G (oracle disabled)

| seed | train escape | train strengthen / weaken muts | tie-zero | eval escape | eval random | eval push |
|------|--------------|--------------------------------|----------|-------------|-------------|-----------|
| 1    | 0.685        | 535 / 322                      | 404      | 0.000       | 0.590       | 0         |
| 5    | 0.685        | 529 / 368                      | 462      | 0.000       | 0.740       | 0         |
| 11   | 0.680        | 517 / 357                      | 462      | 0.000       | 0.690       | 0         |

The credit loop runs (hundreds of strengthen/weaken mutations per run), but the learned frozen
policy still never pushes. Diagnosis: under random-exploration credit, a PUSH chosen at a single
tick rarely completes the full push -> block-at-chokepoint -> run -> escape chain, so push-context
edges are weakened about as often as strengthened and never dominate WAIT at eval. This is the
expected Repeat: the mechanism is honest and wired; convergence is the next (deferred) tuning step.

## Evaluation map note

Map D is not useful for the "beats random" claim: random baseline is already saturated at 100%.
v1.2.1 uses a new held-out pressure map **G** so the +20 pp margin is meaningful.

Measured G (seeds 1-11, 200 episodes each):

| Map | oracle escape | random escape | non-escape mode |
|-----|---------------|---------------|-----------------|
| A (train) | 100% | ~0.63 | timeout |
| D (held-out) | 100% | 1.00 (saturated) | — |
| **G (held-out, new)** | 100% | ~0.62-0.72 | timeout |

Note on the world: on tool maps the seeker is effectively static (its own avoidance bubble blocks
every adjacent step), so the only non-escape outcome is **timeout**. G's pressure therefore comes
from the block obstructing the sole short corridor — random play frequently fails to clear it in the
tick budget, while the oracle push→run reliably opens the route. See [gate.md](gate.md).

## First pure-feedback run on G (seed 1, oracle disabled)

```
--generalization --pure-feedback --mode action --eval-map G --seed 1 --fast --baseline
eval_escape_rate        = 0.0000
eval_baseline_escape    = 0.5900   (random, not saturated)
eval_action_push_doorway= 0        (learned policy never pushes)
action_wait_count       = 6400     (frozen eval is all WAIT)
eval_avg_mutations/ep   = 0.00     (frozen)
```

This is the expected Repeat state: removing the oracle `train_pair` chains leaves the
learner with no acquired push behavior, so frozen eval loses to random. The gap
(0.0 vs 0.59) is now meaningful because G's baseline is not saturated.

## Order of work (per plan)

1. Add held-out pressure map G. **done**
2. Confirm oracle wins (push→run escape >= 95%). **done** (100%)
3. Confirm random baseline is not saturated (20-80%). **done** (~0.62-0.72)
4. Run pure-feedback learning. **done** (eval 0.0 — credit wired, not converging)
5. Inspect traces. **done** (decision records link context→action→consequence→outcome→mutation)
6. Only then tune feedback/credit. **next / deferred** (e.g. eligibility traces over the full
   decision chain, reward shaping on `led_to_block_at_chokepoint`, or annealed exploration —
   none added yet to avoid biasing the honest baseline)

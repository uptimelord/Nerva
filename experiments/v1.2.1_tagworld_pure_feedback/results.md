# v1.2.1 Results

**Status:** Repeat / Draft

> v1.2.1 currently establishes the pure-feedback harness: oracle online `train_pair` chains are disabled and selected policy edges are traceable. It does not yet establish pure-feedback tool-schema acquisition.

## Still TBD (blocks promote)

- push rises during online learning
- frozen eval beats baseline on a nontrivial held-out map
- ablation drops push/escape
- full trace connects action credit to outcome mutation

## Harness (landed)

- `--pure-feedback` disables oracle `train_pair` chains in online tool acquisition
- Policy edges contributing to the selected action record `NERVA_TRACE_USED_PATH` during learn phase
- Dynamics pretrain (`tagworld_pretrain_abstract_dynamics`) unchanged — world structure observation only

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
4. Run pure-feedback learning. **done** (currently 0.0 — TBD)
5. Inspect traces. **next**
6. Only then tune feedback/credit. **deferred** (do not tune learning before the eval map is right)

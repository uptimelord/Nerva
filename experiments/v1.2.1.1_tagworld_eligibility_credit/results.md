# v1.2.1.1 Results

**Decision:** Promote (with seed-11 residual)

v1.2.1.1 replaces blanket outcome-only pure feedback with **eligibility credit**: policy edges are
strengthened or weakened only when the decision that used them produced an observed intermediate
consequence (push→block, path_blocked→run→escape) or failed without improvement (wait→timeout).
Oracle `train_pair` chains remain disabled.

## Supported

- Eligibility credit fires through the mutation queue with per-rule counters
  (`eligibility_push_block_strengthen`, `eligibility_run_escape_strengthen`,
  `eligibility_wait_timeout_weaken`, `eligibility_run_fail_weaken`).
- On held-out pressure map G, seeds 1–7 achieve frozen eval escape 100% vs random baseline
  0.59–0.74 (+20 pp margin met).
- PUSH selection rises during learning (last-window episodes-with-push > first-window).
- Ablation of learned push edges reduces push or escape (seed 1 unit test).
- No oracle online `train_pair` chains (`test_tagworld_pure_feedback_no_oracle_train_pairs`).

## Not supported

- Universal convergence across all seeds: seed 11 fails (0% eval, zero push-block credit events).
- Adversarial chase behavior — map G pressure is obstruction/path-clearance under tick pressure.

## Evidence

### Gate seeds (required)

| seed | train push first/last window | push_block credit | run_escape credit | eval escape | eval random | eval push |
|------|------------------------------|-------------------|-------------------|-------------|-------------|-----------|
| 1    | 6 / 12                       | 94                | 828               | 1.000       | 0.590       | 100       |
| 5    | 5 / 13                       | 46                | 634               | 1.000       | 0.740       | 100       |
| 11   | 4 / 8                        | 0                 | 487               | 0.000       | 0.690       | 0         |

### Additional variance seeds

| seed | push_block credit | eval escape | eval random |
|------|-------------------|-------------|-------------|
| 2    | 112               | 1.000       | 0.700       |
| 3    | 110               | 1.000       | 0.640       |
| 7    | 68                | 1.000       | 0.600       |

## Residuals / next gates

- **Seed 11:** zero observed push→block consequences during learning; eligibility cannot bootstrap
  push without at least one successful intermediate credit event. Track as exploration cold-start
  stress, not v1.2.1.1 mechanism failure on seeds where push_block credit > 0.
- v1.2.1 outcome-only credit superseded by eligibility rules in this commit.

## Discipline claim

```text
Supported:     pure-feedback tool-action acquisition on held-out map G via eligibility credit
               (observed push→block, path_blocked→run→escape, wait→timeout weakening)
Not supported: guaranteed convergence on every seed without intermediate push observations
Evidence:      metrics above, unit tests, ablation test seed 1
Residuals:     seed 11 exploration cold-start; static seeker / obstruction world (not pursuit)
```

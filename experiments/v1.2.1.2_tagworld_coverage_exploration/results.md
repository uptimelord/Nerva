# v1.2.1.2 Results

**Decision:** Repeat (strong)

v1.2.1.2 adds coverage exploration and removes dynamics pretrain for `--pure-feedback` mode. All
gate seeds (1, 2, 3, 5, 7, 11) reach **100% frozen eval** on held-out map G with policy edges
starting at zero.

```text
before:
  eligibility credit works only when lucky exploration sees push→block

now:
  coverage exploration guarantees the circuit gets sampled,
  eligibility attaches at consequence time,
  zero-start push+run policy reaches 100% frozen eval on G across seeds
```

Seed 11 cold-start failure from v1.2.1.1 is resolved. RUN-after-block is acquired online (no
`train_pair` pretrain on `PATH_BLOCKED_BY_TOOL → RUN`).

## Run configuration

```text
dynamics_pretrain        = off  (pure-feedback skips tagworld_pretrain_dynamics_only)
oracle_train_pair_rounds = 0   (test_tagworld_pure_feedback_no_oracle_train_pairs)
coverage_phase           = on   (online_coverage_episodes defaults to learn window = 400)
eval_mutations           = 0    (eval_avg_mutations_per_episode = 0.00 all gate seeds)
random_baseline range    = 0.59–0.74 on map G
eval_escape              = 1.0 on seeds 1, 2, 3, 5, 7, 11
```

## Supported claim

```text
Nerva can acquire a zero-start push+run policy on held-out pressure map G using coverage
exploration and eligibility credit, without dynamics pretrain or oracle train_pair chains
during learning.
```

Evidence:

- `tagworld_pretrain_for_config` skips dynamics pretrain when `pure_feedback`; run and push edges
  start at weight 0 (`test_tagworld_pure_feedback_no_dynamics_pretrain`).
- Gate seeds all show `train_push_block_observations > 0` and
  `train_eligibility_push_block_strengthen > 0`.
- Frozen eval beats random by ≥ 20 pp on every gate seed.
- Unit tests green including `test_tagworld_pure_feedback_g_all_gate_seeds`.
- No oracle online `train_pair` during pure-feedback learn.

## Not supported

```text
not no-handholding
not unsupervised schema discovery
not acquisition without coverage exploration
not general tool use beyond adapter-emitted TagWorld events
```

Specifically **not** supported:

- Learning without coverage exploration (epsilon alone is insufficient on some seeds)
- Unsupervised tool-schema / adapter-event discovery (events are world-provided labels)
- Eligibility rules learned from data (strengthen/weaken conditions are engineered)
- Fixed graph topology is not discovered online
- Coverage exploration is an engineered learning phase, not emergent discovery

## Evidence

### Gate seeds

| seed | train escape | push_block obs | push credit | run credit | coverage actions | tie_zero (train) | eval escape | eval random |
|------|--------------|----------------|-------------|------------|------------------|------------------|-------------|-------------|
| 1    | 0.8775       | 89             | 89          | 1526       | 1060             | 924              | 1.000       | 0.590       |
| 2    | 0.8900       | 92             | 93          | 1561       | 1059             | 929              | 1.000       | 0.700       |
| 3    | 0.8800       | 87             | 88          | 1540       | 1066             | 954              | 1.000       | 0.640       |
| 5    | 0.6050       | 89             | 89          | 763        | 1063             | 924              | 1.000       | 0.740       |
| 7    | 0.8600       | 80             | 81          | 1484       | 1076             | 924              | 1.000       | 0.600       |
| 11   | 0.8700       | 95             | 96          | 1512       | 1066             | 983              | 1.000       | 0.690       |

Column mapping:

- `push_block obs` → `train_push_block_observations`
- `push credit` → `train_eligibility_push_block_strengthen`
- `run credit` → `train_eligibility_run_escape_strengthen`
- `coverage actions` → `train_coverage_explore_count`
- `tie_zero (train)` → `train_action_tie_zero_score_count`

Eval phase: `coverage_explore_count=0`, `push_block_observations=0`,
`eligibility_push_block_strengthen=0`, `eligibility_run_escape_strengthen=0`,
`eval_avg_mutations_per_episode=0.00`.

### Command

```powershell
.\build\nerva_tagworld.exe --generalization --pure-feedback --mode action --eval-map G --seed <N> --learn-episodes 400 --fast --baseline
.\build\test_runner.exe
```

Coverage defaults to full learn window when unset (`online_coverage_episodes == online_learn_episodes`).

## Mechanism fixes (v1.2.1.2)

1. **Post-consequence edge attachment** — push/run policy edges attached when block-at-chokepoint
   is observed after `apply_action`, not only from pre-action fire_log capture.
2. **RUN weaken guard** — weaken `PATH_BLOCKED_BY_TOOL → RUN` only when RUN was taken while path
   was blocked (`led_to_path_blocked_by_tool`), not on every failed episode.
3. **Push validity** — on tool maps, push actions masked when block is already at chokepoint so
   learned push weights cannot trap eval in push-only loops.

## Next gate (v1.2.1.3)

**Coverage ablation / coverage reduction** — not “make it smarter.”

```text
How much coverage is required before the policy self-sustains?
```

Variants: full coverage, half coverage, epsilon only, coverage for first N episodes only,
coverage disabled after first successful push→block.

Promote only when:

```text
coverage is a bootstrap mechanism, not the whole policy
```

## Discipline claim

```text
Supported:     zero-start pure-feedback acquisition of push+run policy on map G across gate seeds
               with coverage exploration and eligibility credit; no dynamics pretrain; no oracle
               escape train_pair chains during learn
Not supported: no-handholding learning; acquisition without coverage; unsupervised schema discovery
Evidence:      table above, unit tests, ablation tests
Residuals:     engineered eligibility rules; adapter events; coverage schedule; seed-5 train
               escape still low (0.605) despite perfect eval — monitor variance on longer runs
```

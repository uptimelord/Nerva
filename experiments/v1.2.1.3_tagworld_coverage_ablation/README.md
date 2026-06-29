# v1.2.1.3 TagWorld Coverage Ablation

**Base:** `v1.2.1.2` (`04c9b4b`)  
**Status:** Draft (not run)

## Problem

v1.2.1.2 originally reported zero-start pure-feedback acquisition on held-out map G when **full
coverage** exploration runs for the entire learn window. Strict v1.3 re-audit marks that evidence as
legacy/fallback-contaminated, and the exploration schedule was engineered:

```text
Nerva can acquire the policy when the environment guarantees sufficient action/consequence coverage.
```

Not:

```text
Nerva discovers tool use with no scaffold.
```

## Question

```text
How much coverage does the circuit need before normal learning can take over?
```

## Test ladder

| variant | CLI |
|---------|-----|
| 400 episodes full coverage (baseline) | `--coverage-episodes 400` |
| 200 episodes coverage | `--coverage-episodes 200` |
| 100 episodes coverage | `--coverage-episodes 100` |
| 50 episodes coverage | `--coverage-episodes 50` |
| coverage until N push→block obs | `--coverage-until-push-block N` |
| epsilon-only, no coverage | `--coverage-episodes 0` |

All variants: `--generalization --pure-feedback --mode action --eval-map G --learn-episodes 400 --fast --baseline`

## Diagnostic curve

The important curve is not escape rate alone:

```text
push_block_observations -> push_credit_count -> eval_push_count -> eval_escape
```

That curve distinguishes coverage forcing behavior from graph charging.

## Gate

See [gate.md](gate.md).

## Reproduce

```bash
./experiments/v1.2.1.3_tagworld_coverage_ablation/commands.sh
```

Or run individual variants from [commands.sh](commands.sh).

# v1.2.1.3 Results

**Decision:** Draft (not run)

## Question

```text
How much coverage does the circuit need before normal learning can take over?
```

## Variants

| variant | coverage setting | status |
|---------|------------------|--------|
| coverage_400 | `--coverage-episodes 400` | pending |
| coverage_200 | `--coverage-episodes 200` | pending |
| coverage_100 | `--coverage-episodes 100` | pending |
| coverage_50 | `--coverage-episodes 50` | pending |
| until_push_block_N | `--coverage-until-push-block N` | pending |
| epsilon_only | `--coverage-episodes 0` | pending |

## Results table

_TBD — run [commands.sh](commands.sh) and fill per variant × seed._

### Diagnostic curve (per passing variant)

```text
push_block_observations -> push_credit_count -> eval_push_count -> eval_escape
```

## Decision

_TBD after runs._

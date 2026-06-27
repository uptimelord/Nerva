# Benchmark rules

## Decision rule before results

Every benchmark README and experiment README must include a decision section **before** any results section.

Required wording:

```text
Promote if:
- ...

Kill if:
- ...
```

Write the rule before running. When a discipline preflight tool exists:

```sh
nerva discipline preflight-readme experiments/path/to/README.md
```

Until then, check manually.

Benchmark-specific criteria belong in `benchmarks/<name>/gate.md`, not in root `DISCIPLINE.md`.

## Claim discipline

Every **Promote** must state four fields in `results.md`:

### Supported

What the evidence actually proves — narrow and testable.

### Not supported

What must **not** be implied from this result. Prevents demo creep.

Example (TagWorld-lite v1.1):

```text
Supported:
- Nerva can learn closed-loop event/prediction/action/outcome paths in TagWorld-lite
- Trained action beats random valid-action baseline by ≥20 pp on seeds 1, 5, 11

Not supported:
- Nerva has generalized tool use in bulk action mode
- Nerva can solve arbitrary grid worlds
- Nerva discovered strategy without environmental shaping
```

### Evidence

Metrics, test counts, trace excerpts, commit/tag.

### Residuals

Known gaps, open follow-ups (e.g. v1.1.1 tool-action gate).

## Default experiment shape

Routine confirmation runs: small **2×2** before large sweeps.

Recommended defaults:

```text
two graph sizes × two tick budgets
```

Example:

```text
graph sizes: 100 nodes, 1000 nodes
tick budgets: 32 ticks, 128 ticks
```

Alternatives when appropriate:

```text
two edge densities × two tick budgets
two memory limits × two query complexities
two trace decay settings × two feedback counts
```

Reserve extra seeds, randomized graphs, or large runs for promote-to-stage gates — not early exploration.

## Repository layout

```text
worlds/        reusable environments
benchmarks/    stable gates, configs, metrics definitions
experiments/   specific run evidence (README, results, metrics.json, commands)
runs/          raw logs — usually gitignored
evaluation/    dev and frozen evals
discipline/    extended global rules (this folder)
```

Do not put world simulators or long gate definitions in `experiments/`.

## Benchmark file expectations

Each benchmark directory should include at minimum:

```text
benchmarks/<name>/
  README.md       how to run
  gate.md         Promote if / Kill if
  configs/        default parameters (optional)
  metrics.md      benchmark-specific metrics (optional)
```

## Promotion vs kernel stage gates

- **Kernel stages** (v0.x): see [stage_gates_v0.md](stage_gates_v0.md)
- **World benchmarks** (v1.x+): gates in `benchmarks/*/gate.md`

Both use Promote/Kill wording; lifecycle states apply to both (see [experiment_lifecycle.md](experiment_lifecycle.md)).

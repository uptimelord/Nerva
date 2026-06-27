# Discipline — extended rules

Root [`DISCIPLINE.md`](../DISCIPLINE.md) is the constitution. These files hold detailed, reusable rules.

## Index

| File | Contents |
|------|----------|
| [experiment_lifecycle.md](experiment_lifecycle.md) | Draft / Repeat / Promote / Kill / Archive |
| [benchmark_rules.md](benchmark_rules.md) | Gates, claim discipline, default experiment shape |
| [reporting.md](reporting.md) | Metrics, efficiency, results template |
| [trace_requirements.md](trace_requirements.md) | Trace fields, storage, minimum successful test |
| [hot_loop_rules.md](hot_loop_rules.md) | Allowed vs forbidden in the tick loop |
| [architecture_guardrails.md](architecture_guardrails.md) | Mutation, sparse behavior, debug, coding agents |
| [frozen_eval_rules.md](frozen_eval_rules.md) | dev vs frozen eval layout and rules |
| [adapter_rules.md](adapter_rules.md) | World/benchmark adapter conventions |
| [stage_gates_v0.md](stage_gates_v0.md) | v0.0–v0.7 kernel stage gates, parameter discipline |

## Where other things live

```text
benchmarks/*/gate.md   how one benchmark is judged
experiments/*/results.md   what actually happened on one run
runs/                  raw logs (usually gitignored)
worlds/                reusable simulators
```

Simple rule:

> `DISCIPLINE.md` says how Nerva science is done.  
> `benchmarks/*/gate.md` says how one benchmark is judged.  
> `experiments/*/results.md` says what actually happened.

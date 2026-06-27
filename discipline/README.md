# Discipline — extended rules

Root **`Discipline.md`** is the local constitution (**gitignored** — not in git). Agents must read it before any work. Template: [`Discipline.example.md`](../Discipline.example.md) → `cp Discipline.example.md Discipline.md`.

Tracked **`discipline/`** holds detailed reusable rules. **`benchmarks/*/gate.md`** holds per-benchmark gates.

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

> `Discipline.md` (gitignored) says how Nerva science is done on this machine.  
> `discipline/` says how Nerva science is done in shared tracked docs.  
> `benchmarks/*/gate.md` says how one benchmark is judged.  
> `experiments/*/results.md` says what actually happened.

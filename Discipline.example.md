# Nerva Discipline

Global rules for **Nerva: A Conductance-Gated Cognitive Engine**.

The goal is not impressive demos. The goal is traceable, repeatable, hardware-aware proof — stage by stage.

**Detailed rules live in [`discipline/`](discipline/README.md). Benchmark gates live in [`benchmarks/`](benchmarks/). Run evidence lives in [`experiments/`](experiments/).**

> **Setup:** `Discipline.md` is gitignored (local constitution). Copy this file:
> `cp Discipline.example.md Discipline.md`

---

## Prime Rule

```text
No trace, no trust.
```

A result does not count unless the engine can print or save the trace that explains it.

If the trace is missing, the experiment is incomplete.

---

## Scope

This file is the **constitution** — read it before every serious experiment.

| Path | Holds |
|------|--------|
| [`worlds/`](worlds/) | Reusable environments (TagWorld, future worlds) |
| [`benchmarks/`](benchmarks/) | Stable gates, configs, metrics definitions |
| [`experiments/`](experiments/) | Specific run records and evidence |
| [`runs/`](runs/) | Raw logs — usually gitignored |
| [`evaluation/dev/`](evaluation/dev/) | Debugging evals |
| [`evaluation/frozen/`](evaluation/frozen/) | Reporting evals — do not tune on these |
| [`discipline/`](discipline/) | Extended discipline docs |

World-specific and benchmark-specific gates do **not** belong in this file.

---

## Experiment Lifecycle

Every benchmark run progresses through one of:

| State | Meaning |
|-------|---------|
| **Draft** | Idea exists; not trusted |
| **Repeat** | Runs complete; blockers remain |
| **Promote** | Gate passed; claim supported |
| **Kill** | Failed in a meaningful way |
| **Archive** | No longer active; kept for evidence |

See [discipline/experiment_lifecycle.md](discipline/experiment_lifecycle.md).

---

## Decision Rule Before Results

Every benchmark README and experiment README must define **Promote if / Kill if** **before** running.

Required wording:

```text
Promote if ...
Kill if ...
```

See [discipline/benchmark_rules.md](discipline/benchmark_rules.md).

---

## Claim Discipline

Every **Promote** result must state:

```text
Supported:     what the evidence actually proves
Not supported: what must not be implied
Evidence:      metrics, traces, tests
Residuals:     known gaps and next gates
```

This prevents demo creep. See [discipline/benchmark_rules.md](discipline/benchmark_rules.md#claim-discipline).

---

## Trace Requirements

Every successful test must produce an inspectable trace.

See [discipline/trace_requirements.md](discipline/trace_requirements.md).

---

## Reporting Requirements

No naked success claims. Report correctness **and** cost.

See [discipline/reporting.md](discipline/reporting.md).

---

## Architecture Guardrails

```text
No malloc/free in the hot tick loop
No graph mutation during propagation
No file I/O in the hot loop
Mutation only through the mutation queue
Sparse behavior: work proportional to active events, not graph size
```

See [discipline/architecture_guardrails.md](discipline/architecture_guardrails.md).

---

## Final Rule

Nerva is allowed to be simple.

It is not allowed to be opaque.

If the engine cannot explain what fired, what path carried the signal, what changed, and why — the result does not count.

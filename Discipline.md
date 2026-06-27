# Nerva Discipline

Global rules for **Nerva: A Conductance-Gated Cognitive Engine**.

The goal is not impressive demos. The goal is traceable, repeatable, hardware-aware proof — stage by stage.

**Detailed rules live in [`discipline/`](discipline/README.md). Benchmark gates live in [`benchmarks/`](benchmarks/). Run evidence lives in [`experiments/`](experiments/).**

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

Example (TagWorld-lite v1.1):

```text
Supported:     closed-loop event/prediction/action/outcome learning in TagWorld-lite
Not supported: generalized tool use; arbitrary grid worlds; strategy without shaping
```

This prevents demo creep. See [discipline/benchmark_rules.md](discipline/benchmark_rules.md#claim-discipline).

---

## Trace Requirements

Every successful test must produce an inspectable trace.

Minimum expectations: input → event path → nodes fired → edges used → traces → mutations queued → pass/fail.

See [discipline/trace_requirements.md](discipline/trace_requirements.md).

---

## Reporting Requirements

No naked success claims. Report correctness **and** cost (ticks, events, queue depth, memory).

See [discipline/reporting.md](discipline/reporting.md).

---

## Architecture Guardrails

Hard constraints unless an experiment explicitly tests a violation:

```text
No malloc/free in the hot tick loop
No graph mutation during propagation
No file I/O in the hot loop
Mutation only through the mutation queue
Sparse behavior: work proportional to active events, not graph size
```

See [discipline/architecture_guardrails.md](discipline/architecture_guardrails.md) and [discipline/hot_loop_rules.md](discipline/hot_loop_rules.md).

---

## Mutation Rules

```text
Propagation decides what happened.
Mutation applies later.
```

No direct edge creation, deletion, schema promotion, or permanent memory write inside propagation.

---

## Sparse Behavior Rule

Routine propagation must not scan all nodes or all edges. Full scans require explicit labels: `debug-only`, `offline consolidation`, `benchmark setup`, or `temporary implementation debt`.

---

## Benchmark Promotion Rules

- Gates are defined in `benchmarks/<name>/gate.md`, not here.
- Default experiment shape: small 2×2 (two sizes × two tick budgets) before large sweeps.
- Frozen evals stay frozen — see [discipline/frozen_eval_rules.md](discipline/frozen_eval_rules.md).
- Stage gates (v0.x kernel) — see [discipline/stage_gates_v0.md](discipline/stage_gates_v0.md).

Current benchmarks:

- [TagWorld-lite](benchmarks/tagworld_lite/gate.md) — promoted v1.1.0
- [TagWorld tool-action](benchmarks/tagworld_tool_action/README.md) — v1.1.1 (open)
- [Minesweeper-lite](benchmarks/minesweeper_lite/gate.md) — draft
- [ARC-lite](benchmarks/arc_lite/gate.md) — draft

---

## Frozen Evaluation Rule

```text
Do not tune on frozen evals.
Do not edit frozen evals after seeing engine behavior.
Do not train or specialize the graph on frozen evals.
```

Use `evaluation/dev/` for debugging. Use `evaluation/frozen/` for reporting.

---

## Negative Results

Failed experiments are valuable. A **Kill** must explain what failed, which trace revealed it, and whether the idea is dead or only the implementation.

Do not repeat killed experiments with tiny cosmetic changes.

See [discipline/experiment_lifecycle.md](discipline/experiment_lifecycle.md).

---

## Coding Agents

Agents are allowed. Architecture drift is not. Tasks must be small; reject code that allocates in the hot loop, mutates during propagation, or skips traces.

See [discipline/architecture_guardrails.md](discipline/architecture_guardrails.md#coding-agent-rules).

---

## Final Rule

Nerva is allowed to be simple.

It is not allowed to be opaque.

If the engine cannot explain what fired, what path carried the signal, what changed, and why — the result does not count.

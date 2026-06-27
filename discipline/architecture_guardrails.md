# Architecture guardrails

Hard constraints unless an experiment explicitly tests a violation.

Hot-loop specifics: [hot_loop_rules.md](hot_loop_rules.md).

## Mutation rules

Graph mutation must go through the mutation queue.

```text
Propagation decides what happened.
Mutation applies later.
```

No direct edge creation, edge deletion, schema promotion, or permanent memory write inside propagation.

## Sparse behavior rule

Work must be proportional to active events and active edges, not total graph size.

Any experiment that scans all nodes or all edges must label that scan as:

```text
debug-only
offline consolidation
benchmark setup
temporary implementation debt
```

## Debug rule

Every mechanism must be inspectable.

If a feature changes behavior, it must produce a debug record showing:

```text
what changed
why it changed
which trace/path caused it
which tick it happened on
```

## Coding agent rules

Coding agents are allowed. Architecture drift is not.

Every coding-agent task must be small.

Bad prompt:

```text
Build Nerva.
```

Good prompt:

```text
Implement only the fixed-size event queue in C11.
No malloc in push/pop.
Include overflow handling.
Include tests.
Do not touch node activation.
```

Before accepting generated code, check:

```text
Does it allocate in the hot loop?
Does it hide state behind unnecessary abstraction?
Does it scan the whole graph unnecessarily?
Does it mutate graph structure during propagation?
Does it skip trace logging?
Does it introduce threads before the single-threaded engine is correct?
Does it make the demo pass by hardcoding names?
```

If yes, reject or rewrite.

## Non-goals for early Nerva

Do not implement during early v0.x unless explicitly promoted by roadmap:

```text
full natural language parser
GUI
network services
plugin system
multithreading
GPU path
large corpus ingestion
autonomous external actions
full directive/governor layer
robotics tenets layer
complex schema language
```

The early engine should stay small enough to understand.

## Parameter discipline

Parameters must be declared in one place.

Suggested file:

```text
include/nerva_params.h
```

Each parameter must have:

```text
name
default value
unit or fixed-point format
purpose
runtime-tunable or compile-time
safe range, if known
```

Do not tune parameters directly on frozen evals. See [frozen_eval_rules.md](frozen_eval_rules.md).

Any parameter change must be reported with:

```text
old value
new value
reason
experiments affected
result before/after
```

If a result only works after narrow hand-tuning, say so.

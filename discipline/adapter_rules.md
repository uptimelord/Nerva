# Adapter rules

Conventions for connecting **worlds** (userland environments) to **Nerva core** (`src/`, `include/`).

## Kernel / userland split

```text
Nerva Core     src/include     conductance graph, events, mutation, prediction
Worlds         worlds/         simulators, affordances, replay formats
Benchmarks     benchmarks/     gates and configs — not sim code
Experiments    experiments/    evidence from specific runs
Runs           runs/           disposable logs
```

## World adapter requirements

A world adapter should:

- Emit **actual** world events through Nerva APIs (no fake confirms)
- Never mutate the graph outside the mutation queue
- Never malloc in the hot path bridged to engine ticks
- Expose affordance-masked actions — simulator must not oracle the winning action
- Support deterministic reset from `(seed, episode)` for replay
- Build frames/traces compatible with [trace_requirements.md](trace_requirements.md)

## CLI wrappers

Keep CLIs thin in `tools/`:

```text
tools/<world>_cli.c   → parse args, init engine, call worlds/<world>/
```

World logic lives in `worlds/<world>/`, not in `tools/` or `experiments/`.

## Benchmark adapter requirements

A benchmark gate in `benchmarks/<name>/gate.md` should:

- Reference the world path (`worlds/...`)
- Define Promote/Kill before results
- Specify seeds, episode counts, and baselines
- List unit tests that enforce the gate
- Define benchmark-specific metrics in `metrics.md` when global reporting fields are insufficient

## Visualization

Terminal viz and browser replay viewers are **observe-only** — they must not change learning or simulator state.

Replay JSONL is evidence format; live viz parity with fast mode should be testable.

## Future worlds

When adding Minesweeper, ARC-lite, Chesslets, etc.:

1. Create `worlds/<name>/` with sim + Nerva bridge
2. Create `benchmarks/<name>_lite/gate.md`
3. Record promote/kill in `experiments/<version>_<name>_<decision>/`
4. Do not add world-specific gates to root `DISCIPLINE.md`

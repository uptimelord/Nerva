# Nerva

A cognitive graph engine written in C11. Nerva models knowledge as a sparse
activation network where nodes fire, propagate weighted signals through edges,
form traces of recent activity, learn from feedback, induce schemas, consolidate
memories, and resolve exceptions — all within a deterministic, fixed-pool,
single-threaded tick loop.

## Quick Start

```powershell
# Build (requires GCC or MSVC; see bootstrap below)
.\build.bat          # or: powershell .\build.ps1

# Run the test suite (84 tests across 12 modules)
.\build\test_runner.exe

# Run the benchmark suite (9 cognitive tasks)
.\build\nerva_bench.exe

# Run a script
.\build\nerva_cli.exe examples\poodle.nerva
```

### Toolchain Bootstrap (Windows, one-time)

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-toolchain.ps1
```

This downloads a repo-local MinGW-w64 toolchain (~900 MB, gitignored). After
that, `build.bat` / `build.ps1` will find it automatically.

## Architecture

```
nerva_engine_init → [nerva_tick] → nerva_engine_free
                         │
         ┌───────────────┼──────────────────┐
         ▼               ▼                  ▼
    event_pop    prediction_expire    routing_on_tick
         │                                  │
         ▼                                  ▼
    apply_event → exception_suppress  memory_on_tick
         │
         ▼
    trace_record → should_fire → fire_node
                                     │
                         ┌───────────┴────────┐
                         ▼                    ▼
                 prediction_mode       normal propagation
                 (pre-charge)          (push events)

    post-tick: leak on active set, trace decay
```

Zero heap allocation inside the tick loop. All storage is pre-allocated pools.
Mutations (weight changes, new edges, gate closes) are queued and applied
outside the hot path.

## Command Language

Scripts are line-oriented text files (`.nerva`):

```text
NODE poodle
NODE dog
NODE animal
EDGE poodle kind_of dog
EDGE dog kind_of animal
ACTIVATE poodle
TICK 4
```

Commands: `NODE`, `EDGE`, `ACTIVATE`, `TICK`, `QUERY`, `FEEDBACK`, `BLOCK`,
`APPLY`, `SAVE`, `LOAD`, `SCHEMA OBSERVE`, `SCHEMA APPLY`.

## Modules

| Module | Header | Purpose |
|--------|--------|---------|
| Graph | `nerva_graph.h` | Node/edge creation, CSR adjacency, reachability |
| Event | `nerva_event.h` | Binary min-heap, activation application, firing |
| Engine | `nerva_engine.h` | Tick loop, init/free, tick_n |
| Trace | `nerva_trace.h` | Ring buffer recording, decay, path lookup |
| Mutation | `nerva_mutation.h` | Deferred FIFO for weight/gate/edge changes |
| Learning | `nerva_learning.h` | Feedback → trace scan → mutation queue |
| Prediction | `nerva_prediction.h` | Next-event prediction, pre-charge, confirm/miss |
| Exception | `nerva_exception.h` | Blocker edges, contradiction suppression |
| Schema | `nerva_schema.h` | Triple observation, candidate promotion, apply |
| Memory | `nerva_memory.h` | Episodic blocks, consolidation, forgetting |
| Routing | `nerva_routing.h` | Fluid/crystallized split, competition, query focus |
| Parse | `nerva_parse.h` | Line-oriented command parser |
| Persist | `nerva_persist.h` | Binary save/load with versioned header + CRC |
| Bench | `nerva_bench.h` | 9-benchmark cognitive task suite |

## Benchmark Results (v1.0)

| Benchmark | Result |
|-----------|--------|
| Few-shot relation | N=5 held-out schema apply passes |
| Transitive reasoning | C fires in 3 ticks (limit 100) |
| Exception handling | bird→fly 20/20, penguin blocked 5/5 |
| Container movement | located_at inferred, shelf in 2 ticks |
| Ambiguity resolution | 3:1 bias wins 5/5 within 50 ticks |
| Memory consolidation | episode retrievable 4/4 nodes |
| Forgetting | unreinforced episode decays to deletion |
| Contradiction repair | blocker trace ≤2 ticks, fly suppressed |
| Compute efficiency | peak_queue=2, RAM=0.89 MB, 10k ticks/sec |

## Project Structure

```
Nerva/
├── include/          C headers (public API)
├── src/              Implementation
├── tests/            Unit tests (12 modules)
├── tools/            CLI and benchmark executables
├── examples/         .nerva demo scripts
├── experiments/      Per-version gate documents and results
├── scripts/          Toolchain bootstrap
├── build.bat         Windows build entry point
├── build.ps1         PowerShell build script
└── Makefile          POSIX build (gcc/clang)
```

## Design Principles

- **No allocation in the hot loop.** All pools are sized at init.
- **Sparse active-set processing.** Only nodes that received events are touched
  per tick.
- **Deferred mutation.** The graph is never modified during propagation.
- **No trace, no trust.** Every experiment produces tick-level fire evidence.
- **Write before running.** Promote/kill criteria defined before code is written.
- **Fixed-point arithmetic.** Activations and weights use Q8.8 for determinism.

## License

Licensed under the [Apache License, Version 2.0](LICENSE) (`SPDX-License-Identifier: Apache-2.0`).

See [NOTICE](NOTICE) for attribution. Source files include SPDX license headers.

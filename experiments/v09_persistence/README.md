# v0.9 Save / Load Persistence

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- `nerva_persist_save` / `nerva_persist_load` round-trip nodes, edges, names, tick, memory blocks, and schemas
- loaded graph rebuilds adjacency before reachability or propagation
- post-load poodle propagation matches pre-save fire path in `trace.log`
- CRC32 and header validation reject truncated or corrupted files
- `SAVE` / `LOAD` parser commands call persist APIs (no duplicate serialization)
- save/load perform no file I/O inside the hot tick loop
- invalid paths or oversize snapshots fail without corrupting engine state

**Kill if:**

- load leaves stale CSR adjacency or wrong reachability
- checksum/header errors load partial graph state anyway
- runtime queues (events, mutations, traces) leak into snapshot or restore incorrectly
- round-trip changes edge weights, schema flags, or memory charge silently
- save/load bypasses caps or writes beyond allocated engine storage

## Verdict

**Promote** — 7/7 persist tests pass; poodle round-trip, header+payload CRC rejection, atomic failed-load, memory/schema round-trip, feedback weights, and post-load trace match. Evidence: [results.md](results.md), `trace.log`, `persist.log`.

## Setup

stage: v0.9  
graph: poodle `kind_of` chain with optional feedback weight delta  
snapshot: `experiments/v09_persistence/roundtrip.nerva` (gitignored)  
parameters: `nerva_config_test()`  
format: little-endian `Nervav001` header + payload CRC32

## Commands

```text
BUILD: NODE/EDGE poodle graph, ACTIVATE poodle, TICK 4, optional FEEDBACK+APPLY
SAVE: nerva_persist_save -> roundtrip.nerva
LOAD: fresh engine -> nerva_persist_load
VERIFY: reachability, edge weights, activate poodle TICK 4 -> same fire path
CORRUPT: flip byte / truncate -> load returns error, engine unchanged
```

## Expected trace

```text
pre-save trace.log: tick 0 poodle, tick 1 dog, tick 2 animal
post-load trace.log: identical fire sequence after same ACTIVATE/TICK
persist.log: node/edge/memory/schema counts, load ok, crc validated
```

Run: `.\build.bat` (includes unit tests).

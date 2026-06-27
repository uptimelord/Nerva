# v0.6 Memory Consolidation

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- episodic memory blocks can be opened and closed around a query episode
- open episodes defer idle consolidation until `nerva_memory_end_episode()`
- charge updates accumulate useful/surprise/repetition/correction signals
- consolidated state reached when charge exceeds store threshold
- idle consolidation runs only when the event queue stays empty for `idle_consolidate_ticks`
- charge decays during idle consolidation, not during active propagation ticks
- useful high-charge episodes persist across consolidation passes
- low-charge episodes are not marked delete before the hold period elapses
- low-charge episodes are marked delete only after sustained low charge beyond hold period
- replay placeholder selects top-K charged episodes and records replay (no graph mutation in tick)
- used-path episode visible in `trace.log`

**Kill if:**

- memory logic mutates graph edges during propagation tick
- forgetting deletes blocks immediately on first sub-threshold decay
- consolidation runs every tick regardless of queue activity
- open episodes decay or forget before episode close
- charge inflation has no access/update path tied to episode reuse
- replay placeholder pushes live propagation events in v0.6

## Verdict

**Promote** — 9/9 memory tests pass; episode charge/consolidation, open-episode guard, hold-period forget, idle-only consolidate, and replay placeholder. Evidence: [results.md](results.md), `trace.log`, `memory.log`.

## Setup

stage: v0.6  
graph: poodle two-hop query for useful episode; separate unused low-charge episode  
parameters: `nerva_config_test()` with lowered memory thresholds and fast idle/hold for tests  
observe: begin/end episode around query; charge useful episode on reuse  
idle: empty event queue for `idle_consolidate_ticks` to trigger consolidation

## Commands

```text
EPISODE_A: begin -> activate poodle -> tick -> end -> charge (reuse)
EPISODE_B: begin -> end with low initial charge -> leave idle
IDLE: tick until consolidation passes run
CHECK: A consolidated and not marked delete; B not marked before hold; B marked after hold
REPLAY: top-K replay counter increments on idle consolidate
```

## Expected trace

```text
useful episode charge rises above store threshold -> consolidated
idle consolidate decays unused episode charge
forget mark only after hold_period with charge below forget threshold
trace.log: poodle->dog->animal used-path hops from useful episode
memory.log records block charge/state snapshots
```

Run: `.\build.bat` (includes unit tests).

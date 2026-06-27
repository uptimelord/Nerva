# Results: v0.6 Memory Consolidation

## Decision Rule

See [README.md](README.md).

## Setup

commit: 5ef64f2  
date: 2026-06-27  
stage: v0.6  
graph: poodle two-hop query (useful episode); separate low-charge unused episode  
parameters: `nerva_config_test()` (`memory_store_threshold=5`, `memory_forget_threshold=1`, `hold_period_ticks=20`, `idle_consolidate_ticks=2`, `memory_decay_per_idle=0.5`)  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_memory.c`:

- `test_memory_episode_captures_traces`
- `test_memory_charge_consolidates`
- `test_memory_useful_persists`
- `test_memory_forget_requires_hold_period`
- `test_memory_idle_skips_when_events_pending`
- `test_memory_replay_top_k`
- `test_memory_open_episode_defers_consolidation`
- `test_memory_low_charge_since_at_tick_zero`
- `test_memory_experiment_artifacts`

Build: `.\build.bat`

Memory artifact: `experiments/v06_memory_consolidation/memory.log`  
Trace artifact: `experiments/v06_memory_consolidation/trace.log`

## Results

task_success_rate: 9/9 memory tests + 8/8 schema + 5/5 exception + 6/6 prediction + 7/7 learning + 4/4 trace + 8/8 event + 9/9 graph (56 total)  
useful_episode_charge: 7.0 at store (6 useful + 1 repetition) -> consolidated  
low_charge_episode: surprise-only charge (no USEFUL flag); replay ranks by charge only  
unused_episode_forget: marked delete only after hold period with sustained low charge  
memory_consolidations: >= 1 per idle cycle under test  
memory_replayed: >= 1 (top-K placeholder on useful episode)  
variance/noise: deterministic run, no random seed used

## Trace Summary

```text
useful episode: activate poodle -> 2 used-path traces captured -> charge 7 -> consolidated
idle consolidate: decays unused episode; replay selects high-charge block
forget: low_charge_since tracked; mark delete only after hold_period_ticks
no consolidation while event queue non-empty
```

## Decision

Promote

## Notes

- Consolidation runs from `nerva_memory_on_tick_end` only when the event queue is empty for `idle_consolidate_ticks`.
- `nerva_consolidate_idle` returns early while any episode remains open (`NERVA_MEM_STATE_OPEN`).
- `low_charge_since` uses `NERVA_MEM_LOW_CHARGE_UNSET`; tick 0 is a valid low-charge start tick.
- Charge decay and forget pass run inside `nerva_consolidate_idle`, not during propagation.
- Replay placeholder flags top-K blocks by charge and increments debug counter; no live propagation events in v0.6.

# Results

Date: 2026-06-28  
Machine: Windows 10 (win32 10.0.19044), gcc WinLibs UCRT  
Build flags: `-std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude -Itools`  
Seeds: 1 (primary), 5, 11 (action cross-check)  
Episodes: 1000 (observer/prediction/action), 100000 (fast stress), 100 (action seeds 5/11)  

## Commands

```powershell
.\build.bat                     # 98 tests — All tests passed
.\build\nerva_tagworld.exe --episodes 1000 --mode observer --seed 1 --fast
.\build\nerva_tagworld.exe --episodes 1000 --mode prediction --seed 1 --fast
.\build\nerva_tagworld.exe --episodes 1000 --mode action --seed 1 --fast --baseline
.\build\nerva_tagworld.exe --episodes 100 --mode action --seed 5 --fast --baseline
.\build\nerva_tagworld.exe --episodes 100 --mode action --seed 11 --fast --baseline
.\build\nerva_tagworld.exe --episodes 100000 --mode observer --seed 1 --fast
.\build\nerva_tagworld.exe --episodes 1 --mode action --seed 1 --viz --write-replay learned_block_replay.log
```

Artifacts (local, not committed):  
`experiments/v11_tagworld_lite/run_*.log`, `learned_block_replay.log`

## Metrics

### Observer (1000 ep, seed 1, fast)

| Metric | Value |
|--------|------:|
| escape_rate | 0.6670 |
| caught | 333 |
| timeouts | 0 |
| avg_ticks_per_episode | 6.00 |
| avg_events_per_episode | 12.01 |
| avg_mutations_per_episode | 4.67 |
| total_mutations_applied | 4665 |
| max_event_queue_depth | 4 |
| max_mutation_queue_depth | 0 |
| learned_edge_count | 8 |

Variant mix ~⅓ each: variant 0 (open doorway, wait) → caught; variants 1/2 (block / push-then-run) → escaped.

### Prediction (1000 ep, seed 1, fast)

| Metric | Value |
|--------|------:|
| escape_rate | 0.6670 |
| caught | 333 |
| prediction_confirm_count | 5002 |
| prediction_miss_count | 1001 |
| surprise_count | 1001 |
| avg_mutations_per_episode | 6.00 |
| total_mutations_applied | 6003 |
| max_mutation_queue_depth | 0 |
| learned_edge_count | 8 |

Confirms align with doorway state (BLOCK_AT_DOORWAY→PATH_BLOCKED or DOORWAY_OPEN→PATH_OPEN); misses on variant-0 open-door episodes where only one path event resolves per tick.

### Action (1000 ep, seed 1, fast, --baseline)

| Metric | Value |
|--------|------:|
| escape_rate | 1.0000 |
| baseline_escape_rate (random valid action) | 0.3930 |
| margin vs random | +60.7 pp |
| action_run_count | 7000 |
| action_wait_count | 268 |
| action_push_doorway_count | 0 |
| avg_ticks_per_episode | 7.27 |
| max_event_queue_depth | 6 |
| learned_edge_count | 9 |

### Action cross-seeds (100 ep, fast, --baseline)

| seed | escape_rate | baseline_escape_rate | margin | action_run | action_wait |
|-----:|------------:|---------------------:|-------:|-----------:|------------:|
| 5 | 1.0000 | 0.3900 | +61.0 pp | 700 | 29 |
| 11 | 1.0000 | 0.3000 | +70.0 pp | 700 | 27 |

Trained policy beats random valid-action baseline by ≥20 pp on all three seeds.

### Fast stress (100000 ep, observer, seed 1)

| Metric | Value |
|--------|------:| 
| escape_rate | 0.6666 |
| caught | 33335 |
| timeouts | 0 |
| avg_mutations_per_episode | 4.67 |
| total_mutations_applied | 466675 |
| wall time | ~14 s |
| viz_frames | 0 |

## Trace excerpt

Action-mode learned block escape (seed 1, episode 0, variant 1 — block pre-placed at doorway):

```text
[t=0 episode=0]
ACTIVE: BLOCK_AT_DOORWAY, PATH_BLOCKED
ACTION: ACTION_RUN_TO_SAFE

[t=6 episode=0]
ACTIVE: SEEKER_NEAR_RUNNER, BLOCK_AT_DOORWAY, PATH_BLOCKED
ACTION: ACTION_RUN_TO_SAFE
OUTCOME: RUNNER_ESCAPED
```

Runner routes around the blocked chokepoint via BFS while seeker remains delayed at the doorway.

## Closed loop (what v1.1 proved)

```text
world event
→ expectation
→ actual outcome
→ confirm/miss
→ mutation
→ changed future behavior
```

Nerva can operate inside a tiny world: observe events, predict futures, confirm or miss, learn from outcomes, beat a nontrivial baseline, and produce a replayable trace.

**Strongest numbers:**

| Run | Result |
|-----|--------|
| Prediction 1k | 5002 confirms / 1001 misses |
| Action 1k vs random | 100% vs 39.3% |
| Action seeds 5 / 11 | 100% vs 39% / 30% |
| Observer 100k | ~14 s, 98/98 tests |

## Decision

**Promote — v1.1.0 TagWorld-lite**

**Evidence:**
- Observer learns BLOCK_AT_DOORWAY→PATH_BLOCKED; 333/1000 caught on variant 0 (open doorway + wait) with zero timeouts.
- Prediction bulk run: 5002 confirms, 1001 misses/surprises; metrics use `total_mutations_applied` (no overflow).
- Action mode escape rate exceeds random valid-action baseline by ≥20 pp on seeds 1, 5, and 11.
- 98/98 unit tests pass, including direct confirm/mismatch pair tests and corridor catch-pressure tests.
- Fast 100k observer completes without crash; browser replay viewer + trace for block-at-doorway escape.

## Claims (disciplined)

**Claim:**
> Nerva learned predictive/outcome structure in TagWorld-lite and learned an action policy that beats random under a nontrivial catch/escape environment. The block-at-doorway causal path is represented and replayable.

**Do not claim yet:**
> Nerva independently discovered block-pushing tool use in bulk action mode.

Bulk action currently favors `RUN_TO_SAFE` after pretrain; variant 2 push-to-doorway is exercised in observer mode but not selected at scale in action runs.

**Next gate:** [v1.1.1 Tool-Action Pressure](../v11_1_tool_action_pressure/README.md)

## Fixes applied (blocker pass)

| Blocker | Fix |
|---------|-----|
| 0 prediction confirms | Stop activating predicted targets before confirm; inject ACTUAL edge only |
| Runner never caught | Catch on adjacent seeker (manhattan ≤ 1); corridor map + variant 0 wait script |
| Action vs random tied at 100% | BFS run-to-safe; random baseline via valid-action mask; pretrain path_open→wait |
| Mutation metric overflow | Track `debug.mutations_applied` delta; separate queue depth from applied total |

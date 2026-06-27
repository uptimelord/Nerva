# Results: v0.8 Simple Command Language

## Decision Rule

See [README.md](README.md).

## Setup

commit: 6872156  
date: 2026-06-27  
stage: v0.8  
scripts: `examples/poodle.nerva`, `examples/penguin.nerva`  
parameters: `nerva_config_test()` in unit tests  
seed: deterministic, no random seed

## Commands

Unit tests in `tests/test_parse.c`:

- `test_parse_blank_and_comment`
- `test_parse_invalid_command`
- `test_parse_rejects_extra_tokens`
- `test_parse_script_poodle_propagation`
- `test_parse_script_penguin_blocker`
- `test_parse_query_feedback_mutations`
- `test_parse_experiment_artifacts`

Build: `.\build.bat`  
CLI: `build/nerva_cli.exe examples/poodle.nerva`

Script artifact: `experiments/v08_command_language/script.log`  
Trace artifact: `experiments/v08_command_language/trace.log`

## Results

task_success_rate: 7/7 parse tests + 9/9 routing + 9/9 memory + 8/8 schema + 5/5 exception + 6/6 prediction + 7/7 learning + 4/4 trace + 8/8 event + 9/9 graph (72 total)  
poodle_script: dog and animal fire; reachability poodle→animal true  
penguin_script: blocker applied via APPLY; exception suppression on penguin path  
feedback_script: QUERY + FEEDBACK correct + APPLY strengthens used edge  
invalid_commands: rejected without graph mutation (including extra tokens)  
variance/noise: deterministic run, no random seed used

## Trace Summary

```text
poodle.nerva: NODE/EDGE/ACTIVATE/TICK -> two-hop propagation
penguin.nerva: BLOCK + APPLY before ACTIVATE penguin -> fly suppressed
inline QUERY/FEEDBACK/APPLY -> mutation queue write-back
script.log: nodes/edges/fires summary from poodle script
```

## Decision

Promote

## Notes

- Parser is line-oriented only; `#` comments and blank lines ignored.
- `EDGE`/`NODE` construct graph state directly (setup); `BLOCK`/`FEEDBACK` queue mutations; `APPLY` drains.
- `QUERY` wraps routing begin + activate; `ACTIVATE` is activation-only for legacy scripts.
- Lines with extra tokens after the required arguments are rejected at parse time.

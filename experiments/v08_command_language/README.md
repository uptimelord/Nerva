# v0.8 Simple Command Language

## Decision Rule

Write before running. Verdict after evidence goes in [results.md](results.md).

**Promote if:**

- line-oriented commands parse: `NODE`, `EDGE`, `QUERY`, `ACTIVATE`, `FEEDBACK`, `BLOCK`, `TICK`, `APPLY`, `SAVE`, `LOAD`, `SCHEMA`
- comments (`#`) and blank lines are ignored
- `examples/poodle.nerva` builds graph and propagates poodle→dog→animal
- `examples/penguin.nerva` with `APPLY` after `BLOCK` suppresses penguin→fly path
- commands call existing engine APIs (no duplicate graph logic in parser)
- `EDGE`/`NODE` are direct graph setup (adjacency rebuild deferred until `ACTIVATE`/`QUERY`/`TICK`); `BLOCK`/`FEEDBACK` queue mutations; `APPLY` drains the queue
- `QUERY`/`ACTIVATE`/`BLOCK`/`SCHEMA APPLY` require pre-declared nodes (no auto-create on query path)
- invalid commands fail without mutating engine state
- `examples/poodle.nerva` saves tick fire path and edge traces to `trace.log`

**Kill if:**

- parser embeds hand-coded demo graphs bypassing command input
- full NLP / free-text parsing scope creep
- commands mutate graph directly during propagation tick
- scripts cannot reproduce poodle or penguin gate behaviors
- silent failure on malformed input

## Verdict

**Promote** — 7/7 parse tests pass; poodle/penguin scripts and QUERY/FEEDBACK/APPLY drive existing engine APIs. Evidence: [results.md](results.md), `trace.log`, `script.log`, `build/nerva_cli.exe`.

## Setup

stage: v0.8  
scripts: `examples/poodle.nerva`, `examples/penguin.nerva`  
parameters: `nerva_config_test()` in unit tests; default config in CLI  
runner: `nerva_parse_run_file` / `tools/nerva_cli.c`

## Commands

```text
POODLE: NODE/EDGE/ACTIVATE poodle, TICK 4 -> dog and animal fire
PENGUIN: EDGE + BLOCK + APPLY, ACTIVATE bird/penguin, TICK 4
FEEDBACK: QUERY/ACTIVATE, TICK, FEEDBACK correct, APPLY -> weight delta
```

## Expected trace

```text
poodle script: two-hop propagation, reachability true
trace.log: tick fires + edge traces from poodle.nerva script run
penguin script: bird reaches fly; penguin path blocked after APPLY
script.log records node/edge/fire summary from poodle script
```

Run: `.\build.bat` (includes unit tests).

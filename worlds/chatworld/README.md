# ChatWorld

Language-game world for Nerva.

`worlds/chatworld/` owns the simulator, surface adapter, graph binding/action/output
affordances, memory table, and trace-backed feedback loop. CLI parsing stays in
`tools/chatworld_cli.c`.

## v1.4 Claim

ChatWorld v1.4 is a bounded surface-text circuit:

```text
surface tokens -> binding nodes -> MEM_WRITE/MEM_READ/ACTION nodes -> OUTPUT_TOKEN nodes
```

It supports a small dialogue game where learned surface traces can:

- bind key/value positions in fixed-form text
- write and read memory through fired graph paths
- render only fired `OUTPUT_TOKEN:*` nodes
- produce learned greetings, acknowledgements, and supported unknown responses
- correct a prior remembered value through later trace-backed support
- learn small write-phrase chunks such as `I am called X` and `call me X` as
  alternate ways to bind the same memory key

The C host may emit mechanical surface events, run Nerva ticks, read fired
action/output nodes, execute the fired memory path, render fired output tokens, and
report `NO_SUPPORTED_RESPONSE` or `CONTRADICTION_OR_AMBIGUOUS_RESPONSE`.

The C host must not rank candidates, choose answer content, choose response labels,
fall back to unknown, create graph nodes during frozen eval, or emit supervision labels
as events.

## Boundaries

Supported:

- bounded same-template memory heartbeat, for example `my name is Ada` followed by
  `what is my name`
- held-out entities on learned surface forms
- learned unknown only when `ACTION:RESP_UNKNOWN` fires
- trace-backed correction behavior
- learned write paraphrases for the identity key, limited to trained chunks

Not supported:

- open-domain conversation
- semantic parsing
- embeddings, transformers, neural networks, or runtime LLM/API teachers
- broad paraphrase equivalence beyond the trained chunks
- unrestricted free-form generation

Datasets live under `datasets/`. Benchmark gates live in `benchmarks/chatworld_lite/`
until the benchmark folder is renamed for v1.4.

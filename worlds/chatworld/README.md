# ChatWorld

Language-game world for Nerva.

`worlds/chatworld/` owns the simulator, surface tokenizer, candidate action generation, response
frame selection, memory table, and trace-backed feedback loop. CLI parsing stays in
`tools/chatworld_cli.c`.

The first implementation is ChatWorld-lite:

- surface tokens only
- response-frame actions
- mechanical candidate memory write/read actions
- zero-weight policy edges at start
- no fallback responder
- no runtime LLM/API teacher
- train/dev/frozen TSV datasets under `datasets/`

Benchmark gates live in `benchmarks/chatworld_lite/`.

# ChatWorld-lite

Small language-game benchmark for Nerva dialogue behavior.

The adapter emits surface text events and mechanically generated candidate actions only. It must not
emit intent labels, slot labels, semantic parses, correct answer labels, or fallback responses.

Initial target:

```powershell
.\build\nerva_chatworld.exe --train --eval --seed 1
```

Stable gate: `benchmarks/chatworld_lite/gate.md`.

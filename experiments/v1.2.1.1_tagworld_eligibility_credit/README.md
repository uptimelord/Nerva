# v1.2.1.1 TagWorld Eligibility Credit

**Base:** `v1.2.1`  
**Status:** See [results.md](results.md)

## Target

Delayed sequence credit through **observed intermediate consequences** — not blanket final-outcome
credit on every action, and no oracle `train_pair` chains.

## Claim (when promoted)

Pure-feedback tool-action acquisition on held-out pressure map G via eligibility rules:

```text
PUSH + BLOCK_AT_CHOKEPOINT     -> strengthen context->PUSH (half LTP)
PATH_BLOCKED + RUN + ESCAPED   -> strengthen PATH_BLOCKED->RUN (full LTP)
WAIT + no improvement + TIMEOUT -> weaken context->WAIT
RUN + no escape                -> weaken PATH_BLOCKED->RUN
```

## Reproduce

```powershell
./build.ps1
./experiments/v1.2.1.1_tagworld_eligibility_credit/commands.sh
```

Use `--pure-feedback` with `--generalization --eval-map G`.

## Gate

See [gate.md](gate.md).

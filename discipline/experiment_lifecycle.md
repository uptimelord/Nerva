# Experiment lifecycle

Every benchmark run and experiment record should have an explicit lifecycle state.

## States

### Draft

- Idea or benchmark sketch exists
- Gate may be incomplete
- Results are not trusted for claims

### Repeat

- Runs executed; evidence collected
- One or more **blockers** remain (metrics, traces, tests, or gate criteria)
- Document blockers in `results.md`

### Promote

- All gate criteria in `benchmarks/<name>/gate.md` passed
- Claim discipline filled in (supported / not supported / evidence / residuals)
- Suitable for tag, milestone, or external citation

### Kill

- Gate **Kill if** criteria met, or hypothesis falsified
- Must document: what failed, trace that revealed it, idea dead vs implementation fixable
- Do not hide killed runs

### Archive

- No longer active benchmark or superseded experiment
- Kept for historical evidence only
- Link to replacement benchmark if any

## Transitions

```text
Draft → Repeat → Promote
              ↘ Kill
Promote / Kill / Repeat → Archive (when superseded)
```

## results.md requirement

Every experiment `results.md` must end with:

```text
## Decision

Draft | Repeat | Promote | Kill | Archive

## Claims (required for Promote)

Supported:
Not supported:
Evidence:
Residuals:
```

Repeat and Kill should still note partial claims and blockers.

## Negative results

Killed experiments are valuable. Explain:

```text
what failed
how it failed
which trace revealed the failure
whether the idea is dead or just the implementation
what to avoid next time
```

Do not repeat killed experiments with tiny cosmetic changes.

# Hot loop rules

Hard constraints for the event/tick propagation loop unless an experiment explicitly tests a violation.

## Forbidden inside the hot loop

```text
malloc/free
file I/O
graph-wide scans
schema induction
memory consolidation
large debug string construction
graph mutation
```

## Allowed inside the hot loop

```text
pop due event
load target node
apply signal
apply leak/decay
check refractory
check threshold
fire node
walk outgoing edge list
push new events
record trace
queue mutation request
increment counters
```

See also [architecture_guardrails.md](architecture_guardrails.md) for mutation queue rules and sparse behavior.

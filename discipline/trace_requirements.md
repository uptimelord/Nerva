# Trace requirements

## Prime rule (reminder)

```text
No trace, no trust.
```

## Successful experiment must show

```text
input command
event path
nodes fired
edges used
traces written
mutations queued
memory changes, if any
pass/fail result
```

## Storage

Suggested locations:

```text
runs/<world>/run_*.log          raw CLI output (gitignored)
experiments/<name>/trace_excerpt.log   committed excerpt for evidence
experiments/<name>/trace.log           full trace when small enough
```

For kernel-stage experiments, legacy path still valid:

```text
experiments/<experiment_name>/trace.log
```

## Minimum trace fields

```text
tick
event_id
source_node
target_node
relation
signal
target_activation_before
target_activation_after
threshold
fired_or_not
outgoing_events_created
trace_tag
mutation_queued
```

## Debug rule

Every mechanism that changes behavior must produce a debug record showing:

```text
what changed
why it changed
which trace/path caused it
which tick it happened on
```

## Minimal viable engine checklist

First meaningful Nerva heartbeat:

```text
create three nodes
connect two edges
activate first node
tick engine
observe activation path
record trace
print path
```

Required output:

```text
poodle -> dog -> animal
```

Do not move to advanced mechanisms until this is boring and reliable.

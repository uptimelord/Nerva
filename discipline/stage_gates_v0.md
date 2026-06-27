# Stage gates — v0.x kernel

Each stage must pass its own tests before the next stage begins.

These are **kernel** gates. World benchmarks (TagWorld, Minesweeper, etc.) live in `benchmarks/*/gate.md`.

---

## v0.0 Static Graph

**Goal:** create nodes; create edges; traverse reachable path.

**Promote if:**

```text
nodes have stable IDs
edges connect correct source and target
reachable path is correct
test graph is not hardcoded into traversal
```

**Kill if:**

```text
node IDs change unexpectedly
edge traversal depends on string names during runtime
graph storage cannot be inspected
```

---

## v0.1 Event Propagation

**Goal:** activation moves through directed edges over ticks.

**Required demo:**

```text
NODE poodle
NODE dog
NODE animal
EDGE poodle kind_of dog
EDGE dog kind_of animal
ACTIVATE poodle
TICK 4
TRACE
```

**Expected trace:**

```text
tick 0: poodle fired
tick 1: dog fired
tick 2: animal fired
path: poodle -> dog -> animal
```

**Promote if:**

```text
events arrive in timestamp order
nodes fire only after threshold crossing
outgoing edges create new events
trace shows the active path
```

**Kill if:**

```text
the system only works for the poodle demo
event order is nondeterministic without explanation
queue overflow is silent
```

---

## v0.2 Trace Buffer

**Goal:** recently active paths are recorded for later learning.

**Promote if:**

```text
used edges receive trace records
trace decay matches expected tolerance
trace output identifies the active path
```

**Kill if:**

```text
trace records are missing or unbounded
trace decay requires scanning all nodes every tick in routine propagation
feedback cannot find the used path
```

---

## v0.3 Feedback Write-Back

**Goal:** correct feedback strengthens used edges; wrong feedback weakens or gates used edges.

**Promote if:**

```text
correct feedback increases used edge strength
wrong feedback decreases or gates used edge strength
unused edges are not changed
all changes are logged through the mutation system
```

**Kill if:**

```text
feedback changes unrelated edges
edge weights change inside propagation
repeated feedback causes runaway values without clipping
```

---

## v0.3.5 Next-Event Prediction

**Goal:** after repeated A then B, activating only A marks B as expected without treating expectation as fact.

**Promote if:**

```text
A alone produces B expected (pre-charge below fire threshold, B does not fire from prediction)
actual B confirms prediction and strengthens the predicting edge through mutation log
actual C misses prediction, weakens/gates predicting edge, surprise trace visible
expected events are tagged separately from actual propagation
confirm/miss write-back uses mutation queue (not inline graph edits during propagation)
```

**Kill if:**

```text
expectation equals propagation (B fires from prediction alone)
predictions not visible in trace.log
confirm/miss bypasses mutation system
prediction scan walks all nodes every tick
```

---

## v0.4 Exception and Contradiction Handling

**Goal:** exceptions block inherited/default paths when context requires it.

**Required demo:**

```text
bird usually_has_property fly
penguin kind_of bird
penguin blocks fly
```

**Promote if:**

```text
bird activates fly
penguin activates bird
penguin blocks fly
contradiction/blocker trace is visible
```

**Kill if:**

```text
blockers suppress unrelated properties
exceptions require hardcoded animal names
contradiction handling permanently deletes useful defaults
```

---

## v0.5 Schema Induction

**Goal:** repeated useful path patterns can become reusable schemas.

**Promote if:**

```text
schema candidates require repeated support
exceptions are counted
schema promotion is logged
schema application improves a held-out dev task
```

**Kill if:**

```text
single examples become permanent schemas
schemas are just hand-coded rules
schema application ignores exceptions
```

---

## v0.6 Memory

**Goal:** useful memories persist; unused memories decay; forgetting requires sustained low charge.

**Promote if:**

```text
reused memory charge increases
unused memory charge decays
memory is deleted only after low charge persists beyond hold period
memory charge changes are logged
```

**Kill if:**

```text
important memories disappear after one idle period
episodic memory grows without bound
memory deletion is irreversible without trace
```

---

## v0.7 Routing

**Goal:** routine queries stay cheap; novel or contradictory queries recruit fluid workspace.

**Promote if:**

```text
routine query avoids fluid workspace
novel query enters fluid workspace
contradiction increases difficulty score
routing decision is logged
```

**Kill if:**

```text
fluid workspace activates for everything
fluid workspace never activates
routing threshold is tuned only on frozen evals
```

---

## Parameter discipline

See [architecture_guardrails.md](architecture_guardrails.md#parameter-discipline).

Do not tune on frozen evals — see [frozen_eval_rules.md](frozen_eval_rules.md).

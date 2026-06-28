# v1.2.1 Results

**Status:** Repeat / Draft

> v1.2.1 currently establishes the pure-feedback harness: oracle online `train_pair` chains are disabled and selected policy edges are traceable. It does not yet establish pure-feedback tool-schema acquisition.

## Still TBD (blocks promote)

- push rises during online learning
- frozen eval beats baseline on a nontrivial held-out map
- ablation drops push/escape
- full trace connects action credit to outcome mutation

## Harness (landed)

- `--pure-feedback` disables oracle `train_pair` chains in online tool acquisition
- Policy edges contributing to the selected action record `NERVA_TRACE_USED_PATH` during learn phase
- Dynamics pretrain (`tagworld_pretrain_abstract_dynamics`) unchanged — world structure observation only

## Evaluation map note

Map D is not useful for the "beats random" claim: random baseline is already saturated at 100%.
v1.2.1 uses a new held-out pressure map **G** (novel topology, oracle wins, random baseline 20-80%)
so the +20 pp margin is meaningful. See [gate.md](gate.md).

## Next

1. Add held-out pressure map G.
2. Confirm oracle wins (push→run escape >= 95%).
3. Confirm random baseline is not saturated (20-80%).
4. Run pure-feedback learning.
5. Inspect traces.
6. Only then tune feedback/credit.

# Frozen evaluation rules

Keep frozen eval files frozen.

## Layout

```text
evaluation/
  dev/
    core_path_dev.nerva
    exception_dev.nerva
    memory_dev.nerva
  frozen/
    frozen_core_path_001.nerva
    frozen_exception_001.nerva
    frozen_transitive_001.nerva
    frozen_container_001.nerva
    frozen_memory_001.nerva
```

## Rules

```text
Do not tune on frozen evals.
Do not edit frozen evals after seeing engine behavior.
Do not train or specialize the graph on frozen evals.
Do not refresh frozen evals to make results look better.
```

## Usage

- `evaluation/dev/` — debugging, iteration, parameter exploration
- `evaluation/frozen/` — reporting only; held-out from tuning

Routing thresholds and other hyperparameters must not be tuned only on frozen evals.

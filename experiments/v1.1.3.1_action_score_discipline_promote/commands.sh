#!/usr/bin/env bash
# Reproduce v1.1.3.1 action score discipline gate (from repo root)
set -euo pipefail

./build.ps1
./build/test_runner.exe
for seed in 1 5 11; do
  ./build/nerva_tagworld.exe --map tool --mode action --online-frozen --seed "$seed" --fast --baseline \
    | tee "runs/tagworld/run_action_score_seed${seed}.log"
done

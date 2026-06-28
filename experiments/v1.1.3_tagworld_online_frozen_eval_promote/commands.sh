#!/usr/bin/env bash
# Reproduce v1.1.3 Online Frozen Eval promote gate (from repo root)
set -euo pipefail

./build.ps1
./build/nerva_tagworld.exe --map tool --mode action --online-frozen --seed 1 --fast --baseline \
  | tee runs/tagworld/run_online_frozen_seed1.log
./build/nerva_tagworld.exe --map tool --mode action --online-frozen --seed 5 --fast --baseline \
  | tee runs/tagworld/run_online_frozen_seed5.log
./build/nerva_tagworld.exe --map tool --mode action --online-frozen --seed 11 --fast --baseline \
  | tee runs/tagworld/run_online_frozen_seed11.log
./build/nerva_tagworld.exe --map tool --mode action --online-frozen --seed 1 --learn-episodes 200 \
  --eval-episodes 1 --write-replay \
  experiments/v1.1.3_tagworld_online_frozen_eval_promote/trace_replay.jsonl

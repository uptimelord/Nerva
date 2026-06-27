#!/usr/bin/env bash
# Reproduce v1.1.1 Tool-Action Pressure promote gate (from repo root)
set -euo pipefail

./build.ps1
./build/nerva_tagworld.exe --map tool --mode action --episodes 100 --seed 1 --fast --baseline \
  | tee runs/tagworld/run_tool_action_seed1.log
./build/nerva_tagworld.exe --map tool --mode action --episodes 100 --seed 5 --fast --baseline \
  | tee runs/tagworld/run_tool_action_seed5.log
./build/nerva_tagworld.exe --map tool --mode action --episodes 100 --seed 11 --fast --baseline \
  | tee runs/tagworld/run_tool_action_seed11.log
./build/nerva_tagworld.exe --map tool --mode action --episodes 1 --seed 1 --write-replay \
  experiments/v1.1.1_tagworld_tool_action_promote/trace_replay.jsonl

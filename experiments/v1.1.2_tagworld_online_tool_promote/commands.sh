#!/usr/bin/env bash
# Reproduce v1.1.2 Online Tool Acquisition promote gate (from repo root)
set -euo pipefail

./build.ps1
./build/nerva_tagworld.exe --map tool --mode action --online-tool --episodes 200 --seed 1 --fast --baseline \
  | tee runs/tagworld/run_online_tool_seed1.log
./build/nerva_tagworld.exe --map tool --mode action --online-tool --episodes 200 --seed 5 --fast --baseline \
  | tee runs/tagworld/run_online_tool_seed5.log
./build/nerva_tagworld.exe --map tool --mode action --online-tool --episodes 200 --seed 11 --fast --baseline \
  | tee runs/tagworld/run_online_tool_seed11.log
./build/nerva_tagworld.exe --map tool --mode action --online-tool --episodes 1 --seed 1 --write-replay \
  experiments/v1.1.2_tagworld_online_tool_promote/trace_replay.jsonl

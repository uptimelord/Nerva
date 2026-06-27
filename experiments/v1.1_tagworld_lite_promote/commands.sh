#!/usr/bin/env bash
# Reproduce v1.1 TagWorld-lite promote gate (from repo root)
set -euo pipefail

./build.bat
./build/nerva_tagworld.exe --episodes 1000 --mode observer --seed 1 --fast \
  | tee runs/tagworld/run_observer_1k.log
./build/nerva_tagworld.exe --episodes 1000 --mode prediction --seed 1 --fast \
  | tee runs/tagworld/run_prediction_1k.log
./build/nerva_tagworld.exe --episodes 1000 --mode action --seed 1 --fast --baseline \
  | tee runs/tagworld/run_action_1k.log
./build/nerva_tagworld.exe --episodes 100 --mode action --seed 5 --fast --baseline \
  | tee runs/tagworld/run_action_seed5.log
./build/nerva_tagworld.exe --episodes 100 --mode action --seed 11 --fast --baseline \
  | tee runs/tagworld/run_action_seed11.log
./build/nerva_tagworld.exe --episodes 100000 --mode observer --seed 1 --fast \
  | tee runs/tagworld/run_observer_100k.log

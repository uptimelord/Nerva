#!/usr/bin/env bash
# Reproduce v1.2 TagWorld Generalization promote gate (from repo root)
set -euo pipefail

./build.ps1
./build/nerva_tagworld.exe --generalization --mode action --eval-map D --seed 1 --fast --baseline \
  | tee runs/tagworld/run_generalization_seed1.log
./build/nerva_tagworld.exe --generalization --mode action --eval-map D --seed 5 --fast --baseline \
  | tee runs/tagworld/run_generalization_seed5.log
./build/nerva_tagworld.exe --generalization --mode action --eval-map D --seed 11 --fast --baseline \
  | tee runs/tagworld/run_generalization_seed11.log
./build/nerva_tagworld.exe --generalization --mode action --eval-map E --seed 1 --fast --baseline \
  | tee runs/tagworld/run_generalization_eval_E_seed1.log
./build/nerva_tagworld.exe --generalization --mode action --eval-map F --seed 1 --fast --baseline \
  | tee runs/tagworld/run_generalization_eval_F_seed1.log

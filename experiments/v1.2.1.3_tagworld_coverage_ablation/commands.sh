#!/usr/bin/env bash
# v1.2.1.3 coverage ablation ladder — gate seeds on held-out map G
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${ROOT}/build/nerva_tagworld"
if [[ ! -x "$BIN" && -x "${ROOT}/build/nerva_tagworld.exe" ]]; then
  BIN="${ROOT}/build/nerva_tagworld.exe"
fi

SEEDS=(1 2 3 5 7 11)
COMMON=(--generalization --pure-feedback --mode action --eval-map G --learn-episodes 400 --fast --baseline)

run_variant() {
  local name="$1"
  shift
  echo "######## variant: ${name} ########"
  for SEED in "${SEEDS[@]}"; do
    echo "=== ${name} seed ${SEED} ==="
    "$BIN" "${COMMON[@]}" --seed "$SEED" "$@"
  done
}

# Known-good baseline (v1.2.1.2)
run_variant "coverage_400" --coverage-episodes 400

# Episode-budget ablation
run_variant "coverage_200" --coverage-episodes 200
run_variant "coverage_100" --coverage-episodes 100
run_variant "coverage_50"  --coverage-episodes 50

# Observation-budget ablation
for N in 1 5 10 20; do
  run_variant "until_push_block_${N}" --coverage-until-push-block "$N"
done

# Epsilon only (explicit zero coverage)
run_variant "epsilon_only" --coverage-episodes 0

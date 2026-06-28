#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${ROOT}/build/nerva_tagworld"
if [[ ! -x "$BIN" && -x "${ROOT}/build/nerva_tagworld.exe" ]]; then
  BIN="${ROOT}/build/nerva_tagworld.exe"
fi

for SEED in 1 5 11; do
  echo "=== v1.2.1.1 eligibility credit seed ${SEED} (eval map G) ==="
  "$BIN" --generalization --pure-feedback --mode action --eval-map G --seed "$SEED" --fast --baseline
done

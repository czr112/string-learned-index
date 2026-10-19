#!/bin/bash
# Stage-6 clean rebuild: recompute the space round's ledger binary from source and compare.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
ROUND="$W/07_Eval_Integration/results/bm_space_20261019b"
FRESH="$W/builds/rebuild_20261019"
rm -rf "$FRESH"; mkdir -p "$FRESH"
gcc -std=c11 -DNDEBUG -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" \
  -c "$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src/art.c" \
  -o "$FRESH/art_clean.o"
g++ -pthread -std=c++17 -DNDEBUG -march=x86-64-v3 \
  -I"$W/07_Eval_Integration/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$W/07_Eval_Integration/upstream_snapshots/model_3894c767/src" \
  -I"$W/07_Eval_Integration/diagnostics" \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -O3 -DBM_ALLOCATION_TRACE=0 \
  "$W/07_Eval_Integration/diagnostics/ledger_bm_space.cpp" \
  "$W/07_Eval_Integration/adapters/art/art_adapter.cpp" \
  "$W/07_Eval_Integration/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" \
  "$W/07_Eval_Integration/results/upstream_update_audit_20261019c/build/src/libblocklearn_adapter.a" \
  "$W/07_Eval_Integration/results/upstream_update_audit_20261019c/build/src/libblocklearn_core.a" \
  "$FRESH/art_clean.o" -o "$FRESH/clean"
echo "fresh $(sha256sum "$FRESH/clean" | cut -d' ' -f1)"
echo "recorded $(sha256sum "$ROUND/build/clean" | cut -d' ' -f1)"

#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/n3_space_20261022"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/gate_bm_n3_space.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" -o "$F/space" 2>"$F/build.err" \
  || { echo BUILD FAILED; tail -20 "$F/build.err"; exit 1; }
for name in english_words memetracker_urls memetracker_phrases; do
  echo -n "$name "
  "$F/space" "$R/results/bm_real_20261018/interchange/${name}_rows.bin"
done

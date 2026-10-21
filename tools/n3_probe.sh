#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/n3_probe_20261021"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" -I"$R/upstream_snapshots/model_3894c767/src" \
  -I"$R/learned-index/src" -I"$D" "$D/probe_bm_n3_split.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" -o "$F/probe" 2>"$F/build.err" \
  || { echo BUILD FAILED; tail -20 "$F/build.err"; exit 1; }
"$F/probe" "$R/results/bm_p3_dynamic_20261020/traces/english_words_95_5_growth_rows.bin" 40

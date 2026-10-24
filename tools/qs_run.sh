#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/qs_20261025"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/diag_bm_query_subsets.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/qs" 2>"$F/build.err" || { echo BUILD FAILED; tail -12 "$F/build.err"; exit 1; }
T="$R/results/bm_audited_v3_20261025/traces"
for cell in words_95_5_uniform_growth words_80_20_uniform_growth \
            words_95_5_hotspot_growth words_95_5_uniform_replacement; do
  echo "== $cell"
  "$F/qs" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" | sed 's/^/   /'
done

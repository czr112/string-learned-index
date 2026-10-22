#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"
T="$R/results/bm_p3_dynamic_20261020/traces"
B="$W/builds/dev"
mkdir -p "$B"
g++ -pthread -std=c++17 -march=x86-64-v3 -O1 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$R/diagnostics" \
  "$R/diagnostics/diag_bm_n3_ledger.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" -o "$B/diag"
echo "== baseline"
"$B"/diag "$T/english_words_95_5_growth_rows.bin" "$T/english_words_95_5_growth_ops.bin" baseline || true
echo "== candidate"
"$B"/diag "$T/english_words_95_5_growth_rows.bin" "$T/english_words_95_5_growth_ops.bin" candidate || true

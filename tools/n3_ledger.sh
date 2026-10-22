#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/n3_ledger_20261023"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/gate_bm_n3_ledger.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/ledger" 2>"$F/build.err" || { echo BUILD FAILED; tail -15 "$F/build.err"; exit 1; }
T="$R/results/bm_p3_dynamic_20261020/traces"
for cell in english_words_95_5_growth english_words_80_20_growth \
            memetracker_urls_95_5_growth memetracker_urls_80_20_growth; do
  "$F/ledger" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" | sed "s/^{/{\"cell\":\"$cell\",/"
done

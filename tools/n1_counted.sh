#!/bin/bash
# N1: count rebuild exits (page full / prefix change / slot full / arena short).
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"
D="$R/diagnostics"
FRESH="$W/builds/n1_counted_20261021"
rm -rf "$FRESH"; mkdir -p "$FRESH"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" \
  -I"$R/learned-index/src" -I"$D" \
  "$D/bench_bm_n1_counted.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$FRESH/counted" 2>"$FRESH/build.err" || { echo BUILD FAILED; tail -25 "$FRESH/build.err"; exit 1; }
T="$R/results/bm_p3_dynamic_20261020/traces"
for cell in english_words_95_5_growth english_words_80_20_growth \
            memetracker_urls_95_5_growth memetracker_urls_80_20_growth; do
  "$FRESH/counted" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$cell" >"$FRESH/${cell}.json" 2>"$FRESH/${cell}.err" \
    || { echo "FAILED $cell"; cat "$FRESH/${cell}.err"; exit 1; }
  cat "$FRESH/${cell}.json"
done

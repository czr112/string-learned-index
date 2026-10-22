#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/n3_fault_20261023"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/gate_bm_n3_fault.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/fault" 2>"$F/build.err" || { echo BUILD FAILED; tail -15 "$F/build.err"; exit 1; }
for at in 1 2 3 5 8 13; do
  "$F/fault" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" "$at" \
    || { echo "FAULT GATE FAILED at=$at"; exit 1; }
done
gunzip -c /dev/null 2>/dev/null || true

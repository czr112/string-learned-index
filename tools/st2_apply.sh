#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 "$W/tools/_st2d.py" && rm -f "$W/tools/_st2d.py"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/gate_bm_n3_fault.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$W/builds/fault2" 2>&1 | head -8
T="$R/results/bm_p3_dynamic_20261020/traces"
# deep replay: full trace on the candidate, then the ledger
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/diag_bm_n3_ledger.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" -o "$W/builds/diag2"
"$W/builds/diag2" "$T/english_words_95_5_growth_rows.bin" "$T/english_words_95_5_growth_ops.bin" candidate
"$W/builds/diag2" "$T/memetracker_urls_80_20_growth_rows.bin" "$T/memetracker_urls_80_20_growth_ops.bin" candidate
for at in 1 2 3 5 8 13 21; do
  "$W/builds/fault2" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" "$at"
done

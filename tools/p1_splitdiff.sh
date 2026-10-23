#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/p1_splitdiff_20261024"; rm -rf "$F"; mkdir -p "$F"
python3 - <<'PY'
import pathlib
p=pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/gate_bm_split_diff.cpp')
t=p.read_text(encoding='utf-8')
t=t.replace('for(const auto& key:sink+=0, keys)','for(const auto& key:keys)')
p.write_text(t,encoding='utf-8')
PY
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/gate_bm_split_diff.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/splitdiff" 2>"$F/build.err" || { echo BUILD FAILED; tail -15 "$F/build.err"; exit 1; }
for n in 400 2000 6000; do
  "$F/splitdiff" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" "$n"
done

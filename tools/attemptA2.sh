#!/bin/bash
# Fix attempt A: shorten the block right after the reserves (the only throwing step), so no
# transient duplicate is visible to scan()/rebuild().
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/bm_n1_page_core.hpp')
t = p.read_text(encoding='utf-8')
old = """            stable_fences_.insert(stable_fences_.begin()+std::ptrdiff_t(at),split_fence);
            blocks_.insert(blocks_.begin()+std::ptrdiff_t(at),tail);
            block.count=8;"""
new = """            block.count=8;                     // shrink first: the reserves above were the only throwing step
            stable_fences_.insert(stable_fences_.begin()+std::ptrdiff_t(at),split_fence);
            blocks_.insert(blocks_.begin()+std::ptrdiff_t(at),tail);"""
assert old in t, 'insert block not found'
p.write_text(t.replace(old, new), encoding='utf-8')
print('ordering fixed')
PY
F="$W/builds/qs_20261025"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/diag_bm_query_subsets.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/qsA" 2>"$F/buildA.err" || { echo BUILD FAILED; tail -10 "$F/buildA.err"; exit 1; }
T="$R/results/bm_audited_v3_20261025/traces"
for cell in words_95_5_uniform_growth words_80_20_uniform_growth \
            words_95_5_hotspot_growth words_95_5_uniform_replacement; do
  echo "== $cell"
  "$F/qsA" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" | sed 's/^/   /'
done

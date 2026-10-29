#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    start = s.index('rebuild_by_split')
    end = s.index('    Pages::iterator rebuild(Pages::iterator target,std::vector<Row> rows,bool coalesce) {')
    body = s[start:end]
    if 'resident_=memory_bytes();' in body:
        print(f'{name}: already recomputed'); continue
    body2 = body.replace("            root_.swap(next_root);",
                         "            root_.swap(next_root);\n"
                         "            resident_=memory_bytes();   // exact by construction, O(pages)", 1)
    assert body2 != body, name
    s = s[:start] + body2 + s[end:]
    p.write_text(s, encoding='utf-8')
    print(f'{name}: resident recomputed after the root swap')
PY
F="$W/builds/g2g_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/dbg_bm_counts.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/dbg" 2>"$F/b.err" || { echo BUILD FAILED; tail -10 "$F/b.err"; exit 1; }
"$F/dbg" "$R/results/bm_real_20261018/interchange/english_words_rows.bin"

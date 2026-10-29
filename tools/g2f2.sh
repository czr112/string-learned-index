#!/bin/bash
# Make the accounting uniformly exact: recompute resident_ at the end of rebuild() as well.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    if '/*exact*/' in s:
        print(f'{name}: already exact'); continue
    old = "            resident_=resident_-removed+added-old_root_bytes+new_root_bytes;generation_+=pieces;"
    new = "            resident_=resident_-removed+added-old_root_bytes+new_root_bytes;generation_+=pieces;\n"
    if old in s:
        s = s.replace(old, new, 1)
        print(f'{name}: found rebuild formula')
    # after the generic rebuild's root swap, recompute exactly
    marker = "            root_.swap(next_root);"
    idx = s.find(marker)
    if idx != -1 and '/*exact*/' not in s:
        s = s[:idx+len(marker)] + "\n            resident_=memory_bytes();   /*exact*/" + s[idx+len(marker):]
        p.write_text(s, encoding='utf-8')
        print(f'{name}: resident recomputed in rebuild()')
PY
F="$W/builds/g2f2_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/gate_bm_n3.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" \
  -o "$F/gate" 2>"$F/b.err" || { echo BUILD FAILED; tail -8 "$F/b.err"; exit 1; }
for name in english_words memetracker_urls memetracker_phrases; do
  "$F/gate" "$R/results/bm_real_20261018/interchange/${name}_rows.bin" \
            "$R/results/bm_real_20261018/interchange/${name}_queries.bin" 2>&1 | tail -1 \
    | sed "s/^{/{\"corpus\":\"$name\",/"
done

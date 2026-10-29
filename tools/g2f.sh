#!/bin/bash
# Fix: recompute resident_ from memory_bytes() (exact by construction) instead of a formula.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    old = """            resident_=resident_-removed+added-old_root_bytes+new_root_bytes;
"""
    new = """            (void)removed;(void)added;(void)old_root_bytes;(void)new_root_bytes;
"""
    if old in s:
        s = s.replace(old, new, 1)
        # after the root swap, recompute exactly
        s = s.replace("""            root_.swap(next_root);
            ++maintenance_.rebuilds;++maintenance_.inplace_inserts;
            ++size_;""",
                      """            root_.swap(next_root);
            // Recomputation is exact by construction and costs O(pages); the hand formula was
            // 41 KB short because payload() accounting differs from the split's real footprint.
            resident_=memory_bytes();
            ++maintenance_.rebuilds;++maintenance_.inplace_inserts;
            ++size_;""", 1)
        p.write_text(s, encoding='utf-8')
        print(f'{name}: resident recomputed')
    else:
        print(f'{name}: formula not found (check state)')
PY
F="$W/builds/g2f_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/dbg_bm_counts.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/dbg" 2>"$F/b.err" || { echo BUILD FAILED; tail -10 "$F/b.err"; exit 1; }
"$F/dbg" "$R/results/bm_real_20261018/interchange/english_words_rows.bin"

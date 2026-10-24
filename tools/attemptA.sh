#!/bin/bash
# Attempt A: reserve headroom instead of rebuilding the directory on every split, then re-run
# the query-subset diagnosis.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/bm_n1_page_core.hpp')
t = p.read_text(encoding='utf-8')
start = t.index('        if(block.count==16) {')
end = t.index('        {\n        auto& dest=blocks_[target];')
new = '''        if(block.count==16) {
            // Strong guarantee without rebuilding the whole directory: grow capacity with
            // headroom first (this is the only step that can throw), then splice in place.
            Block tail;tail.count=8;
            for(std::uint32_t i=8;i<16;++i){tail.entries[i-8]=block.entries[i];tail.tags[i-8]=block.tags[i];}
            const Fence split_fence{tail.entries[0].offset,tail.entries[0].length};
            const auto at=std::size_t(r.first)+std::size_t(b)+1;
            if(blocks_.capacity()<blocks_.size()+1)blocks_.reserve(blocks_.size()+8);
            if(stable_fences_.capacity()<stable_fences_.size()+1)
                stable_fences_.reserve(stable_fences_.size()+8);
            stable_fences_.insert(stable_fences_.begin()+std::ptrdiff_t(at),split_fence);
            blocks_.insert(blocks_.begin()+std::ptrdiff_t(at),tail);
            block.count=8;
            ++regions_[0].count;
            ++reasons().block_splits;
            target=compare(s,suffix(blocks_[at].entries[0]))<0?std::size_t(b):std::size_t(b)+1;
        }
'''
t = t[:start] + new + t[end:]
p.write_text(t, encoding='utf-8')
print('attempt A applied: reserve headroom, splice in place')
PY
F="$W/builds/qs_20261025"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/diag_bm_query_subsets.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/qsA" 2>"$F/buildA.err" || { echo BUILD FAILED; tail -12 "$F/buildA.err"; exit 1; }
T="$R/results/bm_audited_v3_20261025/traces"
for cell in words_95_5_uniform_growth words_80_20_uniform_growth \
            words_95_5_hotspot_growth words_95_5_uniform_replacement; do
  echo "== $cell"
  "$F/qsA" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" | sed 's/^/   /'
done

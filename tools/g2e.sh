#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
git -C "$W" checkout 8a94f76 -- "$D/bm_n1_page_core.hpp"
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/bm_n1_page_core.hpp')
t = p.read_text(encoding='utf-8')
if 'block_for' not in t:
    anchor = '    struct SplitResult {'
    add = ('    std::uint32_t block_for(std::string_view key)const {\n'
           '        if(regions_.empty())throw std::logic_error("empty page");\n'
           '        const auto& r=regions_[0];\n'
           '        return directory_block(r,key.substr(r.prefix.size()),nullptr);\n'
           '    }\n')
    t = t.replace(anchor, add + anchor, 1)
if 'std::unique_ptr<Page> lower;' not in t:
    print('WARNING: this page core does not have the two-half SplitResult')
p.write_text(t, encoding='utf-8')
print('page core prepared; block_for present:', 'block_for' in t,
      '| two halves:', 'std::unique_ptr<Page> lower;' in t)
PY
F="$W/builds/g2e_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/dbg_bm_counts.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/dbg" 2>"$F/b.err" || { echo BUILD FAILED; tail -10 "$F/b.err"; exit 1; }
"$F/dbg" "$R/results/bm_real_20261018/interchange/english_words_rows.bin"

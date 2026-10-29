#!/bin/bash
# Fix: drop the duplicate block_count, update the unit test to the two-phase API, pin b before
# any mutation, then build and run the unit test + semantic gate.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
core = d/'bm_n1_page_core.hpp'
t = core.read_text(encoding='utf-8')
dup = "    std::uint32_t block_count()const{return regions_.empty()?0:regions_[0].count;}\n"
if dup in t:
    t = t.replace(dup, "", 1)
    core.write_text(t, encoding='utf-8')
    print('duplicate block_count removed')

for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    old = """            auto core=target->second.core.get();
            auto split=Page::extract_split(*core,std::uint32_t(core->block_count()/2));"""
    new = """            auto core=target->second.core.get();
            const auto b=std::uint32_t(core->block_count()/2);
            auto split=Page::extract_split(*core,b);"""
    if old in s:
        s = s.replace(old, new, 1)
        s = s.replace("core->apply_drop(std::uint32_t(core->block_count()/2),std::move(split.lower_model));",
                      "core->apply_drop(b,std::move(split.lower_model));", 1)
        p.write_text(s, encoding='utf-8')
        print(f'{name}: split point pinned')

test = d/'check_bm_split_block.cpp'
s = test.read_text(encoding='utf-8')
s = s.replace("        auto upper=page.split_at_block(split);",
              "        auto split_result=bm_n1_leaf::Page::extract_split(page,split);\n"
              "        page.apply_drop(split,std::move(split_result.lower_model));\n"
              "        auto& upper=split_result.upper;")
test.write_text(s, encoding='utf-8')
print('unit test updated to the two-phase API')
PY
F="$W/builds/u3e_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/check_bm_split_block.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/check" 2>"$F/build.err" || { echo BUILD FAILED; tail -14 "$F/build.err"; exit 1; }
for b in 70 35 100; do
  "$F/check" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" "$b"
done

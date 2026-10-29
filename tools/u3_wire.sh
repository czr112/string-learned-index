#!/bin/bash
# U3 step 3: wire split_at_block into the page-full rebuild path, then gate it.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')

# 1) Page: expose the block count (needed to choose a middle split point).
core = d/'bm_n1_page_core.hpp'
t = core.read_text(encoding='utf-8')
if 'std::uint32_t block_count()const' not in t:
    t = t.replace("public:\n    // Move blocks [b_split, count)",
                  "public:\n    std::uint32_t block_count()const{return regions_.empty()?0:regions_[0].count;}\n"
                  "    // Move blocks [b_split, count)", 1)
    core.write_text(t, encoding='utf-8')
    print('block_count added')

# 2) Index: add rebuild_by_split and call it when the page is full.
for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    if 'rebuild_by_split' in s:
        continue
    helper = '''    // A full page is split in place instead of being scanned into strings and re-encoded.
    // The split runs on a copy so the live page is untouched until the new root is ready.
    Pages::iterator rebuild_by_split(Pages::iterator target,std::string_view key,std::uint64_t value) {
        try {
            auto draft=std::make_unique<Page>(*target->second.core);
            const auto blocks=draft->block_count();
            auto upper=draft->split_at_block(std::uint32_t(blocks/2));
            const auto upper_key=upper->fence_keys().front();
            Page* dest=compare(key,upper_key)<0?draft.get():upper.get();
            if(dest->try_insert_known_absent(key,value)!=Page::Insert::inserted)
                throw std::logic_error("split insert rejected");
            Pages staged;
            staged.emplace(target->first,Node{std::move(draft),++generation_});
            staged.emplace(upper_key,Node{std::move(upper),++generation_});
            const auto added=route_bytes(staged);
            auto next_root=prepare_root(target,pages_.end(),staged,added);
            peak(added+next_root->memory_bytes());
            clear_dirty(target);
            auto replacement=staged.begin();
            std::swap(target->second,replacement->second);
            staged.erase(replacement);
            while(!staged.empty()) {
                auto node=staged.extract(staged.begin());
                pages_.insert(std::move(node));
            }
            root_.swap(next_root);
            ++maintenance_.rebuilds;
            ++maintenance_.inplace_inserts;
            ++size_;
            return target;
        }catch(...){++maintenance_.failed_rebuilds;throw;}
    }
'''
    anchor = '    Pages::iterator rebuild(Pages::iterator target,std::vector<Row> rows,bool coalesce) {'
    assert anchor in s, f'{name}: rebuild anchor missing'
    s = s.replace(anchor, helper + anchor, 1)
    old_call = """            else {
                auto rows=it->second.core->scan("",SIZE_MAX);"""
    new_call = """            else if(it->second.core->size()>=2048) {
                rebuild_by_split(it,key,value);
                return true;
            }
            else {
                auto rows=it->second.core->scan("",SIZE_MAX);"""
    assert old_call in s, f'{name}: insert else-branch missing'
    s = s.replace(old_call, new_call, 1)
    p.write_text(s, encoding='utf-8')
    print(f'{name}: wired')
PY
F="$W/builds/u3wire_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/gate_bm_split_factory.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/residual_tags.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/query_cost.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/region_planner.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/learned_index.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/benchmark/model_factories.cpp" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" \
  -DBM_MODEL_COMMIT='"3894c767"' -DBM_SPLIT_CORE_SHA256='"u3"' -DBM_SPLIT_ADAPTER_SHA256='"u3"' \
  -o "$F/gate" 2>"$F/build.err" || { echo BUILD FAILED; tail -14 "$F/build.err"; exit 1; }
for name in english_words memetracker_urls memetracker_phrases; do
  "$F/gate" "$R/results/bm_real_20261018/interchange/${name}_rows.bin" \
            "$R/results/bm_real_20261018/interchange/${name}_queries.bin" | sed "s/^{/{\"corpus\":\"$name\",/"
done

#!/bin/bash
# U3 solution A': split at the key's own block so the insert always lands in the (local) upper
# page; the lower half is dropped only after the root is ready.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
core = d/'bm_n1_page_core.hpp'
t = core.read_text(encoding='utf-8')
if 'block_for' not in t:
    anchor = '    struct SplitResult {'
    add = ('    // Block that would hold this key (the caller guarantees the region prefix matches).\n'
           '    std::uint32_t block_for(std::string_view key)const {\n'
           '        if(regions_.empty())throw std::logic_error("empty page");\n'
           '        const auto& r=regions_[0];\n'
           '        return directory_block(r,key.substr(r.prefix.size()),nullptr);\n'
           '    }\n')
    assert anchor in t
    t = t.replace(anchor, add + anchor, 1)
    core.write_text(t, encoding='utf-8')
    print('block_for added')

for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    if 'rebuild_by_split' in s:
        continue
    helper = '''    // Page-full insert: split at the key's own block so the new key always lands in the local
    // upper page. The lower half is only dropped once the new root exists.
    Pages::iterator rebuild_by_split(Pages::iterator target,std::string_view key,std::uint64_t value) {
        try {
            auto* core=target->second.core.get();
            const auto total=std::uint32_t(core->block_count());
            auto b=core->block_for(key);
            if(b==0)b=1;
            if(b>=total)b=total-1;
            auto split=Page::extract_split(*core,b);              // may throw; core untouched
            const auto upper_key=split.upper->fence_keys().front();
            if(split.upper->try_insert(key,value)!=Page::Insert::inserted)
                throw std::logic_error("split insert into the upper half was rejected");
            Pages staged;
            staged.emplace(upper_key,Node{std::move(split.upper),++generation_});
            const auto added=route_bytes(staged);
            auto next_root=prepare_root(target,pages_.end(),staged,added);   // may throw
            core->apply_drop(b,std::move(split.lower_model));     // noexcept
            while(!staged.empty()) {
                auto node=staged.extract(staged.begin());
                pages_.insert(std::move(node));
            }
            root_.swap(next_root);
            ++maintenance_.rebuilds;++maintenance_.inplace_inserts;++size_;
            return target;
        }catch(...){++maintenance_.failed_rebuilds;throw;}
    }
'''
    anchor='    Pages::iterator rebuild(Pages::iterator target,std::vector<Row> rows,bool coalesce) {'
    assert anchor in s, name
    s = s.replace(anchor, helper + anchor, 1)
    old_call = """            else {
                auto rows=it->second.core->scan("",SIZE_MAX);"""
    new_call = """            else if(it->second.core->size()>=2048) {
                rebuild_by_split(it,key,value);
                return true;
            }
            else {
                auto rows=it->second.core->scan("",SIZE_MAX);"""
    assert old_call in s, name
    s = s.replace(old_call, new_call, 1)
    p.write_text(s, encoding='utf-8')
    print(f'{name}: wired')
PY
F="$W/builds/u3x_20261029"; rm -rf "$F"; mkdir -p "$F"
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
  -o "$F/gate" 2>"$F/build.err" || { echo BUILD FAILED; tail -14 "$F/build.err"; git -C "$W" checkout -- "$D/bm_n1_index.hpp" "$D/bm_split_index.hpp"; exit 1; }
for name in english_words memetracker_urls memetracker_phrases; do
  "$F/gate" "$R/results/bm_real_20261018/interchange/${name}_rows.bin" \
            "$R/results/bm_real_20261018/interchange/${name}_queries.bin" | sed "s/^{/{\"corpus\":\"$name\",/" \
  || { echo "GATE FAILED $name"; git -C "$W" checkout -- "$D/bm_n1_index.hpp" "$D/bm_split_index.hpp"; exit 1; }
done

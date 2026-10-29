#!/bin/bash
# U3 final wiring: page-full inserts go through the two-phase split, with both nodes staged.
set -eu
export PATH="/home/nocctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
python3 - <<'PY'
import pathlib, os
home = pathlib.Path('/home/noctis')
pathlib.Path('/tmp/x')  # noop
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    if 'rebuild_by_split' in s:
        print(f'{name}: already wired'); continue
    helper = '''    // Page-full insert: split the page in place instead of scanning it into strings.
    // Everything that can throw happens before the live page is mutated.
    Pages::iterator rebuild_by_split(Pages::iterator target,std::string_view key,std::uint64_t value) {
        try {
            auto* core=target->second.core.get();
            const auto b=std::uint32_t(core->block_count()/2);
            auto split=Page::extract_split(*core,b);              // may throw; core untouched
            const auto upper_key=split.upper->fence_keys().front();
            const bool goes_upper=compare(key,upper_key)>=0;
            if(goes_upper&&split.upper->try_insert(key,value)!=Page::Insert::inserted)
                throw std::logic_error("split insert into the upper half was rejected");
            Pages staged;
            staged.emplace(upper_key,Node{std::move(split.upper),++generation_});
            const auto added=route_bytes(staged);
            auto next_root=prepare_root(target,pages_.end(),staged,added);   // may throw
            if(!goes_upper&&core->try_insert(key,value)!=Page::Insert::inserted)
                throw std::logic_error("split insert into the lower half was rejected");
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
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/u3w_20261029"; rm -rf "$F"; mkdir -p "$F"
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

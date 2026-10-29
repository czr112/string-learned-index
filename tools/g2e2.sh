#!/bin/bash
# G2e: wire with ONE placement judgement (block_for) and resident recomputed after commit.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
helper = '''    // G2e: page-full insert via the two-phase split. Placement is decided by block_for alone,
    // and the resident ledger is recomputed after the commit instead of patched by a formula.
    Pages::iterator rebuild_by_split(Pages::iterator target,std::string_view key,std::uint64_t value) {
        try {
            auto* core=target->second.core.get();
            const auto total=std::uint32_t(core->block_count());
            const auto at=core->block_for(key);                 // the one and only placement judge
            auto point=at;
            if(point==0)point=1;                                 // the upper half must be non-empty
            if(point>=total)point=total-1;
            auto split=Page::extract_split(*core,point);         // may throw; core untouched
            const auto upper_key=split.upper->fence_keys().front();
            Page* dest=(at<point)?split.lower.get():split.upper.get();
            if(dest->try_insert(key,value)!=Page::Insert::inserted)
                throw std::logic_error("split insert rejected");
            const auto lower_key=target->first;
            Pages staged;
            staged.emplace(lower_key,Node{std::move(split.lower),++generation_});
            staged.emplace(upper_key,Node{std::move(split.upper),++generation_});
            const auto added=route_bytes(staged);
            auto next_root=prepare_root(target,pages_.end(),staged,added);   // may throw
            if(next_root->bindings().size()!=pages_.size()+1)
                throw std::logic_error("root/pages binding count mismatch");
            peak(added+next_root->memory_bytes());
            clear_dirty(target);
            {
                auto lower=staged.find(lower_key);
                std::swap(target->second,lower->second);
                staged.erase(lower);
            }
            while(!staged.empty()) {
                auto node=staged.extract(staged.begin());
                pages_.insert(std::move(node));
            }
            root_.swap(next_root);
            resident_=memory_bytes();       // exact by construction; O(pages)
            ++maintenance_.rebuilds;++maintenance_.inplace_inserts;
            ++size_;
            return target;
        }catch(...){++maintenance_.failed_rebuilds;throw;}
    }
'''
for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
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
    print(f'{name}: wired (single judgement + resident recompute)')
PY
F="$W/builds/g2e2_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/gate_bm_n3.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" \
  -o "$F/gate" 2>"$F/b.err" || { echo BUILD FAILED; tail -10 "$F/b.err"; exit 1; }
for name in english_words memetracker_urls memetracker_phrases; do
  "$F/gate" "$R/results/bm_real_20261018/interchange/${name}_rows.bin" \
            "$R/results/bm_real_20261018/interchange/${name}_queries.bin" 2>&1 | tail -1 \
    | sed "s/^{/{\"corpus\":\"$name\",/" || echo "GATE FAILED $name"
done

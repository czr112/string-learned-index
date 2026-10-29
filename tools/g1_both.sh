#!/bin/bash
# G1: extract BOTH halves as fresh pages so both can be staged (the old page is dropped at commit).
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
core = d/'bm_n1_page_core.hpp'
t = core.read_text(encoding='utf-8')
start = t.index('    struct SplitResult {')
end = t.index('    std::vector<std::string> fence_keys()const {')
new = '''    struct SplitResult {
        std::unique_ptr<Page> lower;
        std::unique_ptr<Page> upper;
    };
    // Phase 1 (may throw): build BOTH halves as fresh pages; the source is untouched. Everything
    // that can fail (arena growth, model fitting) happens here.
    static SplitResult extract_split(const Page& src,std::uint32_t b_split) {
        if(src.regions_.empty())throw std::logic_error("split on an empty page");
        const auto& r=src.regions_[0];
        if(b_split==0||b_split>=r.count)throw std::invalid_argument("split point");
        auto build=[&](std::uint32_t lo,std::uint32_t hi) {
            auto out=std::make_unique<Page>(std::vector<Row>{{std::string(),0}},15);
            out->blocks_.clear();out->stable_fences_.clear();out->regions_.clear();
            out->fences_.clear();out->digests_.clear();out->arena_.clear();
            std::uint32_t rows=0;
            for(std::uint32_t i=lo;i<hi;++i) {
                Block block=src.blocks_[std::size_t(r.first)+i];
                for(std::uint32_t e=0;e<block.count;++e) {
                    const auto view=src.suffix(block.entries[e]);
                    const auto at=std::uint32_t(out->arena_.size());
                    out->arena_.insert(out->arena_.end(),view.begin(),view.end());
                    block.entries[e].offset=at;
                    block.entries[e].length=std::uint32_t(view.size());
                }
                rows+=block.count;
                out->blocks_.push_back(block);
                out->stable_fences_.push_back({out->blocks_.back().entries[0].offset,
                                               out->blocks_.back().entries[0].length});
            }
            Region nr=r;nr.first=0;nr.count=hi-lo;
            out->regions_.push_back(nr);
            out->size_=rows;out->region_keys_=src.region_keys_;
            out->fences_.push_back(r.prefix+std::string(out->boundary(out->regions_[0],0)));
            out->fence_prefix_=out->fences_[0].size();
            out->digests_.push_back(static_physical::digest8(std::string_view()));
            out->arena_.reserve(out->arena_.size()+out->arena_.size()/32+64);
            std::vector<std::string_view> train;
            for(std::uint32_t b=0;b<out->regions_[0].count;++b)train.push_back(out->boundary(out->regions_[0],b));
            out->bm_=blocklearn::BlockModel::fit(train,16,1,7,3);
            return out;
        };
        SplitResult result;
        result.lower=build(0,b_split);
        result.upper=build(b_split,std::uint32_t(r.count));
        return result;
    }
'''
t = t[:start] + new + t[end:]
core.write_text(t, encoding='utf-8')
print('extract_split now builds both halves')

for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    old = """            const auto lower_key=target->first;
            Pages staged;
            staged.emplace(lower_key,Node{std::move(split.lower_page),++generation_});"""
    new2 = """            const auto lower_key=target->first;
            Pages staged;
            staged.emplace(lower_key,Node{std::move(split.lower),++generation_});"""
    if old in s:
        s = s.replace(old, new2, 1)
        s = s.replace("            core->apply_drop(b,std::move(split.lower_model));     // noexcept\n", "", 1)
        p.write_text(s, encoding='utf-8')
        print(f'{name}: staged both fresh halves')
PY
F="$W/builds/g2b_20261029"; rm -rf "$F"; mkdir -p "$F"
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
  -DBM_MODEL_COMMIT='"3894c767"' -DBM_SPLIT_CORE_SHA256='"g2"' -DBM_SPLIT_ADAPTER_SHA256='"g2"' \
  -o "$F/gate" 2>"$F/build.err" || { echo BUILD FAILED; tail -12 "$F/build.err"; exit 1; }
for name in english_words memetracker_urls memetracker_phrases; do
  "$F/gate" "$R/results/bm_real_20261018/interchange/${name}_rows.bin" \
            "$R/results/bm_real_20261018/interchange/${name}_queries.bin" 2>&1 | tail -2 | sed "s/^{/{\"corpus\":\"$name\",/"
done

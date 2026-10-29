#!/bin/bash
# U3 step 3b: two-phase split (const extract + noexcept apply), then wire and gate.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
d = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics')
core = d/'bm_n1_page_core.hpp'
t = core.read_text(encoding='utf-8')
if 'extract_split' not in t:
    start = t.index('    std::unique_ptr<Page> split_at_block(std::uint32_t b_split) {')
    end = t.index('    std::vector<std::string> fence_keys()const {')
    new = '''    struct SplitResult {std::unique_ptr<Page> upper;blocklearn::BlockModel lower_model;};
    // Phase 1 (may throw): build the upper page from a const source; the source is untouched.
    static SplitResult extract_split(const Page& src,std::uint32_t b_split) {
        if(src.regions_.empty())throw std::logic_error("split on an empty page");
        const auto& r=src.regions_[0];
        if(b_split==0||b_split>=r.count)throw std::invalid_argument("split point");
        auto out=std::make_unique<Page>(std::vector<Row>{{std::string(),0}},15);
        out->blocks_.clear();out->stable_fences_.clear();out->regions_.clear();
        out->fences_.clear();out->digests_.clear();out->arena_.clear();
        std::uint32_t moved_rows=0;
        for(std::uint32_t i=b_split;i<r.count;++i) {
            Block block=src.blocks_[std::size_t(r.first)+i];
            for(std::uint32_t e=0;e<block.count;++e) {
                const auto view=src.suffix(block.entries[e]);
                const auto at=std::uint32_t(out->arena_.size());
                out->arena_.insert(out->arena_.end(),view.begin(),view.end());
                block.entries[e].offset=at;
                block.entries[e].length=std::uint32_t(view.size());
            }
            moved_rows+=block.count;
            out->blocks_.push_back(block);
            out->stable_fences_.push_back({out->blocks_.back().entries[0].offset,
                                           out->blocks_.back().entries[0].length});
        }
        Region nr=r;nr.first=0;nr.count=std::uint32_t(r.count-b_split);
        out->regions_.push_back(nr);
        out->size_=moved_rows;out->region_keys_=src.region_keys_;
        out->fences_.push_back(r.prefix+std::string(out->boundary(out->regions_[0],0)));
        out->fence_prefix_=out->fences_[0].size();
        out->digests_.push_back(static_physical::digest8(std::string_view()));
        std::vector<std::string_view> train;
        for(std::uint32_t b=0;b<out->regions_[0].count;++b)train.push_back(out->boundary(out->regions_[0],b));
        out->bm_=blocklearn::BlockModel::fit(train,16,1,7,3);
        std::vector<std::string_view> lower_train;
        for(std::uint32_t b=0;b<b_split;++b)lower_train.push_back(src.boundary(r,b));
        SplitResult result;
        result.lower_model=blocklearn::BlockModel::fit(lower_train,16,1,7,3);
        result.upper=std::move(out);
        return result;
    }
    // Phase 2 (no-throw): drop the moved range from this page and adopt the precomputed model.
    void apply_drop(std::uint32_t b_split,blocklearn::BlockModel&& lower_model) noexcept {
        const auto& r=regions_[0];
        std::uint32_t removed=0;
        for(std::uint32_t i=b_split;i<r.count;++i)removed+=blocks_[std::size_t(r.first)+i].count;
        blocks_.erase(blocks_.begin()+std::ptrdiff_t(std::size_t(r.first)+b_split),blocks_.end());
        stable_fences_.erase(stable_fences_.begin()+std::ptrdiff_t(std::size_t(r.first)+b_split),
                             stable_fences_.end());
        regions_[0].count=b_split;
        size_-=removed;
        bm_=std::move(lower_model);
    }
'''
    t = t[:start] + new + t[end:]
    core.write_text(t, encoding='utf-8')
    print('two-phase split installed')

for name in ('bm_n1_index.hpp','bm_split_index.hpp'):
    p = d/name
    s = p.read_text(encoding='utf-8')
    if 'rebuild_by_split' in s:
        continue
    helper = '''    Pages::iterator rebuild_by_split(Pages::iterator target,std::string_view key,std::uint64_t value) {
        try {
            auto core=target->second.core.get();
            auto split=Page::extract_split(*core,std::uint32_t(core->block_count()/2));
            const auto upper_key=split.upper->fence_keys().front();
            Page* dest=compare(key,upper_key)<0?core:split.upper.get();
            const auto inserted=dest->try_insert(key,value);
            if(inserted!=Page::Insert::inserted)throw std::logic_error("split insert rejected");
            Pages staged;
            staged.emplace(target->first,Node{std::shared_ptr<Page>{},++generation_});
            auto next_root=prepare_root(target,pages_.end(),staged,0);
            core->apply_drop(std::uint32_t(core->block_count()/2),std::move(split.lower_model));
            pages_.emplace(upper_key,Node{std::move(split.upper),++generation_});
            root_.swap(next_root);
            ++maintenance_.rebuilds;++maintenance_.inplace_inserts;++size_;
            return target;
        }catch(...){++maintenance_.failed_rebuilds;throw;}
    }
'''
    anchor='    Pages::iterator rebuild(Pages::iterator target,std::vector<Row> rows,bool coalesce) {'
    assert anchor in s
    s = s.replace(anchor, helper + anchor, 1)
    p.write_text(s, encoding='utf-8')
    print(f'{name}: helper added (wiring deferred)')
PY
F="$W/builds/u3d_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$D/check_bm_split_block.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  -o "$F/check" 2>"$F/build.err" || { echo BUILD FAILED; tail -14 "$F/build.err"; exit 1; }
echo "build ok (unit test binary)"

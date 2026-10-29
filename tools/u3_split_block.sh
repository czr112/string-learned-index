#!/bin/bash
# U3 implementation, step 1: Page::split_at_block + a unit test that the two halves reproduce the
# original page exactly. Reverts on any failure.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/bm_n1_page_core.hpp')
t = p.read_text(encoding='utf-8')
if 'split_at_block' in t:
    print('already patched'); raise SystemExit(0)
anchor = "    std::vector<std::string> fence_keys()const {"
new = '''public:
    // Move blocks [b_split, count) out of this page into a new page that shares the region prefix.
    // The upper half gets a compacted arena with rewritten entry offsets; nothing is re-encoded
    // from keys, so no per-row std::string is materialised.
    std::unique_ptr<Page> split_at_block(std::uint32_t b_split) {
        if(regions_.empty())throw std::logic_error("split on an empty page");
        const auto& r=regions_[0];
        if(b_split==0||b_split>=r.count)throw std::invalid_argument("split point");
        auto out=std::make_unique<Page>(std::vector<Row>{{std::string(),0}},15);
        out->blocks_.clear();out->stable_fences_.clear();out->regions_.clear();
        out->fences_.clear();out->digests_.clear();out->arena_.clear();
        const auto moved=std::uint32_t(r.count-b_split);
        std::uint32_t moved_rows=0;
        for(std::uint32_t i=b_split;i<r.count;++i) {
            const auto src=std::size_t(r.first)+i;
            Block block=blocks_[src];
            for(std::uint32_t e=0;e<block.count;++e) {
                const auto view=suffix(block.entries[e]);
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
        Region nr=r;nr.first=0;nr.count=moved;out->regions_.push_back(nr);
        out->size_=moved_rows;out->region_keys_=region_keys_;
        out->fences_.push_back(r.prefix+std::string(out->boundary(out->regions_[0],0)));
        out->fence_prefix_=out->fences_[0].size();
        out->digests_.push_back(static_physical::digest8(std::string_view()));
        std::vector<std::string_view> train_out;
        for(std::uint32_t b=0;b<out->regions_[0].count;++b)train_out.push_back(out->boundary(out->regions_[0],b));
        out->bm_=blocklearn::BlockModel::fit(train_out,16,1,7,3);
        blocks_.erase(blocks_.begin()+std::ptrdiff_t(r.first+b_split),blocks_.end());
        stable_fences_.erase(stable_fences_.begin()+std::ptrdiff_t(r.first+b_split),stable_fences_.end());
        regions_[0].count=b_split;
        size_-=moved_rows;
        std::vector<std::string_view> train_lo;
        for(std::uint32_t b=0;b<regions_[0].count;++b)train_lo.push_back(boundary(regions_[0],b));
        bm_=blocklearn::BlockModel::fit(train_lo,16,1,7,3);
        return out;
    }
'''
assert anchor in t
p.write_text(t.replace(anchor, new + anchor, 1), encoding='utf-8')
print('split_at_block inserted')
PY

TEST="$D/check_bm_split_block.cpp"
cat > "$TEST" <<'CPP'
#include "bm_n1_page_root.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin split_block");
        const std::uint32_t split=std::stoul(argv[2]);
        std::ifstream in(argv[1],std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
        std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
        std::size_t pos=16;std::vector<static_physical::Row> rows;
        for(std::uint64_t i=0;i<count&&i<2048;++i) {
            std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
            std::string key=bytes.substr(pos,n);pos+=n;
            std::uint64_t v=0;std::memcpy(&v,bytes.data()+pos,8);pos+=8;
            rows.push_back({key,v});
        }
        bm_n1_leaf::Page page(rows,15);
        const auto before=page.scan("",SIZE_MAX);
        auto upper=page.split_at_block(split);
        const auto lower_scan=page.scan("",SIZE_MAX);
        const auto upper_scan=upper->scan("",SIZE_MAX);
        std::uint64_t mismatches=0;
        if(lower_scan.size()+upper_scan.size()!=before.size())++mismatches;
        else {
            std::vector<static_physical::Row> joined=lower_scan;
            joined.insert(joined.end(),upper_scan.begin(),upper_scan.end());
            for(std::size_t i=0;i<before.size();++i)
                if(joined[i].key!=before[i].key||joined[i].value!=before[i].value){++mismatches;break;}
        }
        for(const auto& row:before) {
            const auto a=page.find_base(row.key), b=upper->find_base(row.key);
            if((a.has_value()==b.has_value())&&((!a&&!b)||(a&&b&&*a==*b)))++mismatches;
            const auto got=a?a:b;
            if(!got||*got!=row.value)++mismatches;
        }
        std::cout<<"{\"rows\":"<<before.size()<<",\"split_block\":"<<split
                 <<",\"lower_rows\":"<<lower_scan.size()<<",\"upper_rows\":"<<upper_scan.size()
                 <<",\"mismatches\":"<<mismatches<<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
CPP

F="$W/builds/u3split_20261029"; rm -rf "$F"; mkdir -p "$F"
g++ -pthread -std=c++17 -march=x86-64-v3 -O2 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" -I"$R/learned-index/src" -I"$D" \
  "$TEST" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" -o "$F/check" 2>"$F/build.err" \
  || { echo BUILD FAILED; tail -12 "$F/build.err"; git -C "$W" checkout -- "$D/bm_n1_page_core.hpp"; exit 1; }
for b in 70 35 100; do
  "$F/check" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" "$b" \
    || { echo "CHECK FAILED at $b"; git -C "$W" checkout -- "$D/bm_n1_page_core.hpp"; exit 1; }
done

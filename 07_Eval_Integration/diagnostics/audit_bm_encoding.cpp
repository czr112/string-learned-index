// Stage-3 audit: why the BM windows are wide, on real key types. Counts only, no timing.
// Rebuilds exactly the page partitioning the BM leaf uses (2048 rows, occupancy 15, page prefix,
// fit(1,7,8,1) on the block fences), fitting each page model once and reusing it.
#include "static_input.hpp"
#include "block_model.h"
#include "key_bytes.h"
#include "static_physical_index.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace uni=string_index_bench::unified;

struct Stats {
    std::vector<std::uint32_t> values;
    void add(std::uint64_t v) { values.push_back(std::uint32_t(v)); }
    void finish(std::uint64_t& mean,std::uint64_t& p50,std::uint64_t& p95,std::uint64_t& max) const {
        mean=p50=p95=max=0;
        if(values.empty())return;
        std::uint64_t total=0;for(auto v:values)total+=v;
        mean=total/values.size();
        std::vector<std::uint32_t> sorted=values;std::sort(sorted.begin(),sorted.end());
        p50=sorted[sorted.size()/2];
        p95=sorted[std::size_t(double(sorted.size()-1)*0.95)];
        max=sorted.back();
    }
};
struct PageAudit {
    std::size_t first=0,last=0;
    std::vector<std::string> fence_keys;      // owns the bytes the model's views point at
    std::vector<std::string_view> fences;     // suffix views after the page prefix
    std::string prefix;
    blocklearn::BlockModel model;
};

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin queries.bin");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::map<std::string,std::uint64_t> reference;
        for(const auto& r:rows)reference.emplace(r.key,r.value);

        constexpr std::size_t page_rows=2048, occupancy=15;
        std::vector<std::unique_ptr<PageAudit>> pages;
        for(std::size_t first=0;first<rows.size();first+=page_rows) {
            const auto last=std::min(first+page_rows,rows.size());
            auto page=std::make_unique<PageAudit>();
            page->first=first;page->last=last;
            page->prefix=rows[first].key.substr(0,blocklearn::common_prefix(rows[first].key,rows[last-1].key));
            for(std::size_t i=first;i<last;i+=occupancy)page->fence_keys.push_back(rows[i].key);
            page->fences.reserve(page->fence_keys.size());
            for(const auto& key:page->fence_keys)
                page->fences.push_back(std::string_view(key).substr(page->prefix.size()));
            page->model=blocklearn::BlockModel::fit(page->fences,1,7,8,1);
            pages.push_back(std::move(page));
        }

        Stats window_blocks,window_rows,collision_run,extra_prefix;
        std::uint64_t blocks=0,distinct_codes=0,fences_in_runs=0,run_count=0;
        std::uint64_t row_keys=0,predicted_hits=0,covered=0,uncovered=0;
        std::uint64_t segments=0,certified_pages=0;
        for(const auto& page:pages) {
            blocks+=page->fences.size();
            segments+=page->model.segment_count();
            if(page->model.certified())++certified_pages;
            for(std::size_t b=0;b<page->fences.size();++b) {
                const auto prediction=page->model.locate(page->fences[b]);
                window_blocks.add(prediction.window.last-prediction.window.first);
            }
            for(std::size_t i=page->first;i<page->last;++i) {
                const auto suffix=std::string_view(rows[i].key).substr(page->prefix.size());
                const auto prediction=page->model.locate(suffix);
                const auto containing=(i-page->first)/occupancy;
                window_rows.add(prediction.window.last-prediction.window.first);
                ++row_keys;
                if(prediction.predicted==containing)++predicted_hits;
                if(containing>=prediction.window.first&&containing<prediction.window.last)++covered;
                else ++uncovered;
            }
            std::size_t run_begin=0;
            const auto n=page->fences.size();
            while(run_begin<n) {
                const auto code=blocklearn::encode(page->fences[run_begin],0,7);
                std::size_t run_end=run_begin;
                while(run_end+1<n&&blocklearn::encode(page->fences[run_end+1],0,7)==code)++run_end;
                const auto length=run_end-run_begin+1;
                if(length>1) {
                    collision_run.add(length);fences_in_runs+=length;++run_count;
                    const auto common=blocklearn::common_prefix(page->fences[run_begin],page->fences[run_end]);
                    extra_prefix.add(common>=7?common-7:0);
                } else ++distinct_codes;
                run_begin=run_end+1;
            }
        }
        std::uint64_t absent_queries=0,absent_outside=0,absent_inside=0,absent_prefix_rejected=0;
        for(std::size_t qi=0;qi<queries.size();++qi) {
            if(qi%10!=9)continue;
            std::string key=queries[qi].key;key+="~f2/miss";
            while(reference.count(key))key+="~";
            ++absent_queries;
            std::size_t lo=0,hi=pages.size();
            while(lo<hi){const auto mid=lo+(hi-lo)/2;
                if(static_physical::compare(rows[pages[mid]->first].key,key)<=0)lo=mid+1;else hi=mid;}
            const auto& page=*pages[lo?lo-1:0];
            if(key.size()<page.prefix.size()||
               blocklearn::compare(std::string_view(key).substr(0,page.prefix.size()),
                                   std::string_view(page.prefix))!=0){++absent_prefix_rejected;continue;}
            const auto suffix=std::string_view(key).substr(page.prefix.size());
            const auto prediction=page.model.locate(suffix);
            std::size_t pred=0;
            for(std::size_t b=0;b<page.fences.size();++b)
                if(blocklearn::compare(page.fences[b],suffix)<=0)pred=b;
            if(pred>=prediction.window.first&&pred<prediction.window.last)++absent_inside;
            else ++absent_outside;
        }
        std::uint64_t a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p;
        window_blocks.finish(a,b,c,d);window_rows.finish(e,f,g,h);
        collision_run.finish(i,j,k,l);extra_prefix.finish(m,n,o,p);
        std::cout<<"{\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()<<",\"pages\":"<<pages.size()
            <<",\"blocks\":"<<blocks
            <<",\"window_blocks\":{\"mean\":"<<a<<",\"p50\":"<<b<<",\"p95\":"<<c<<",\"max\":"<<d<<"}"
            <<",\"window_rows\":{\"mean\":"<<e<<",\"p50\":"<<f<<",\"p95\":"<<g<<",\"max\":"<<h<<"}"
            <<",\"segments\":{\"total\":"<<segments<<",\"per_page\":"<<(pages.empty()?0.0:double(segments)/pages.size())<<"}"
            <<",\"certified_pages\":"<<certified_pages
            <<",\"predicted_is_containing\":{\"keys\":"<<row_keys<<",\"hits\":"<<predicted_hits
            <<",\"rate\":"<<(row_keys?double(predicted_hits)/row_keys:0.0)<<"}"
            <<",\"window_covers_containing\":{\"inside\":"<<covered<<",\"outside\":"<<uncovered
            <<",\"rate\":"<<(row_keys?double(covered)/row_keys:0.0)<<"}"
            <<",\"absent\":{\"queries\":"<<absent_queries<<",\"prefix_rejected\":"<<absent_prefix_rejected
            <<",\"pred_inside_window\":"<<absent_inside<<",\"pred_outside_window\":"<<absent_outside
            <<",\"outside_rate\":"<<(absent_queries?double(absent_outside)/absent_queries:0.0)<<"}"
            <<",\"collisions\":{\"fences_in_runs\":"<<fences_in_runs<<",\"runs\":"<<run_count
            <<",\"share\":"<<(blocks?double(fences_in_runs)/blocks:0.0)
            <<",\"run_mean\":"<<i<<",\"run_p50\":"<<j<<",\"run_p95\":"<<k<<",\"run_max\":"<<l
            <<",\"extra_prefix_mean\":"<<m<<",\"extra_prefix_p50\":"<<n
            <<",\"extra_prefix_p95\":"<<o<<",\"extra_prefix_max\":"<<p<<"}"
            <<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

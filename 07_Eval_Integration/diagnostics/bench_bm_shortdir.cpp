// Short-directory ablation on the BM physical leaf page: correctness, fence work, and a paired
// page-level timing of the routing choice. The root routing is precomputed for every query so
// the measurement isolates positioning, which is what this ablation changes.
#include "bm_shortdir_core.hpp"
#include "bm_page_core.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace uni=string_index_bench::unified;
using Clock=std::chrono::steady_clock;

static volatile std::uint64_t sink=0;
static std::uint64_t token(std::optional<std::uint64_t> r) {
    return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
static std::vector<std::uint32_t> permutation(std::size_t n,std::uint64_t repeat) {
    std::vector<std::uint32_t> p(n);std::iota(p.begin(),p.end(),std::uint32_t(0));
    std::mt19937_64 rng(UINT64_C(0x9e3779b97f4a7c15)^(repeat*UINT64_C(0x100000001b3))^std::uint64_t(n));
    std::shuffle(p.begin(),p.end(),rng);return p;
}

int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::invalid_argument("rows.bin queries.bin mode(entry|time) process_offset");
        const std::string mode=argv[3];
        if(mode!="entry"&&mode!="time")throw std::invalid_argument("unknown mode");
        const int offset=std::stoi(argv[4]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});

        constexpr std::size_t page_rows=2048;
        struct Chunk {std::unique_ptr<bm_leaf::Page> frozen;std::unique_ptr<bm_shortdir::Page> copy;
                      std::string lower;std::size_t first=0,last=0;};
        std::vector<Chunk> chunks;
        const auto build_start=Clock::now();
        for(std::size_t first=0;first<physical.size();first+=page_rows) {
            const auto last=std::min(first+page_rows,physical.size());
            std::vector<static_physical::Row> part(physical.begin()+first,physical.begin()+last);
            Chunk chunk;chunk.first=first;chunk.last=last;
            chunk.lower=first?physical[first].key:std::string();
            if(mode=="entry")chunk.frozen=std::make_unique<bm_leaf::Page>(part,15);
            chunk.copy=std::make_unique<bm_shortdir::Page>(part,15);
            chunks.push_back(std::move(chunk));
        }
        const auto build_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-build_start).count());
        auto route=[&](std::string_view key) {
            std::size_t lo=0,hi=chunks.size();
            while(lo<hi) {const auto mid=lo+(hi-lo)/2;if(static_physical::compare(chunks[mid].lower,key)<=0)lo=mid+1;else hi=mid;}
            return std::size_t(lo?lo-1:0);
        };

        if(mode=="entry") {
            std::uint64_t keys_checked=0,queries_checked=0,mismatch_bm=0,mismatch_short=0,mismatch_counted=0,mismatch_answer=0;
            std::uint64_t memory_frozen=0,memory_copy=0,short_bytes=0,useful_pages=0;
            bm_leaf::Work wbm;bm_shortdir::Work wshort;bm_shortdir::Work wcurrent;
            for(const auto& chunk:chunks) {
                memory_frozen+=chunk.frozen->memory().total();
                memory_copy+=chunk.copy->memory().total();
                short_bytes+=chunk.copy->short_directory_bytes();
                if(chunk.copy->short_useful())++useful_pages;
            }
            bool reported=false;
            auto report=[&](const char* what,std::size_t page,std::string_view key,
                            std::uint32_t exact,std::optional<std::uint32_t> counted,
                            std::uint32_t short_begin,std::uint32_t bm_begin) {
                if(!reported) {
                    std::cerr<<"mismatch "<<what<<" page="<<page<<" key="<<std::quoted(std::string(key))
                        <<" exact="<<exact<<" short="<<short_begin<<" bm="<<bm_begin
                        <<" counted="<<(counted?std::to_string(*counted):"none")<<"\n";
                    reported=true;
                }
            };
            auto check=[&](std::size_t page,std::string_view key,bool counted) {
                auto& frozen=*chunks[page].frozen;auto& copy=*chunks[page].copy;
                const auto exact=copy.location_exact(key);
                const auto bm_copy=copy.location_bm(key);
                const auto bm_frozen=frozen.locate_blocks_single<bm_leaf::Lookup::learned>(key);
                if(bm_copy.begin!=bm_frozen.begin||bm_copy.end!=bm_frozen.end)++mismatch_bm;
                if(bm_copy.begin!=exact.begin||bm_copy.end!=exact.end)++mismatch_bm;
                const auto short_location=copy.location_short(key);
                if(short_location.begin!=exact.begin||short_location.end!=exact.end) {
                    ++mismatch_short;report("short-vs-exact",page,key,exact.begin,std::nullopt,
                                            short_location.begin,bm_copy.begin);
                }
                const auto counted_route=counted?copy.counted_route(key,&wshort):copy.counted_route(key);
                // The replica must predict the upstream outcome in both branches: a bracket hit
                // names the predecessor itself, a failed bracket must equal the fallback result.
                if(counted_route) {
                    if(short_location.begin!=*counted_route) {
                        ++mismatch_counted;report("counted-hit",page,key,exact.begin,counted_route,
                                                  short_location.begin,bm_copy.begin);
                    }
                } else if(short_location.begin!=bm_copy.begin) {
                    ++mismatch_counted;report("counted-fallback",page,key,exact.begin,counted_route,
                                              short_location.begin,bm_copy.begin);
                }
                const auto answer_exact=copy.find_exact(key);
                if(copy.find_short(key)!=answer_exact)++mismatch_answer;
                if(copy.find_model(key)!=answer_exact)++mismatch_answer;
                if(counted) {
                    if(copy.find_short(key,&wshort)!=answer_exact)++mismatch_answer;
                    if(copy.find_model(key,&wcurrent)!=answer_exact)++mismatch_answer;
                    if(frozen.find_base(key,&wbm)!=answer_exact)++mismatch_answer;
                }
                ++keys_checked;
            };
            for(std::size_t page=0;page<chunks.size();++page)
                for(std::size_t i=chunks[page].first;i<chunks[page].last;++i)check(page,physical[i].key,false);
            for(const auto& q:queries){check(route(q.key),q.key,true);++queries_checked;}
            if(mismatch_bm||mismatch_short||mismatch_counted||mismatch_answer)
                throw std::logic_error("positioning disagreement");

            const auto q=double(queries_checked);
            const auto current_compares=wcurrent.bm_boundary_compares+wcurrent.directory_steps;
            const auto short_fence=wshort.bm_boundary_compares+wshort.short_full_compares;
            const auto short_numeric=wshort.short_steps+wshort.short_bracket_compares+wshort.short_prefix_compares;
            const auto short_total=short_fence+wshort.directory_steps+short_numeric;
            std::cout<<"{\"mode\":\"entry\",\"rows\":"<<physical.size()<<",\"queries\":"<<queries_checked
                <<",\"pages\":"<<chunks.size()<<",\"keys_checked\":"<<keys_checked
                <<",\"useful_pages\":"<<useful_pages
                <<",\"mismatch\":{\"bm\":"<<mismatch_bm<<",\"short\":"<<mismatch_short
                <<",\"counted\":"<<mismatch_counted<<",\"answer\":"<<mismatch_answer<<"}"
                <<",\"memory\":{\"frozen\":"<<memory_frozen<<",\"copy\":"<<memory_copy
                <<",\"short_directory\":"<<short_bytes<<"}"
                <<",\"current\":{\"fence_compares\":"<<wcurrent.bm_boundary_compares
                <<",\"fence_per_query\":"<<double(wcurrent.bm_boundary_compares)/q
                <<",\"directory_steps\":"<<wcurrent.directory_steps
                <<",\"total_compares\":"<<current_compares<<",\"total_per_query\":"<<double(current_compares)/q
                <<",\"blocks\":"<<wcurrent.blocks<<"}"
                <<",\"short\":{\"fence_compares\":"<<short_fence<<",\"fence_per_query\":"<<double(short_fence)/q
                <<",\"numeric_compares\":"<<short_numeric<<",\"numeric_per_query\":"<<double(short_numeric)/q
                <<",\"search_steps\":"<<wshort.short_steps<<",\"bracket_compares\":"<<wshort.short_bracket_compares
                <<",\"prefix_compares\":"<<wshort.short_prefix_compares
                <<",\"fallback_steps\":"<<wshort.directory_steps
                <<",\"total_compares\":"<<short_total<<",\"total_per_query\":"<<double(short_total)/q
                <<",\"hits\":"<<wshort.short_hits<<",\"fallbacks\":"<<wshort.short_fallbacks
                <<",\"hit_rate\":"<<double(wshort.short_hits)/q<<",\"blocks\":"<<wshort.blocks<<"}"
                <<",\"build_ns\":"<<build_ns<<"}\n";
            return 0;
        }

        // time: routing precomputed, six arms on one object set, paired per repeat.
        std::vector<std::uint32_t> page_of(queries.size());
        for(std::size_t i=0;i<queries.size();++i)page_of[i]=std::uint32_t(route(queries[i].key));
        const char* names[]={"current","current_AA","short","short_AA","directory","directory_AA"};
        constexpr int williams[]={0,1,5,2,4,3};
        auto pass=[&](int arm,const std::vector<std::uint32_t>& order) {
            std::uint64_t c=0;
            for(const auto i:order) {
                auto& page=*chunks[page_of[i]].copy;
                switch(arm) {
                    case 0:case 1:c=c*131+token(page.find_model(queries[i].key));break;
                    case 2:case 3:c=c*131+token(page.find_short(queries[i].key));break;
                    default:c=c*131+token(page.find_exact(queries[i].key));break;
                }
            }
            return c;
        };
        for(int arm=0;arm<6;++arm)pass(arm,permutation(queries.size(),0));sink=1;
        std::cout<<"{\"mode\":\"gate\",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"pages\":"<<chunks.size()<<",\"build_ns\":"<<build_ns<<"}\n";
        for(std::size_t repeat=0;repeat<6;++repeat) {
            const auto order=permutation(queries.size(),repeat);
            std::uint64_t checksums[6];
            for(int position=0;position<6;++position) {
                const auto arm=(williams[position]+int(repeat)+offset)%6;
                const auto start=Clock::now();
                const auto checksum=pass(arm,order);
                const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                checksums[arm]=checksum;sink=checksum;
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position
                    <<",\"arm\":\""<<names[arm]<<"\",\"operations\":"<<queries.size()
                    <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
            for(int arm=1;arm<6;++arm)if(checksums[arm]!=checksums[0])throw std::logic_error("arm checksum differs");
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Phase-2 entry: predicted-block payload probe. Counts only; no timing in this round.
// Every key and query is checked against the frozen BM page and the full byte directory.
#include "bm_probe_page.hpp"
#include "bm_page_core.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace uni=string_index_bench::unified;

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin queries.bin");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        std::map<std::string,std::uint64_t> reference;
        for(const auto& r:rows){physical.push_back({r.key,r.value});reference.emplace(r.key,r.value);}
        std::vector<uni::Query> mixed;mixed.reserve(queries.size());
        for(std::size_t i=0;i<queries.size();++i) {
            if(i%10==9){std::string key=queries[i].key;key+="~f2/miss";
                while(reference.count(key))key+="~";mixed.push_back({std::move(key),false,0});}
            else mixed.push_back(queries[i]);
        }

        constexpr std::size_t page_rows=2048;
        struct Chunk {std::unique_ptr<bm_leaf::Page> frozen;std::unique_ptr<bm_probe::Page> copy;
                      std::string lower;std::size_t first=0,last=0;};
        std::vector<Chunk> chunks;
        for(std::size_t first=0;first<physical.size();first+=page_rows) {
            const auto last=std::min(first+page_rows,physical.size());
            std::vector<static_physical::Row> part(physical.begin()+first,physical.begin()+last);
            Chunk chunk;chunk.first=first;chunk.last=last;chunk.lower=first?physical[first].key:std::string();
            chunk.frozen=std::make_unique<bm_leaf::Page>(part,15);
            chunk.copy=std::make_unique<bm_probe::Page>(part,15);
            chunks.push_back(std::move(chunk));
        }
        auto route=[&](std::string_view key) {
            std::size_t lo=0,hi=chunks.size();
            while(lo<hi){const auto mid=lo+(hi-lo)/2;if(static_physical::compare(chunks[mid].lower,key)<=0)lo=mid+1;else hi=mid;}
            return std::size_t(lo?lo-1:0);
        };
        std::uint64_t keys_checked=0,mismatch=0;
        auto check=[&](std::size_t page,std::string_view key) {
            auto& frozen=*chunks[page].frozen;auto& copy=*chunks[page].copy;
            const auto exact=frozen.find_exact(key);
            const auto current=frozen.find_model(key);
            const auto copy_current=copy.find_model(key);
            const auto probe=copy.find_probe(key);
            if(current!=exact||copy_current!=exact||probe!=exact)++mismatch;
            ++keys_checked;
        };
        for(std::size_t page=0;page<chunks.size();++page)
            for(std::size_t i=chunks[page].first;i<chunks[page].last;++i)check(page,physical[i].key);
        for(const auto& q:mixed)check(route(q.key),q.key);
        if(mismatch)throw std::logic_error("probe disagrees with the exact reference");

        bm_leaf::Work current_work;bm_probe::Work probe_work;
        for(const auto& q:mixed) {
            auto& frozen=*chunks[route(q.key)].frozen;
            frozen.find_model(q.key,&current_work);
        }
        for(const auto& q:mixed) {
            auto& copy=*chunks[route(q.key)].copy;
            copy.find_probe(q.key,&probe_work);
        }
        const auto q=double(mixed.size());
        const auto current_fence=current_work.bm_boundary_compares+current_work.directory_steps;
        const auto probe_fence=probe_work.bm_boundary_compares+probe_work.directory_steps;
        const auto current_blocks=current_work.blocks;
        const auto probe_blocks=probe_work.probe_block_loads+probe_work.blocks;
        const auto current_exact=current_work.exact;
        const auto probe_exact=probe_work.probe_key_compares+probe_work.exact;
        std::cout<<"{\"rows\":"<<physical.size()<<",\"queries\":"<<mixed.size()<<",\"pages\":"<<chunks.size()
            <<",\"keys_checked\":"<<keys_checked<<",\"mismatch\":"<<mismatch
            <<",\"current\":{\"fence_compares\":"<<current_fence<<",\"fence_per_query\":"<<double(current_fence)/q
            <<",\"blocks\":"<<current_blocks<<",\"blocks_per_query\":"<<double(current_blocks)/q
            <<",\"key_compares\":"<<current_exact<<",\"key_per_query\":"<<double(current_exact)/q
            <<",\"bm_calls\":"<<current_work.bm_calls<<"}"
            <<",\"probe\":{\"calls\":"<<probe_work.probe_calls<<",\"hits\":"<<probe_work.probe_hits
            <<",\"misses\":"<<probe_work.probe_misses
            <<",\"hit_rate\":"<<(probe_work.probe_calls?double(probe_work.probe_hits)/double(probe_work.probe_calls):0)
            <<",\"fallbacks\":"<<probe_work.fallback_calls
            <<",\"probe_block_loads\":"
            <<probe_work.probe_block_loads<<",\"probe_tag_candidates\":"<<probe_work.probe_tag_candidates
            <<",\"probe_length_rejects\":"<<probe_work.probe_length_rejects
            <<",\"probe_key_compares\":"<<probe_work.probe_key_compares
            <<",\"probe_key_bytes\":"<<probe_work.probe_key_bytes
            <<",\"skipped_block_scans\":"<<probe_work.skipped_block_scans
            <<",\"fence_compares\":"<<probe_fence<<",\"fence_per_query\":"<<double(probe_fence)/q
            <<",\"blocks\":"<<probe_blocks<<",\"blocks_per_query\":"<<double(probe_blocks)/q
            <<",\"key_compares\":"<<probe_exact<<",\"key_per_query\":"<<double(probe_exact)/q
            <<",\"bm_calls\":"<<probe_work.bm_calls<<"}"
            <<",\"delta\":{\"fence_per_query\":"<<(double(probe_fence)-double(current_fence))/q
            <<",\"blocks_per_query\":"<<(double(probe_blocks)-double(current_blocks))/q
            <<",\"key_per_query\":"<<(double(probe_exact)-double(current_exact))/q<<"}"
            <<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Entry diagnosis for predicted-first bracket positioning on the BM physical leaf page.
// Builds one page pair per 2048-row chunk from the same rows, then for every key and every
// frozen query asserts that the current correction path, the bracket path and the full byte
// directory all name the same predecessor block, and records the fence work of each path.
#include "bm_bracket_core.hpp"
#include "bm_page_core.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin queries.bin");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});

        constexpr std::size_t page_rows=2048;
        struct Chunk {std::unique_ptr<bm_leaf::Page> frozen;std::unique_ptr<bm_bracket::Page> copy;
                      std::string lower;std::size_t first=0,last=0;};
        std::vector<Chunk> chunks;
        for(std::size_t first=0;first<physical.size();first+=page_rows) {
            const auto last=std::min(first+page_rows,physical.size());
            std::vector<static_physical::Row> part(physical.begin()+first,physical.begin()+last);
            Chunk chunk;chunk.first=first;chunk.last=last;
            chunk.lower=first?physical[first].key:std::string();
            chunk.frozen=std::make_unique<bm_leaf::Page>(part,15);
            chunk.copy=std::make_unique<bm_bracket::Page>(part,15);
            chunks.push_back(std::move(chunk));
        }
        auto route=[&](std::string_view key) {
            std::size_t lo=0,hi=chunks.size();
            while(lo<hi) {const auto mid=lo+(hi-lo)/2;if(static_physical::compare(chunks[mid].lower,key)<=0)lo=mid+1;else hi=mid;}
            return (lo?lo-1:0);
        };

        std::uint64_t keys_checked=0,queries_checked=0,mismatch_exact=0,mismatch_current=0,mismatch_bracket=0,mismatch_answer=0;
        std::uint64_t memory_frozen=0,memory_copy=0;
        bm_leaf::Work wbase;bm_bracket::Work wcurrent,wbracket;
        for(const auto& chunk:chunks) {
            memory_frozen+=chunk.frozen->memory().total();
            memory_copy+=chunk.copy->memory().total();
        }
        auto check_position=[&](std::size_t page,std::string_view key,bool counted) {
            auto& frozen=*chunks[page].frozen;auto& copy=*chunks[page].copy;
            const auto exact=frozen.locate_blocks_single<bm_leaf::Lookup::directory>(key);
            const auto exact_copy=copy.locate_blocks_single<bm_bracket::Lookup::directory>(key);
            if(exact.begin!=exact_copy.begin||exact.end!=exact_copy.end||
               int(exact.state)!=int(exact_copy.state))++mismatch_exact;
            const auto current=frozen.locate_blocks_single<bm_leaf::Lookup::learned>(key);
            const auto current_copy=copy.locate_blocks_single<bm_bracket::Lookup::learned>(key);
            if(current.begin!=current_copy.begin||current.end!=current_copy.end)++mismatch_current;
            if(current_copy.begin!=exact_copy.begin||current_copy.end!=exact_copy.end)++mismatch_current;
            const auto bracket=copy.locate_bracket_single(key,nullptr);
            if(bracket.begin!=exact_copy.begin||bracket.end!=exact_copy.end)++mismatch_bracket;
            const auto answer_exact=copy.find_exact(key);
            if(copy.find_bracket(key,nullptr)!=answer_exact)++mismatch_answer;
            if(counted) {
                if(copy.find_bracket(key,&wbracket)!=answer_exact)++mismatch_answer;
                if(copy.find_model(key,&wcurrent)!=answer_exact)++mismatch_answer;
                if(frozen.find_base(key,&wbase)!=answer_exact)++mismatch_answer;
            }
            ++keys_checked;
        };
        for(std::size_t page=0;page<chunks.size();++page)
            for(std::size_t i=chunks[page].first;i<chunks[page].last;++i)
                check_position(page,physical[i].key,false);
        for(const auto& q:queries) {
            check_position(route(q.key),q.key,true);
            ++queries_checked;
        }
        if(mismatch_exact||mismatch_current||mismatch_bracket||mismatch_answer)
            throw std::logic_error("positioning disagreement");

        auto mean=[&](std::uint64_t total) {return double(total)/double(queries_checked);};
        const auto current_compares=wcurrent.bm_boundary_compares+wcurrent.directory_steps;
        const auto bracket_compares=wbracket.bracket_boundary_compares+wbracket.directory_steps;
        const auto base_compares=wbase.bm_boundary_compares+wbase.directory_steps;
        std::cout<<"{\"rows\":"<<physical.size()<<",\"queries\":"<<queries_checked<<",\"pages\":"<<chunks.size()
            <<",\"keys_checked\":"<<keys_checked
            <<",\"mismatch\":{\"exact\":"<<mismatch_exact<<",\"current\":"<<mismatch_current
            <<",\"bracket\":"<<mismatch_bracket<<",\"answer\":"<<mismatch_answer<<"}"
            <<",\"memory\":{\"frozen\":"<<memory_frozen<<",\"copy\":"<<memory_copy<<"}"
            <<",\"base\":{\"boundary_compares\":"<<wbase.bm_boundary_compares
            <<",\"directory_steps\":"<<wbase.directory_steps<<",\"total_compares\":"<<base_compares
            <<",\"compares_per_query\":"<<mean(base_compares)<<",\"blocks\":"<<wbase.blocks
            <<",\"outside\":"<<wbase.bm_outside<<"}"
            <<",\"current\":{\"boundary_compares\":"<<wcurrent.bm_boundary_compares
            <<",\"directory_steps\":"<<wcurrent.directory_steps<<",\"total_compares\":"<<current_compares
            <<",\"compares_per_query\":"<<mean(current_compares)<<",\"blocks\":"<<wcurrent.blocks
            <<",\"outside\":"<<wcurrent.bm_outside<<",\"local_searches\":"<<wcurrent.bm_local_searches
            <<",\"proposal_blocks\":"<<wcurrent.bm_proposal_blocks<<"}"
            <<",\"bracket\":{\"boundary_compares\":"<<wbracket.bracket_boundary_compares
            <<",\"search_steps\":"<<wbracket.bracket_search_steps
            <<",\"total_compares\":"<<bracket_compares
            <<",\"compares_per_query\":"<<mean(bracket_compares)<<",\"blocks\":"<<wbracket.blocks
            <<",\"checks\":"<<wbracket.bracket_checks<<",\"hits\":"<<wbracket.bracket_hits
            <<",\"left\":"<<wbracket.bracket_left<<",\"right\":"<<wbracket.bracket_right
            <<",\"out_of_range\":"<<wbracket.bracket_out_of_range
            <<",\"search_calls\":"<<wbracket.bracket_search_calls
            <<",\"hit_rate\":"<<(wbracket.bracket_checks?double(wbracket.bracket_hits)/double(wbracket.bracket_checks):0.0)<<"}"
            <<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

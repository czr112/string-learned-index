// N3 space gate: baseline versus block-split candidate on identical content.
#include "bm_n1_index.hpp"
#include "bm_bounded_index.hpp"
#include "static_input.hpp"
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace uni=string_index_bench::unified;

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("rows.bin");
        auto rs=static_blocks::buffered(argv[1]);
        const auto rows=uni::read_rows(rs);
        if(rows.empty())throw std::invalid_argument("empty rows");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        std::map<std::string,std::uint64_t> content;
        for(const auto& r:rows){physical.push_back({r.key,r.value});content.emplace(r.key,r.value);}

        bm_bounded_page::Index baseline(physical);
        bm_n1_page::Index candidate(physical);
        const auto build_base=baseline.memory_bytes(),build_cand=candidate.memory_bytes();

        // Same 400 inserts on both, then compare again.
        for(int i=0;i<400;++i) {
            std::string key=rows[(std::size_t(i)*rows.size())/400].key+"~space/"+std::to_string(i);
            const bool a=baseline.insert(key,5'000+i);
            const bool b=candidate.insert(key,5'000+i);
            if(a!=b)throw std::logic_error("insert disagreement");
            if(a)content.emplace(key,5'000+i);
        }
        const auto mut_base=baseline.memory_bytes(),mut_cand=candidate.memory_bytes();

        // Pure structural comparison: both rebuilt from the same final map.
        std::vector<static_physical::Row> final_rows;final_rows.reserve(content.size());
        for(const auto& [key,value]:content)final_rows.push_back({key,value});
        bm_bounded_page::Index rebuilt_base(final_rows);
        bm_n1_page::Index rebuilt_cand(final_rows);
        const auto same_base=rebuilt_base.memory_bytes(),same_cand=rebuilt_cand.memory_bytes();

        std::cout<<"{\"rows\":"<<rows.size()
            <<",\"build\":{\"baseline\":"<<build_base<<",\"candidate\":"<<build_cand
            <<",\"delta\":"<<(std::int64_t(build_cand)-std::int64_t(build_base))
            <<",\"percent\":"<<(build_base?100.0*double(std::int64_t(build_cand)-std::int64_t(build_base))/build_base:0.0)<<"}"
            <<",\"after_inserts\":{\"baseline\":"<<mut_base<<",\"candidate\":"<<mut_cand
            <<",\"percent\":"<<(mut_base?100.0*double(std::int64_t(mut_cand)-std::int64_t(mut_base))/mut_base:0.0)<<"}"
            <<",\"same_final_map\":{\"baseline\":"<<same_base<<",\"candidate\":"<<same_cand
            <<",\"delta\":"<<(std::int64_t(same_cand)-std::int64_t(same_base))
            <<",\"percent\":"<<(same_base?100.0*double(std::int64_t(same_cand)-std::int64_t(same_base))/same_base:0.0)<<"}"
            <<",\"final_rows\":"<<content.size()<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

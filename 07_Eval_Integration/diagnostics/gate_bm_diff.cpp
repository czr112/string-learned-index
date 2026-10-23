// Differential gate: the candidate must be structurally identical to the baseline at build time
// (page count, fences, model inputs, memory) and answer identically everywhere; after mutations
// only the maintenance counters are allowed to diverge, answers must not.
#include "bm_n1_index.hpp"
#include "bm_bounded_index.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::pair<std::string,std::uint64_t>> read_rows(const char* path) {
    std::ifstream in(path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<std::pair<std::string,std::uint64_t>> rows;rows.reserve(count);
    for(std::uint64_t i=0;i<count;++i) {
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        std::string key=bytes.substr(pos,n);pos+=n;
        std::uint64_t value=0;std::memcpy(&value,bytes.data()+pos,8);pos+=8;
        rows.emplace_back(std::move(key),value);
    }
    return rows;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("rows.bin");
        const auto initial=read_rows(argv[1]);
        std::vector<static_physical::Row> rows;rows.reserve(initial.size());
        std::map<std::string,std::uint64_t> reference;
        for(const auto& [k,v]:initial){rows.push_back({k,v});reference.emplace(k,v);}

        bm_n1_page::Index candidate(rows);
        bm_bounded_page::Index baseline(rows);
        std::uint64_t checks=0,mismatch=0;
        auto require=[&](bool ok,const char* what){++checks;if(!ok){++mismatch;std::cerr<<"DIFF "<<what<<'\n';}};

        // Build-time structure must be identical.
        require(candidate.page_count()==baseline.page_count(),"page count");
        require(candidate.memory_bytes()==baseline.memory_bytes(),"memory bytes");
        const auto cand_fences=candidate.fence_keys();
        const auto base_fences=baseline.fence_keys();
        require(cand_fences.size()==base_fences.size(),"fence count");
        for(std::size_t i=0;i<std::min(cand_fences.size(),base_fences.size());++i)
            require(cand_fences[i]==base_fences[i],"fence value");
        require(candidate.verify()==baseline.verify(),"verify check count");
        for(const auto& [k,v]:initial) {
            require(candidate.find_base(k)==baseline.find_base(k),"answer at build");
            require(candidate.find(k)==baseline.find(k),"window answer at build");
            require(candidate.find_base(k)==std::optional<std::uint64_t>(v),"candidate oracle");
            require(baseline.find_base(k)==std::optional<std::uint64_t>(v),"baseline oracle");
        }
        // Mutation phase: identical answers, diverging counters allowed.
        std::uint64_t inserted=0;
        for(int i=0;i<800;++i) {
            std::string key=initial[(std::size_t(i)*initial.size())/800].first+"~diff/"+std::to_string(i);
            const bool a=candidate.insert(key,6'000+i);
            const bool b=baseline.insert(key,6'000+i);
            require(a==b,"insert agreement");
            if(a){reference.emplace(key,6'000+i);++inserted;}
        }
        for(const auto& [k,v]:reference) {
            require(candidate.find_base(k)==std::optional<std::uint64_t>(v),"candidate after mutations");
            require(baseline.find_base(k)==std::optional<std::uint64_t>(v),"baseline after mutations");
        }
        const auto cm=candidate.maintenance();const auto bm=baseline.maintenance();
        std::cout<<"{\"rows\":"<<rows.size()<<",\"checks\":"<<checks<<",\"mismatches\":"<<mismatch
            <<",\"inserted\":"<<inserted
            <<",\"pages\":{\"candidate\":"<<candidate.page_count()<<",\"baseline\":"<<baseline.page_count()<<"}"
            <<",\"rebuilds\":{\"candidate\":"<<cm.rebuilds<<",\"baseline\":"<<bm.rebuilds<<"}"
            <<",\"inplace\":{\"candidate\":"<<cm.inplace_inserts<<",\"baseline\":"<<bm.inplace_inserts<<"}"
            <<",\"rows_rebuilt\":{\"candidate\":"<<cm.rows_rebuilt<<",\"baseline\":"<<bm.rows_rebuilt<<"}"
            <<",\"final_scan_rows\":"<<candidate.scan("",SIZE_MAX).size()<<"}\n";
        return mismatch?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

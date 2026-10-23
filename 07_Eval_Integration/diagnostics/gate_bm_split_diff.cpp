// Split-path differential: drive many inserts into the *same* block so the rebalance actually
// fires, compare answers against the baseline after every step, and measure the resulting
// complete-find cost before and after.
#include "bm_n1_index.hpp"
#include "bm_bounded_index.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
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
template<class Index>
double find_us(const Index& index,const std::vector<std::string>& keys,std::uint64_t& sink) {
    const auto start=Clock::now();
    for(const auto& key:keys) {const auto got=index.find_base(key);sink+=(got?*got:0);}
    const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
    return double(ns)/double(keys.size())/1000.0;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin dense_inserts");
        const auto dense=std::stoul(argv[2]);
        const auto initial=read_rows(argv[1]);
        std::vector<static_physical::Row> rows;rows.reserve(initial.size());
        std::map<std::string,std::uint64_t> reference;
        for(const auto& [k,v]:initial){rows.push_back({k,v});reference.emplace(k,v);}
        bm_n1_page::Index candidate(rows);
        bm_bounded_page::Index baseline(rows);
        std::uint64_t checks=0,mismatch=0;
        auto require=[&](bool ok,const char* what){++checks;if(!ok){++mismatch;std::cerr<<"DIFF "<<what<<'\n';}};

        std::vector<std::string> probe;
        probe.reserve(5000);
        for(std::size_t i=0;i<initial.size();i+=initial.size()/5000+1)probe.push_back(initial[i].first);
        std::uint64_t sink=0;
        const auto find_before_cand=find_us(candidate,probe,sink);
        const auto find_before_base=find_us(baseline,probe,sink);

        // All inserts share one source key, so they land in the same block and force splits.
        const std::string root=initial[initial.size()/2].first;
        std::uint64_t inserted=0,splits_expected=0;
        for(std::size_t i=0;i<dense;++i) {
            const std::string key=root+"~dense/"+std::to_string(i);
            const bool a=candidate.insert(key,9'000+i);
            const bool b=baseline.insert(key,9'000+i);
            require(a==b,"insert agreement in dense batch");
            if(a){reference.emplace(key,9'000+i);++inserted;}
        }
        for(const auto& [k,v]:reference) {
            require(candidate.find_base(k)==std::optional<std::uint64_t>(v),"candidate answer after dense batch");
        }
        const auto find_after_cand=find_us(candidate,probe,sink);
        const auto find_after_base=find_us(baseline,probe,sink);
        const auto cm=candidate.maintenance();
        const auto bm=baseline.maintenance();
        std::cout<<"{\"rows\":"<<rows.size()<<",\"dense\":"<<dense<<",\"inserted\":"<<inserted
            <<",\"checks\":"<<checks<<",\"mismatches\":"<<mismatch
            <<",\"find_us\":{\"candidate_before\":"<<find_before_cand<<",\"candidate_after\":"<<find_after_cand
            <<",\"baseline_before\":"<<find_before_base<<",\"baseline_after\":"<<find_after_base<<"}"
            <<",\"ratio\":{\"candidate_after_over_before\":"<<(find_before_cand?find_after_cand/find_before_cand:0)
            <<",\"candidate_over_baseline_after\":"<<(find_after_base?find_after_cand/find_after_base:0)<<"}"
            <<",\"rebuilds\":{\"candidate\":"<<cm.rebuilds<<",\"baseline\":"<<bm.rebuilds<<"}"
            <<",\"inplace\":{\"candidate\":"<<cm.inplace_inserts<<",\"baseline\":"<<bm.inplace_inserts<<"}"
            <<",\"executed\":"<<candidate.scan("",SIZE_MAX).size()<<"}\n";
        return mismatch?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

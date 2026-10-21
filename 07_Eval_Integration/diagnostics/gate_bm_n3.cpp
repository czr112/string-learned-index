// N3 gate: semantics, verify(), scan lifetime and space accounting for the block-rebalance
// candidate (bm_n1_page with block splits enabled).
#include "bm_n1_index.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
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
        std::map<std::string,std::uint64_t> reference;
        for(const auto& r:rows)reference.emplace(r.key,r.value);

        std::uint64_t checks=0,mismatch=0;
        auto require=[&](bool ok,const char* what){++checks;if(!ok){++mismatch;std::cerr<<"FAIL "<<what<<'\n';}};

        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});
        bm_n1_page::Index index(physical);
        const auto base_bytes=index.memory_bytes();
        require(index.verify()>0,"verify after build");
        for(const auto& r:rows)require(index.find_base(r.key)==std::optional<std::uint64_t>(r.value),"row oracle");
        for(const auto& q:queries) {
            const auto found=index.find_base(q.key);
            require(found.has_value()==q.expected_present,"query presence");
            if(q.expected_present)require(found&&*found==q.expected_value,"query value");
        }
        std::vector<std::string> probes{"",std::string(1,'\0'),std::string(1,char(255)),
            std::string("a\0b",3),std::string(4096,'z')};
        for(std::size_t i=0;i<rows.size();i+=997)probes.push_back(rows[i].key+"~absent");
        for(const auto& probe:probes) {
            const auto want=reference.find(probe);
            const auto got=index.find_base(probe);
            require(got.has_value()==(want!=reference.end()),"absent presence");
            if(want!=reference.end())require(got&&*got==want->second,"absent value");
        }
        auto scanned=index.scan("",SIZE_MAX);
        require(scanned.size()==rows.size(),"full scan size");
        for(std::size_t i=0;i<scanned.size();++i)
            require(scanned[i].key==rows[i].key&&scanned[i].value==rows[i].value,"scan row");

        // Mutations: the block split path must survive inserts, updates and erases together.
        std::mt19937_64 rng(20261021);
        std::uint64_t inserted=0,updated=0,erased=0,splits=0;
        for(int n=0;n<400;++n) {
            const auto base=rows[rng()%rows.size()].key;
            std::string key=base+"~n3/"+std::to_string(n);
            require(index.insert(key,1'000+n),"insert accepted");
            reference.emplace(key,1'000+n);
            require(index.find_base(key)==std::optional<std::uint64_t>(1'000+n),"insert read-back");
            ++inserted;
        }
        splits=bm_n1_leaf::reasons().block_splits;
        for(int n=0;n<150;++n) {
            const auto& key=rows[rng()%rows.size()].key;
            require(index.update(key,2'000+n),"update accepted");
            reference[key]=2'000+n;
            require(index.find_base(key)==std::optional<std::uint64_t>(2'000+n),"update read-back");
            ++updated;
        }
        for(int n=0;n<150;++n) {
            const std::string key=rows[rng()%rows.size()].key;
            const bool present=reference.find(key)!=reference.end();
            require(index.erase(key)==present,"erase result");
            if(present)reference.erase(key);
            ++erased;
        }
        require(index.verify()>0,"verify after mutations");
        const auto after_bytes=index.memory_bytes();
        std::size_t checked=0;
        for(const auto& [key,value]:reference)
            if(index.find_base(key)!=std::optional<std::uint64_t>(value))++mismatch;else ++checked;
        const auto final_scan=index.scan("",SIZE_MAX);
        require(final_scan.size()==reference.size(),"final scan size");
        std::size_t i=0;
        for(const auto& [key,value]:reference)
            if(final_scan[i].key!=key||final_scan[i].value!=value){++mismatch;break;}else ++i;

        std::cout<<"{\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"checks\":"<<checks<<",\"mismatches\":"<<mismatch
            <<",\"inserted\":"<<inserted<<",\"updated\":"<<updated<<",\"erased\":"<<erased
            <<",\"block_splits\":"<<splits
            <<",\"bytes_before\":"<<base_bytes<<",\"bytes_after\":"<<after_bytes
            <<",\"bytes_growth_percent\":"<<(base_bytes?100.0*double(after_bytes-base_bytes)/base_bytes:0.0)
            <<",\"final_rows\":"<<reference.size()<<",\"read_back\":"<<checked<<"}\n";
        return mismatch?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Priority 1 gate: the candidate factory through the public contract - corpus correctness,
// owning scan, mutation semantics, and a zero-allocation find loop.
#include "bm_split_adapter.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;

namespace { std::uint64_t g_alloc=0; bool g_count=false; }
void* operator new(std::size_t n) {
    if(g_count)++g_alloc;
    void* p=std::malloc(n?n:1);
    if(!p)throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

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

        auto index=make_bm_split_experiment_index({});
        const auto descriptor=index->descriptor();
        require(descriptor.capabilities.find==sib::Support::native,"find capability");
        require(descriptor.implementation.find("block rebalance")!=std::string::npos,"implementation label");
        index->bulk_load(rows);
        require(index->memory_usage().index_owned_bytes.value_or(0)>0,"memory reported");
        for(const auto& r:rows)require(index->find(r.key)==std::optional<sib::Value>(r.value),"row oracle");
        for(const auto& q:queries) {
            const auto found=index->find(q.key);
            require(found.has_value()==q.expected_present,"query presence");
            if(q.expected_present)require(found&&*found==q.expected_value,"query value");
        }
        std::vector<std::string> probes{"",std::string(1,'\0'),std::string(1,char(255)),
            std::string("a\0b",3),std::string(4096,'z')};
        for(std::size_t i=0;i<rows.size();i+=997)probes.push_back(rows[i].key+"~absent");
        for(const auto& probe:probes) {
            const auto want=reference.find(probe);
            const auto got=index->find(probe);
            require(got.has_value()==(want!=reference.end()),"absent presence");
            if(want!=reference.end())require(got&&*got==want->second,"absent value");
        }
        auto scanned=index->scan("",SIZE_MAX);
        require(scanned.size()==rows.size(),"scan size");
        for(std::size_t i=0;i<scanned.size();++i)
            require(scanned[i].key==rows[i].key&&scanned[i].value==rows[i].value,"scan row");
        index.reset();
        require(scanned.size()==rows.size(),"scan outlives index");

        auto second=make_bm_split_experiment_index({});
        second->bulk_load(rows);
        std::mt19937_64 rng(20261023);
        std::uint64_t ins=0,upd=0,era=0;
        for(int n=0;n<600;++n) {
            std::string key=rows[rng()%rows.size()].key+"~p1/"+std::to_string(n);
            require(second->insert(key,7'000+n)==sib::InsertResult::inserted,"insert");
            require(second->find(key)==std::optional<sib::Value>(7'000+n),"insert read-back");
            ++ins;
        }
        for(int n=0;n<200;++n) {
            const auto& key=rows[rng()%rows.size()].key;
            require(second->update(key,8'000+n)==sib::UpdateResult::updated,"update");
            ++upd;
        }
        for(int n=0;n<200;++n) {
            const std::string key=rows[rng()%rows.size()].key;
            const auto out=second->erase(key);
            require(out==sib::EraseResult::erased||out==sib::EraseResult::not_found,"erase");
            ++era;
        }
        // Zero-allocation find loop through the factory.
        g_count=true;g_alloc=0;
        std::uint64_t sink=0;
        for(const auto& q:queries)sink+=second->find(q.key).value_or(0);
        g_count=false;
        require(g_alloc==0,"find allocations are zero");
        const auto final_scan=second->scan("",SIZE_MAX);
        require(!final_scan.empty(),"final scan non-empty");

        std::cout<<"{\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"checks\":"<<checks<<",\"mismatches\":"<<mismatch
            <<",\"inserted\":"<<ins<<",\"updated\":"<<upd<<",\"erased\":"<<era
            <<",\"find_allocations\":"<<g_alloc<<",\"final_rows\":"<<final_scan.size()
            <<",\"checksum\":"<<sink<<"}\n";
        return mismatch?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

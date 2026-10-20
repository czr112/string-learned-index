// R0 closing gates: differential against the bounded core, zero-allocation find, wrapper cost.
#include "bm_experiment_adapter.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <new>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;
using Clock=std::chrono::steady_clock;

static std::uint64_t g_allocations=0, g_bytes=0;
static bool g_counting=false;
void* operator new(std::size_t n) {
    if(g_counting){++g_allocations;g_bytes+=n;}
    void* p=std::malloc(n?n:1);
    if(!p)throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

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
        if(argc!=4)throw std::invalid_argument("rows.bin queries.bin process_offset");
        const int offset=std::stoi(argv[3]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");

        // Differential: the unified factory must expose exactly the bounded core's structure.
        auto adapter=make_bm_experiment_index({{"lookup","base"}});
        adapter->bulk_load(rows);
        bm_bounded_page::Index core(rows);
        std::uint64_t checks=0,mismatch=0;
        auto require=[&](bool ok,const char* what){++checks;if(!ok){++mismatch;std::cerr<<what<<'\n';}};
        require(adapter->memory_usage().index_owned_bytes.value_or(0)==core.memory_bytes(),"memory bytes");
        require(adapter->descriptor().capabilities.find==sib::Support::native,"capability find");
        for(const auto& row:rows) {
            require(adapter->find(row.key)==core.find_base(row.key),"answer row");
            require(core.find_base(row.key)==std::optional<sib::Value>(row.value),"core row oracle");
        }
        for(const auto& q:queries)require(adapter->find(q.key)==core.find_base(q.key),"answer query");

        // Differential after mutation: insert the same batch through both entry points and check
        // that the answers still agree; the counters belong to the core instance only.
        const auto before=core.maintenance();
        std::uint64_t through_adapter=0;
        std::vector<std::string> inserted;
        for(int i=0;i<50;++i) {
            std::string key=rows[(i*rows.size())/50].key+"~diff/"+std::to_string(i);
            const bool via_adapter=adapter->insert(key,9'000+i)==sib::InsertResult::inserted;
            const bool via_core=core.insert(key,9'000+i);
            require(via_adapter==via_core,"insert agreement");
            if(via_adapter){++through_adapter;inserted.push_back(key);}
        }
        const auto after=core.maintenance();
        require(through_adapter==50,"inserts accepted");
        require(after.rebuilds>before.rebuilds,"core recorded rebuilds");
        for(const auto& key:inserted)require(adapter->find(key)==core.find_base(key),"post-insert answer");

        // Zero allocations during a pure find loop through the factory.
        g_counting=true;g_allocations=0;g_bytes=0;
        std::uint64_t sink=0;
        for(const auto& q:queries)sink+=token(adapter->find(q.key));
        g_counting=false;
        require(g_allocations==0,"find allocations are zero");
        const auto find_allocations=g_allocations;

        // Wrapper cost: factory path versus the bare core, paired.
        std::vector<std::string> keys;keys.reserve(queries.size());
        for(const auto& q:queries)keys.push_back(q.key);
        auto pass=[&](bool via_adapter,const std::vector<std::uint32_t>& order) {
            std::uint64_t acc=0;
            for(const auto i:order)
                acc=acc*131+token(via_adapter?adapter->find(keys[i]):core.find_base(keys[i]));
            return acc;
        };
        const char* names[]={"adapter","adapter_AA","core","core_AA"};
        constexpr int williams[]={0,1,3,2};
        for(std::size_t repeat=0;repeat<6;++repeat) {
            const auto order=permutation(keys.size(),repeat);
            std::uint64_t checksums[4]={};
            for(int position=0;position<4;++position) {
                const auto arm=(williams[position]+int(repeat)+offset)%4;
                const auto start=Clock::now();
                const auto value=pass(arm<2,order);
                const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                checksums[arm]=value;
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position
                    <<",\"arm\":\""<<names[arm]<<"\",\"operations\":"<<keys.size()
                    <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<value<<"}\n";
            }
            for(int arm=1;arm<4;++arm)if(checksums[arm]!=checksums[0])throw std::logic_error("arm checksum differs");
        }
        std::cout<<"{\"phase\":\"gate\",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"checks\":"<<checks<<",\"mismatches\":"<<mismatch
            <<",\"find_allocations\":"<<find_allocations
            <<",\"index_bytes\":"<<core.memory_bytes()
            <<",\"rebuilds_before\":"<<before.rebuilds<<",\"rebuilds_after\":"<<after.rebuilds
            <<",\"root_publications_after\":"<<after.root_publications
            <<",\"answers_checksum\":"<<sink<<"}\n";
        return mismatch?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

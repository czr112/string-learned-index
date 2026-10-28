// Wrapper gate for make_bm_split_experiment_index: answer/structure differential against the
// split core, zero-allocation find through the factory, and a paired adapter-vs-core cost
// measurement with AA arms. Deterministic acceptance plus timing passes; never a benchmark.
#include "bm_split_adapter.hpp"
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
        const int offset=std::stoi(argv[3]);if(offset<0||offset>15)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");

        // Differential: the public factory must expose exactly the split core's structure.
        auto adapter=make_bm_split_experiment_index({});
        adapter->bulk_load(rows);
        bm_split_page::Index core(rows);
        std::uint64_t checks=0,mismatch=0;
        auto require=[&](bool ok,const char* what){++checks;if(!ok){++mismatch;std::cerr<<what<<'\n';}};
        require(adapter->memory_usage().index_owned_bytes.value_or(0)==core.memory_bytes(),"memory bytes");
        require(adapter->descriptor().capabilities.find==sib::Support::native,"capability find");
        require(adapter->scan("",SIZE_MAX).size()==core.size(),"cardinality agreement");
        for(const auto& row:rows) {
            require(adapter->find(row.key)==core.find_base(row.key),"answer row");
            require(core.find_base(row.key)==std::optional<sib::Value>(row.value),"core row oracle");
        }
        for(const auto& q:queries)require(adapter->find(q.key)==core.find_base(q.key),"answer query");
        require(core.verify()>0,"core verify");

        // Mutation agreement through both entry points on identical inputs.
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
        require(after.inplace_inserts+after.rebuilds>before.inplace_inserts+before.rebuilds,
                "core recorded inserts");
        require(adapter->memory_usage().index_owned_bytes.value_or(0)==core.memory_bytes(),"memory bytes after mutation");
        for(const auto& key:inserted)require(adapter->find(key)==core.find_base(key),"post-insert answer");
        for(const auto& q:queries)require(adapter->find(q.key)==core.find_base(q.key),"post-mutation query");

        // Zero allocations during a pure find loop through the factory.
        g_counting=true;g_allocations=0;g_bytes=0;
        std::uint64_t sink=0;
        for(const auto& q:queries)sink+=token(adapter->find(q.key));
        g_counting=false;
        require(g_allocations==0,"find allocations are zero");
        const auto find_allocations=g_allocations;

        // Wrapper cost: factory path versus the bare split core, paired, with AA arms.
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
        // Calibrated passes: each timed arm runs whole-query-set loops until at least
        // 40 ms elapsed (capped), mirroring the query-recheck calibration floor, so the
        // analysis uses ns/operation instead of a single noisy pass.
        constexpr std::uint64_t min_ns=40'000'000, max_loops=200;
        for(std::size_t repeat=0;repeat<8;++repeat) {
            const auto order=permutation(keys.size(),repeat);
            std::uint64_t checksums[4]={};
            for(int position=0;position<4;++position) {
                const auto arm=(williams[position]+int(repeat)+offset)%4;
                auto start=Clock::now();std::uint64_t loops=0,value=0,ns=0;
                do {
                    value=pass(arm<2,order);++loops;
                    ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                }while(ns<min_ns&&loops<max_loops);
                checksums[arm]=value;
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position
                    <<",\"arm\":\""<<names[arm]<<"\",\"operations\":"<<keys.size()
                    <<",\"loops\":"<<loops<<",\"elapsed_ns\":"<<ns
                    <<",\"ns_per_operation\":"<<double(ns)/(double(loops)*double(keys.size()))
                    <<",\"checksum\":"<<value<<"}\n";
            }
            for(int arm=1;arm<4;++arm)if(checksums[arm]!=checksums[0])throw std::logic_error("arm checksum differs");
        }
        std::cout<<"{\"phase\":\"gate\",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"checks\":"<<checks<<",\"mismatches\":"<<mismatch
            <<",\"find_allocations\":"<<find_allocations
            <<",\"index_bytes\":"<<core.memory_bytes()
            <<",\"inplace_inserts_after\":"<<after.inplace_inserts
            <<",\"rebuilds_after\":"<<after.rebuilds
            <<",\"answers_checksum\":"<<sink<<"}\n";
        return mismatch?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

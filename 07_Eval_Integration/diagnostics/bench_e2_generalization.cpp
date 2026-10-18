// E2: no-replacement full-library reads, 100%/0% hit endpoints, first pass after an
// eviction sweep. One cell and one measurement mode per process; modes are never mixed.
#include "collision_refinement_adapter.hpp"
#include "our_static_refined_adapter.h"
#include "../adapters/art/art_adapter.h"
#include "../adapters/hot/hot_adapter.h"
#include "static_input.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;

static volatile std::uint64_t sink=0;
static std::uint64_t token(std::optional<std::uint64_t> r) {
    return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
static std::unique_ptr<sib::Index> prototype(const sib::Parameters& p) {
    if(!p.empty())throw std::invalid_argument("prototype parameters");
    return std::make_unique<refinement_adapter::Adapter<collision_refined::Index,static_physical::Lookup::learned>>();
}
static void number(const std::optional<std::uint64_t>& n) {if(n)std::cout<<*n;else std::cout<<"null";}

// Deterministic permutations: every arm and every process sees the same key order within a
// repeat, so a ratio is a paired comparison and checksums can be compared across arms.
static std::vector<std::uint32_t> permutation(std::size_t n,std::uint64_t repeat) {
    std::vector<std::uint32_t> p(n);
    std::iota(p.begin(),p.end(),std::uint32_t(0));
    std::mt19937_64 rng(UINT64_C(0x9e3779b97f4a7c15)^(repeat*UINT64_C(0x100000001b3))^std::uint64_t(n));
    std::shuffle(p.begin(),p.end(),rng);
    return p;
}

using Clock=std::chrono::steady_clock;
static std::uint64_t pass(const sib::Index& index,const std::vector<uni::Query>& queries,
                          const std::vector<std::uint32_t>& order) {
    std::uint64_t checksum=0;
    for(const auto i:order)checksum=checksum*131+token(index.find(queries[i].key));
    return checksum;
}
static void timed_pass(const sib::Index& index,const std::vector<uni::Query>& queries,
                       const std::vector<std::uint32_t>& order,std::uint64_t& ns,std::uint64_t& checksum) {
    const auto start=Clock::now();
    const auto value=pass(index,queries,order);
    ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
    checksum=value;sink=value;
}
static std::uint64_t now_ns(const Clock::time_point& start) {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
}

int main(int argc,char** argv) {
    try {
        if(argc<5||argc>6)throw std::invalid_argument("rows.bin queries.bin mode process_offset [evict_mb]");
        const std::string mode=argv[3];
        if(mode!="gate"&&mode!="warm"&&mode!="first"&&mode!="evicted")throw std::invalid_argument("unknown mode");
        const int offset=std::stoi(argv[4]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        const std::size_t evict_mb=mode=="evicted"?(argc==6?std::stoul(argv[5]):256):0;

        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty frozen workload");
        // Every E2 cell is exactly one query per row: a hit pass or a miss pass, never a mix.
        if(queries.size()!=rows.size())throw std::invalid_argument("one query per row required");
        const bool any_hit=std::any_of(queries.begin(),queries.end(),[](const auto& q){return q.expected_present;});
        const bool any_miss=std::any_of(queries.begin(),queries.end(),[](const auto& q){return !q.expected_present;});
        if(any_hit&&any_miss)throw std::invalid_argument("mixed hit/miss workload not admitted in E2");

        const sib::Factory factories[]={prototype,make_our_static_refined_index,make_art_index,make_hot_index};
        const char* factory_names[]={"prototype","integrated","ART","HOT"};
        const int object_of[]={0,0,1,1,2,3};
        const char* names[]={"prototype","prototype_AA","integrated","integrated_AA","ART","HOT"};

        auto build_into=[&](std::unique_ptr<sib::Index>& slot,int factory,std::uint64_t& ns) {
            const auto start=Clock::now();
            slot=factories[factory]({});slot->bulk_load(rows);
            ns=now_ns(start);
        };
        // Full-oracle check for one arm; steps >1 sample rows and queries (full verification is
        // gate mode). Sampling exists only to catch gross breakage cheaply.
        auto verify_arm=[&](const sib::Index& index,std::string_view arm_name,std::size_t row_step,std::size_t query_step) {
            const auto d=index.descriptor();const auto& domain=d.capabilities.keys;
            auto admitted=[&](std::string_view key) {
                if(key.size()<domain.min_bytes||(domain.max_bytes&&key.size()>*domain.max_bytes))throw std::logic_error("key length not admitted");
                for(unsigned char c:key)if((!domain.allows_nul&&c==0)||(!domain.allows_high_bit&&c>=128))throw std::logic_error("key byte not admitted");
            };
            std::size_t checked=0;
            for(std::size_t i=0;i<rows.size();i+=row_step) {
                admitted(rows[i].key);
                if(index.find(rows[i].key)!=std::optional<sib::Value>(rows[i].value))throw std::logic_error("row oracle: "+std::string(arm_name));
                ++checked;
            }
            for(std::size_t i=0;i<queries.size();i+=query_step) {
                const auto& q=queries[i];admitted(q.key);const auto found=index.find(q.key);
                if(found.has_value()!=q.expected_present||(found&&*found!=q.expected_value))throw std::logic_error("query oracle: "+std::string(arm_name));
                ++checked;
            }
            return std::pair<std::size_t,sib::Descriptor>(checked,d);
        };
        auto print_gate=[&](const sib::Index& index,std::string_view arm_name,int oi,std::uint64_t build_ns,int build_position) {
            const auto [checked,d]=verify_arm(index,arm_name,1,1);
            const auto m=index.memory_usage();
            std::cout<<"{\"phase\":\"gate\",\"arm\":\""<<arm_name<<"\",\"verified\":"<<checked
                <<",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
                <<",\"implementation_version\":"<<std::quoted(d.implementation_version)
                <<",\"adapter_version\":"<<std::quoted(d.adapter_version)
                <<",\"build_ns\":"<<build_ns<<",\"build_position\":"<<build_position
                <<",\"object_id\":"<<oi<<",\"factory\":"<<std::quoted(factory_names[oi])
                <<",\"hit\":"<<(any_hit?"true":"false")<<",\"index_bytes\":";
            number(m.index_owned_bytes);std::cout<<",\"adapter_bytes\":";number(m.adapter_owned_bytes);
            std::cout<<",\"retained_input_bytes\":";number(m.retained_input_bytes);std::cout<<"}\n";
            std::cout.flush();
        };
        auto light_gate=[&](const sib::Index& index,std::string_view arm_name) {
            verify_arm(index,arm_name,rows.size()/1024+1,1);
        };
        auto sample_gate=[&](const sib::Index& index,std::string_view arm_name) {
            verify_arm(index,arm_name,rows.size()/64+1,queries.size()/64+1);
        };

        constexpr int williams[]={0,1,5,2,4,3};
        const std::size_t rounds=6;

        if(mode=="gate") {
            std::unique_ptr<sib::Index> objects[4];std::uint64_t builds[4]{};int positions[4]{};
            for(int position=0;position<4;++position) {
                const auto a=(position+offset)%4;positions[a]=position;build_into(objects[a],a,builds[a]);
            }
            // Six arms, four objects: the A/A arms share the same instance as their main arm.
            const sib::Index* arms[]={objects[0].get(),objects[0].get(),objects[1].get(),objects[1].get(),objects[2].get(),objects[3].get()};
            for(unsigned a=0;a<6;++a)print_gate(*arms[a],names[a],object_of[a],builds[object_of[a]],positions[object_of[a]]);
            return 0;
        }

        if(mode=="warm") {
            std::unique_ptr<sib::Index> objects[4];std::uint64_t builds[4]{};int positions[4]{};
            for(int position=0;position<4;++position) {
                const auto a=(position+offset)%4;positions[a]=position;build_into(objects[a],a,builds[a]);
            }
            const sib::Index* arms[]={objects[0].get(),objects[0].get(),objects[1].get(),objects[1].get(),objects[2].get(),objects[3].get()};
            for(unsigned a=0;a<6;++a)light_gate(*arms[a],names[a]);
            for(std::size_t repeat=0;repeat<rounds;++repeat) {
                const auto order=permutation(queries.size(),repeat);
                for(int position=0;position<6;++position) {
                    const auto a=(williams[position]+int(repeat)+offset)%6;
                    std::uint64_t warm_ns{},warm_sum{};
                    timed_pass(*arms[a],queries,order,warm_ns,warm_sum);
                    std::uint64_t ns{},checksum{};
                    timed_pass(*arms[a],queries,order,ns,checksum);
                    std::cout<<"{\"phase\":\"pass\",\"mode\":\"warm\",\"repeat\":"<<repeat
                        <<",\"position\":"<<position<<",\"arm\":\""<<names[a]<<"\",\"operations\":"<<queries.size()
                        <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum
                        <<",\"warm_ns\":"<<warm_ns<<",\"warm_checksum\":"<<warm_sum<<"}\n";
                }
            }
            return 0;
        }

        if(mode=="first") {
            // Each slot gets its own freshly built object immediately before its one timed
            // pass, so every arm sees a genuine first traversal of its own structure.
            for(std::size_t round=0;round<rounds;++round) {
                const auto order=permutation(queries.size(),round);
                for(int position=0;position<6;++position) {
                    const auto a=(williams[position]+int(round)+offset)%6;
                    const auto factory=object_of[a];
                    std::unique_ptr<sib::Index> fresh;std::uint64_t build_ns{};
                    build_into(fresh,factory,build_ns);
                    std::uint64_t ns{},checksum{};
                    timed_pass(*fresh,queries,order,ns,checksum);
                    // Verified after the only timed pass: a pre-pass check would warm the object
                    // and destroy the first-traversal semantics this mode exists to measure.
                    sample_gate(*fresh,names[a]);
                    std::cout<<"{\"phase\":\"pass\",\"mode\":\"first\",\"repeat\":"<<round
                        <<",\"position\":"<<position<<",\"arm\":\""<<names[a]<<"\",\"operations\":"<<queries.size()
                        <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum
                        <<",\"build_ns\":"<<build_ns<<"}\n";
                }
            }
            return 0;
        }

        // evicted: one object per implementation, built once; every measured pass is preceded
        // by a write sweep over a 256 MiB buffer. Not a cold cache: only our own footprint is
        // pushed out, and page/TLB/branch state can survive.
        std::unique_ptr<sib::Index> objects[4];std::uint64_t builds[4]{};int positions[4]{};
        for(int position=0;position<4;++position) {
            const auto a=(position+offset)%4;positions[a]=position;build_into(objects[a],a,builds[a]);
        }
        const sib::Index* arms[]={objects[0].get(),objects[0].get(),objects[1].get(),objects[1].get(),objects[2].get(),objects[3].get()};
        for(unsigned a=0;a<6;++a)light_gate(*arms[a],names[a]);
        std::vector<char> eviction(std::size_t(evict_mb)<<20,char(1));
        for(std::size_t round=0;round<rounds;++round) {
            const auto order=permutation(queries.size(),round);
            for(int position=0;position<6;++position) {
                const auto a=(williams[position]+int(round)+offset)%6;
                std::memset(eviction.data(),1,eviction.size());
                sink+=static_cast<unsigned char>(eviction[std::size_t(sink)%eviction.size()]);
                std::uint64_t ns{},checksum{};
                timed_pass(*arms[a],queries,order,ns,checksum);
                std::cout<<"{\"phase\":\"pass\",\"mode\":\"evicted\",\"repeat\":"<<round
                    <<",\"position\":"<<position<<",\"arm\":\""<<names[a]<<"\",\"operations\":"<<queries.size()
                    <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum
                    <<",\"evict_bytes\":"<<eviction.size()<<"}\n";
            }
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

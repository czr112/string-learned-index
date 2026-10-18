#include "collision_refinement_adapter.hpp"
#include "our_static_refined_adapter.h"
#include "../adapters/art/art_adapter.h"
#include "../adapters/hot/hot_adapter.h"
#include "static_input.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

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
static void number(const std::optional<std::uint64_t>& n){if(n)std::cout<<*n;else std::cout<<"null";}
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin queries.bin process_offset");
        const auto offset=std::stoi(argv[3]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||(queries.size()!=10000&&queries.size()!=100000))throw std::invalid_argument("frozen workload required");
        using Clock=std::chrono::steady_clock;
        const sib::Factory factories[]={prototype,make_our_static_refined_index,make_art_index,make_hot_index};
        std::unique_ptr<sib::Index> objects[4];std::uint64_t builds[4]{};int build_positions[4]{};
        for(int position=0;position<4;++position) {
            const auto a=(position+offset)%4;const auto start=Clock::now();
            objects[a]=factories[a]({});objects[a]->bulk_load(rows);
            builds[a]=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();build_positions[a]=position;
        }
        const sib::Index* arms[]={objects[0].get(),objects[0].get(),objects[1].get(),objects[1].get(),objects[2].get(),objects[3].get()};
        const int object_id[]={0,0,1,1,2,3};
        const char* names[]={"prototype","prototype_AA","integrated","integrated_AA","ART","HOT"};
        for(unsigned a=0;a<6;++a) {
            const auto d=arms[a]->descriptor();
            if(d.capabilities.bulk_load==sib::Support::unsupported||d.capabilities.find==sib::Support::unsupported)
                throw std::logic_error("required capability absent");
            const auto admitted=[&](std::string_view key) {
                const auto& domain=d.capabilities.keys;
                if(key.size()<domain.min_bytes||(domain.max_bytes&&key.size()>*domain.max_bytes)||
                   (domain.fixed_bytes&&key.size()!=*domain.fixed_bytes))throw std::logic_error("key length not admitted");
                for(unsigned char c:key)if((!domain.allows_nul&&c==0)||(!domain.allows_high_bit&&c>=128))
                    throw std::logic_error("key byte not admitted");
            };
            for(const auto& r:rows) {
                admitted(r.key);if(r.value>d.capabilities.max_value||arms[a]->find(r.key)!=std::optional<sib::Value>(r.value))
                    throw std::logic_error("full row oracle");
            }
            for(const auto& q:queries) {
                admitted(q.key);const auto found=arms[a]->find(q.key);
                if(found.has_value()!=q.expected_present||(found&&*found!=q.expected_value))throw std::logic_error("query oracle");
            }
            const auto m=arms[a]->memory_usage();const auto oi=object_id[a];
            std::cout<<"{\"phase\":\"gate\",\"arm\":\""<<names[a]<<"\",\"verified\":"<<rows.size()+queries.size()
                <<",\"implementation_version\":"<<std::quoted(d.implementation_version)<<",\"adapter_version\":"<<std::quoted(d.adapter_version)
                <<",\"build_ns\":"<<builds[oi]<<",\"build_position\":"<<build_positions[oi]<<",\"object_id\":"<<oi<<",\"index_bytes\":";
            number(m.index_owned_bytes);std::cout<<",\"adapter_bytes\":";number(m.adapter_owned_bytes);
            std::cout<<",\"retained_input_bytes\":";number(m.retained_input_bytes);std::cout<<"}\n";
        }
        constexpr int order[]={0,1,5,2,4,3};const auto passes=200000/queries.size();
        for(int repeat=0;repeat<6;++repeat)for(int position=0;position<6;++position) {
            const auto a=(order[position]+repeat+offset)%6;
            for(std::size_t p=0;p<passes;++p)for(const auto& q:queries)sink=token(arms[a]->find(q.key));
            std::uint64_t checksum=0;const auto start=Clock::now();
            for(std::size_t p=0;p<passes;++p)for(const auto& q:queries)checksum=checksum*131+token(arms[a]->find(q.key));
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();sink=checksum;
            std::cout<<"{\"phase\":\"paired\",\"repeat\":"<<repeat<<",\"position\":"<<position<<",\"arm\":\""<<names[a]
                <<"\",\"operations\":200000,\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

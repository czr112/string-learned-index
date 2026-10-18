#include "collision_refinement_adapter.hpp"
#include "static_input.hpp"
#include <chrono>
#include <iostream>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;
using static_physical::Lookup;
static volatile std::uint64_t sink=0;
static std::uint64_t token(std::optional<std::uint64_t> r) {
    return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin queries.bin process_offset");
        const int offset=std::stoi(argv[3]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||(queries.size()!=10000&&queries.size()!=100000))throw std::invalid_argument("frozen queue required");
        refinement_adapter::Adapter<physical_locator::Index,Lookup::learned> base;
        refinement_adapter::Adapter<physical_locator::Index,Lookup::wide_directory> wide;
        refinement_adapter::Adapter<physical_locator::Index,Lookup::directory> directory;
        refinement_adapter::Adapter<collision_refined::Index,Lookup::learned> candidate;
        for(sib::Index* p:std::vector<sib::Index*>{&base,&wide,&directory,&candidate})p->bulk_load(rows);
        const sib::Index* arms[]={&base,&base,&wide,&directory,&candidate,&candidate};
        const char* names[]={"B_learned","B_AA","B_wide","B_directory","D1","D1_AA"};
        std::size_t verified=0;
        for(const auto* arm:arms) {
            for(const auto& row:rows) {
                if(arm->find(row.key)!=std::optional<sib::Value>(row.value))throw std::logic_error("full row oracle");++verified;
            }
            for(const auto& q:queries) {
                const auto r=arm->find(q.key);
                if(r.has_value()!=q.expected_present||(r&&*r!=q.expected_value))throw std::logic_error("query oracle");++verified;
            }
        }
        const auto base_bytes=*base.memory_usage().index_owned_bytes,d1_bytes=*candidate.memory_usage().index_owned_bytes;
        std::cout<<"{\"phase\":\"gate\",\"verified_answers\":"<<verified<<",\"base_bytes\":"<<base_bytes<<",\"D1_bytes\":"<<d1_bytes<<"}\n";
        using Clock=std::chrono::steady_clock;constexpr int order[]={0,1,5,2,4,3};
        const auto passes=200000/queries.size();
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

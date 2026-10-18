#include "locator_seam_adapter.hpp"
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
        if(rows.empty()||queries.size()!=100000)throw std::invalid_argument("frozen 100k queue required");
        locator_seam::Adapter<static_physical::Index,Lookup::learned> old_model;
        locator_seam::Adapter<physical_locator::Index,Lookup::learned> new_model;
        locator_seam::Adapter<static_physical::Index,Lookup::directory> old_dir;
        locator_seam::Adapter<physical_locator::Index,Lookup::directory> new_dir;
        for(sib::Index* p:std::vector<sib::Index*>{&old_model,&new_model,&old_dir,&new_dir})p->bulk_load(rows);
        const sib::Index* arms[]={&old_model,&new_model,&new_model,&old_dir,&new_dir,&new_dir};
        const char* names[]={"old_model","new_model","new_model_AA","old_directory","new_directory","new_directory_AA"};
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
        const auto old_bytes=*old_model.memory_usage().index_owned_bytes,new_bytes=*new_model.memory_usage().index_owned_bytes;
        if(new_bytes!=old_bytes+sizeof(std::size_t))throw std::logic_error("unexpected resident difference");
        std::cout<<"{\"phase\":\"gate\",\"verified_answers\":"<<verified<<",\"old_bytes\":"<<old_bytes
            <<",\"new_bytes\":"<<new_bytes<<"}\n";
        using Clock=std::chrono::steady_clock;
        constexpr int order[]={0,1,5,2,4,3};
        for(int repeat=0;repeat<6;++repeat)for(int position=0;position<6;++position) {
            const auto a=(order[position]+repeat+offset)%6;
            for(const auto& q:queries)sink=token(arms[a]->find(q.key));
            std::uint64_t checksum=0;const auto start=Clock::now();
            for(int pass=0;pass<2;++pass)for(const auto& q:queries)checksum=checksum*131+token(arms[a]->find(q.key));
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();sink=checksum;
            std::cout<<"{\"phase\":\"paired\",\"repeat\":"<<repeat<<",\"position\":"<<position<<",\"arm\":\""<<names[a]
                <<"\",\"operations\":200000,\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

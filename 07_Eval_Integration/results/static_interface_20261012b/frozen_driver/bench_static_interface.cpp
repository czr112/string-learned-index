#include "static_physical_blocks.hpp"
#include "../adapters/our_static/our_static_adapter.h"
#include "../adapters/art/art_adapter.h"
#include "../adapters/hot/hot_adapter.h"
#include "unified/runner.h"
#include "static_input.hpp"
#include <chrono>
#include <iostream>
namespace sib=string_index_bench::v1;
namespace unified=string_index_bench::unified;
static volatile std::uint64_t sink=0;
static std::uint64_t token(std::optional<std::uint64_t> r){return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);}
class Prototype final:public sib::Index {
    static_blocks::Index core_;
public:
    explicit Prototype(const std::vector<sib::Row>& rows):core_(rows){}
    sib::Descriptor descriptor()const override{sib::Descriptor d;d.implementation="frozen old point-lookup prototype";return d;}
    void bulk_load(const std::vector<sib::Row>&)override{throw sib::UnsupportedOperation("built prototype view");}
    std::optional<sib::Value> find(std::string_view key)const override{return core_.find_model(key);}
    sib::InsertResult insert(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("prototype");}
    sib::UpdateResult update(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("prototype");}
    sib::EraseResult erase(std::string_view)override{throw sib::UnsupportedOperation("prototype");}
    sib::Value range_sum(std::string_view,std::string_view)const override{throw sib::UnsupportedOperation("prototype");}
    std::vector<sib::Row> scan(std::string_view,std::size_t)const override{throw sib::UnsupportedOperation("prototype");}
    sib::MemoryUsage memory_usage()const override{sib::MemoryUsage m;m.index_owned_bytes=core_.memory().total();return m;}
};
int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::invalid_argument("rows queries offset passes");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=unified::read_rows(rs);const auto queries=unified::read_queries(qs);
        const auto offset=std::stoi(argv[3]),passes=std::stoi(argv[4]);
        if(rows.empty()||queries.empty()||passes<=0||offset<0||offset>4)throw std::invalid_argument("invalid workload");
        using Clock=std::chrono::steady_clock;
        std::uint64_t builds[6]{};
        auto t=Clock::now();Prototype old(rows);builds[0]=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count();
        const auto build=[&](auto factory,const sib::Parameters& p,unsigned arm){
            auto begin=Clock::now();auto index=factory(p);index->bulk_load(rows);
            builds[arm]=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();return index;
        };
        auto learned=build(make_our_static_index,{},1);builds[2]=builds[1];
        auto hybrid=build(make_our_static_index,{{"lookup","wide_directory"}},3);
        auto art=build(make_art_index,{},4),hot=build(make_hot_index,{},5);
        const sib::Index* arms[]={&old,learned.get(),learned.get(),hybrid.get(),art.get(),hot.get()};
        const char* names[]={"prototype","learned","learned_AA","wide_directory","ART","HOT"};
        for(unsigned a=0;a<6;++a) {
            for(const auto& row:rows)if(arms[a]->find(row.key)!=std::optional<sib::Value>(row.value))throw std::logic_error("row oracle mismatch");
            for(const auto& q:queries) {
                auto found=arms[a]->find(q.key);
                if(found.has_value()!=q.expected_present||(found&&*found!=q.expected_value))throw std::logic_error("query oracle mismatch");
            }
            const auto m=arms[a]->memory_usage();
            std::cout<<"{\"phase\":\"gate\",\"arm\":\""<<names[a]<<"\",\"build_ns\":"<<builds[a]<<",\"index_bytes\":";
            if(m.index_owned_bytes)std::cout<<*m.index_owned_bytes;else std::cout<<"null";
            std::cout<<",\"adapter_bytes\":";if(m.adapter_owned_bytes)std::cout<<*m.adapter_owned_bytes;else std::cout<<"null";std::cout<<"}\n";
        }
        constexpr int order[]={0,1,5,2,4,3};
        for(int repeat=0;repeat<6;++repeat)for(int position=0;position<6;++position) {
            const int a=(order[position]+repeat+offset)%6;
            for(const auto& q:queries)sink=token(arms[a]->find(q.key));
            std::uint64_t checksum=0;auto start=Clock::now();
            for(int pass=0;pass<passes;++pass)for(const auto& q:queries)checksum=checksum*131+token(arms[a]->find(q.key));
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();sink=checksum;
            std::cout<<"{\"phase\":\"paired\",\"repeat\":"<<repeat<<",\"position\":"<<position<<",\"arm\":\""<<names[a]
                     <<"\",\"operations\":"<<passes*queries.size()<<",\"elapsed_ns\":"<<elapsed<<",\"checksum\":"<<checksum<<"}\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

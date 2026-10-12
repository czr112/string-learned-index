#include "static_physical_blocks.hpp"
#include "unified/runner.h"
#include "../adapters/our_index/our_index_adapter.h"
#include "../adapters/art/art_adapter.h"
#include "../adapters/hot/hot_adapter.h"
#include "static_input.hpp"
#include <chrono>
#include <iostream>

namespace sib=string_index_bench::v1;
namespace unified=string_index_bench::unified;
std::unique_ptr<sib::Index> make_our_index_routing_index(const sib::Parameters&);
static volatile std::uint64_t sink=0;
static std::uint64_t token(std::optional<std::uint64_t> r) {
    return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
// Identical v1 interface for all timed arms; both packed views share the same owned layout.
template<bool Learned> class PackedView final:public sib::Index {
    std::shared_ptr<const static_blocks::Index> index_;
public:
    explicit PackedView(std::shared_ptr<const static_blocks::Index> index):index_(std::move(index)){}
    sib::Descriptor descriptor()const override {sib::Descriptor d;d.implementation="StaticPhysicalBlocks experimental immutable view";
        d.capabilities.bulk_load=sib::Support::unsupported;d.capabilities.find=sib::Support::native;return d;}
    void bulk_load(const std::vector<sib::Row>&)override{throw sib::UnsupportedOperation("already built immutable view");}
    std::optional<std::uint64_t> find(std::string_view k)const override {
        if constexpr(Learned)return index_->find_model(k);else return index_->find_directory(k);
    }
    sib::InsertResult insert(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("immutable prototype");}
    sib::UpdateResult update(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("immutable prototype");}
    sib::EraseResult erase(std::string_view)override{throw sib::UnsupportedOperation("immutable prototype");}
    std::vector<sib::Row> scan(std::string_view,std::size_t)const override{throw sib::UnsupportedOperation("prototype point lookup only");}
    sib::Value range_sum(std::string_view,std::string_view)const override{throw sib::UnsupportedOperation("prototype point lookup only");}
    sib::MemoryUsage memory_usage()const override{sib::MemoryUsage m;m.index_owned_bytes=index_->memory().total();
        m.adapter_owned_bytes=sizeof(*this);m.retained_input_bytes=0;return m;}
};
static void verify(const sib::Index& index,const std::vector<sib::Row>& rows,const std::vector<unified::Query>& queries) {
    for(const auto& row:rows)if(index.find(row.key)!=std::optional<sib::Value>(row.value))throw std::runtime_error("full row oracle differs");
    for(const auto& q:queries) {
        const auto r=index.find(q.key);
        if(r.has_value()!=q.expected_present||(r&&*r!=q.expected_value))throw std::runtime_error("query oracle differs");
    }
}
int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::invalid_argument("rows.bin queries.bin repeats offset");
        auto row_stream=static_blocks::buffered(argv[1]),query_stream=static_blocks::buffered(argv[2]);
        const auto rows=unified::read_rows(row_stream);const auto queries=unified::read_queries(query_stream);
        const auto repeats=std::stoi(argv[3]),offset=std::stoi(argv[4]);
        if(rows.empty()||queries.empty()||repeats<0||repeats>21||repeats%7)throw std::invalid_argument("empty data or unbalanced repeats");
        using Clock=std::chrono::steady_clock;
        std::uint64_t builds[7]{};
        const auto build=[&](auto factory,unsigned arm) {
            auto t=Clock::now();auto result=factory(sib::Parameters{});result->bulk_load(rows);
            builds[arm]=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count();return result;
        };
        auto release=build(make_our_index_index,0),compact=build(make_our_index_routing_index,1);
        builds[2]=builds[1];auto t=Clock::now();auto packed=std::make_shared<static_blocks::Index>(rows);
        builds[3]=builds[4]=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count();
        PackedView<false> directory(packed);PackedView<true> model(packed);
        auto art=build(make_art_index,5),hot=build(make_hot_index,6);
        const char* names[]={"release","C","C_AA","packed_directory","packed_predictor","ART","HOT"};
        const sib::Index* arms[]={release.get(),compact.get(),compact.get(),&directory,&model,art.get(),hot.get()};
        packed->verify_numeric_certificate();for(const auto& q:queries)packed->check_window(q.key);
        for(unsigned a=0;a<7;++a) {
            verify(*arms[a],rows,queries);const auto m=arms[a]->memory_usage();
            std::cout<<"{\"phase\":\"gate\",\"arm\":\""<<names[a]<<"\",\"rows\":"<<rows.size()
                     <<",\"queries\":"<<queries.size()<<",\"build_ns\":"<<builds[a]<<",\"index_bytes\":";
            if(m.index_owned_bytes)std::cout<<*m.index_owned_bytes;else std::cout<<"null";
            std::cout<<",\"adapter_bytes\":";if(m.adapter_owned_bytes)std::cout<<*m.adapter_owned_bytes;else std::cout<<"null";
            std::cout<<"}\n";
        }
        // Each arm warms on the same full queue; no oracle or trace collection is timed.
        for(auto* arm:arms)for(const auto& q:queries)sink=token(arm->find(q.key));
        for(int rep=0;rep<repeats;++rep)for(int pos=0;pos<7;++pos) {
            const auto a=(rep+pos+offset)%7;std::uint64_t sum=0;auto start=Clock::now();
            for(int pass=0;pass<20;++pass)for(const auto& q:queries)sum=sum*131+token(arms[a]->find(q.key));
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();sink=sum;
            std::cout<<"{\"phase\":\"paired\",\"repeat\":"<<rep<<",\"position\":"<<pos<<",\"arm\":\""<<names[a]
                     <<"\",\"operations\":"<<20*queries.size()<<",\"elapsed_ns\":"<<elapsed<<",\"checksum\":"<<sum<<"}\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

#include "bm_page_index.hpp"
#include "coupled_page_index.hpp"
#include "../adapters/art/art_adapter.h"
#include "static_input.hpp"
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>

namespace sib=string_index_bench::v1;
static volatile std::uint64_t sink=0;
struct Query {std::string key;std::optional<std::uint64_t> expected;};
static std::uint64_t token(std::optional<std::uint64_t> value) {
    return value?*value^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
// One common v1 virtual call per query for all strategies, including ART.
struct View:sib::Index {
    sib::Descriptor descriptor()const override {sib::Descriptor d;d.implementation="diagnostic read view";return d;}
    void bulk_load(const std::vector<sib::Row>&)override{throw sib::UnsupportedOperation("borrowed read view");}
    sib::InsertResult insert(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("read view");}
    sib::UpdateResult update(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("read view");}
    sib::EraseResult erase(std::string_view)override{throw sib::UnsupportedOperation("read view");}
    sib::Value range_sum(std::string_view,std::string_view)const override{throw sib::UnsupportedOperation("read view");}
    std::vector<sib::Row> scan(std::string_view,std::size_t)const override{throw sib::UnsupportedOperation("use owner");}
    sib::MemoryUsage memory_usage()const override{return {};}
};
template<int Mode>struct BMView:View {
    const bm_page::Index& core;explicit BMView(const bm_page::Index& c):core(c){}
    std::optional<sib::Value> find(std::string_view key)const override {
        if constexpr(Mode==0)return core.find_base(key);
        if constexpr(Mode==1)return core.find(key);
        return core.find_exact(key);
    }
};
struct F5View:View {
    const coupled_page::Index& core;explicit F5View(const coupled_page::Index& c):core(c){}
    std::optional<sib::Value> find(std::string_view key)const override{return core.find(key);}
};
__attribute__((noinline)) static std::uint64_t pass(const sib::Index& index,const std::vector<Query>& queries,const std::vector<std::size_t>& order) {
    std::uint64_t checksum=0;for(const auto i:order)checksum=checksum*131+token(index.find(queries[i].key));return checksum;
}
int main(int argc,char** argv) {
    try {
        if(argc!=6)throw std::invalid_argument("rows queries clean|dirty process gate|timing");
        const std::string state=argv[3],mode=argv[5];const auto process=std::stoi(argv[4]);
        if((state!="clean"&&state!="dirty")||(mode!="gate"&&mode!="timing")||process<0||process>=5)throw std::invalid_argument("mode/state/process");
        auto row_input=static_blocks::buffered(argv[1]),query_input=static_blocks::buffered(argv[2]);
        const auto loaded=string_index_bench::unified::read_rows(row_input);
        const auto frozen=string_index_bench::unified::read_queries(query_input);
        std::map<std::string,std::uint64_t,bm_page::ByteLess> reference;std::vector<bm_page::Row> rows;
        for(const auto& r:loaded){reference.emplace(r.key,r.value);rows.push_back({r.key,r.value});}
        if(rows.empty()||rows.size()!=reference.size()||frozen.size()!=rows.size())throw std::logic_error("input shape");
        bm_page::Index bm(rows);coupled_page::Index f5(rows);
        auto art=make_art_index({});art->bulk_load(loaded);
        const auto* root=bm.root_identity_for_testing();const std::size_t writes=state=="dirty"?1024:0;
        std::mt19937_64 writer(0xf2a20261018ULL);
        for(std::size_t i=0;i<writes;++i) {
            const auto& key=loaded[std::size_t(writer()%loaded.size())].key;const auto value=writer();
            if(!bm.update(key,value)||!f5.update(key,value)||art->update(key,value)!=sib::UpdateResult::updated)throw std::logic_error("update result");
            reference[key]=value;
        }
        if(bm.root_identity_for_testing()!=root||bm.maintenance().root_publications!=1||bm.dirty_page_count()!=0)throw std::logic_error("update changed topology");
        BMView<0> base(bm);BMView<1> optimized(bm);BMView<2> directory(bm);F5View self(f5);
        const sib::Index* arms[]={&base,&base,&optimized,&optimized,&directory,&directory,&self,&self,art.get(),art.get()};
        const char* names[]={"BM_base","BM_base_AA","BM","BM_AA","directory","directory_AA","F5","F5_AA","ART","ART_AA"};
        std::size_t checks=bm.verify()+f5.verify();
        for(const auto& r:reference)for(const auto* arm:arms){if(arm->find(r.first)!=r.second)throw std::logic_error("full map mismatch");++checks;}
        auto verify_scan=[&](const auto& found) {
            if(found.size()!=reference.size())throw std::logic_error("scan count");auto it=reference.begin();
            for(const auto& r:found){if(r.key!=it->first||r.value!=it->second)throw std::logic_error("scan value");++it;++checks;}
        };
        verify_scan(bm.scan("",SIZE_MAX));verify_scan(f5.scan("",SIZE_MAX));verify_scan(art->scan("",SIZE_MAX));
        std::vector<Query> queries;queries.reserve(frozen.size());
        bm_leaf::Work raw_work,opt_work;
        for(std::size_t i=0;i<frozen.size();++i) {
            std::string key=frozen[i].key;if(i%10==0){key+="~f2/miss";if(reference.find(key)!=reference.end())throw std::logic_error("miss collision");}
            const auto it=reference.find(key);const auto expected=it==reference.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{it->second};
            for(const auto* arm:arms){if(arm->find(key)!=expected)throw std::logic_error("query answer mismatch");++checks;}
            if(bm.find_base(key,&raw_work)!=expected||bm.find_counted(key,opt_work)!=expected)throw std::logic_error("work answer mismatch");
            bm.check_window(key);queries.push_back({std::move(key),expected});
        }
        if(!opt_work.bm_calls||opt_work.bm_calls+opt_work.rejected!=queries.size()||raw_work.bm_calls!=opt_work.bm_calls)throw std::logic_error("BM usage proof");
        std::cout<<"{\"phase\":\"gate\",\"checks\":"<<checks<<",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"writes\":"<<writes<<",\"pages\":"<<bm.page_count()<<",\"BM_bytes\":"<<bm.memory_bytes()<<",\"F5_bytes\":"<<f5.memory_bytes()
            <<",\"BM_segments\":"<<bm.bm_segment_count()<<",\"BM_calls\":"<<opt_work.bm_calls<<",\"prefix_rejections\":"<<opt_work.rejected
            <<",\"base_blocks\":"<<raw_work.blocks<<",\"optimized_blocks\":"<<opt_work.blocks<<",\"optimized_directory_steps\":"<<opt_work.directory_steps
            <<",\"outside_proposals\":"<<opt_work.bm_outside<<",\"root_publications\":"<<bm.maintenance().root_publications<<"}\n";
        if(mode=="gate")return 0;
        constexpr int williams[]={0,1,9,2,8,3,7,4,6,5};
        std::vector<std::size_t> order(queries.size());std::iota(order.begin(),order.end(),0);
        for(const auto* arm:arms)sink=pass(*arm,queries,order);
        for(int repeat=0;repeat<10;++repeat) {
            std::iota(order.begin(),order.end(),0);std::mt19937_64 random(UINT64_C(0x12345f2)+std::uint64_t(repeat));std::shuffle(order.begin(),order.end(),random);
            std::uint64_t expected=0;for(const auto i:order)expected=expected*131+token(queries[i].expected);
            for(int position=0;position<10;++position) {
                const auto arm=(williams[position]+repeat+process)%10;
                const auto start=std::chrono::steady_clock::now();const auto checksum=pass(*arms[arm],queries,order);
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();sink=checksum;
                if(checksum!=expected)throw std::logic_error("timed checksum mismatch");
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position<<",\"arm\":\""<<names[arm]
                    <<"\",\"operations\":"<<queries.size()<<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

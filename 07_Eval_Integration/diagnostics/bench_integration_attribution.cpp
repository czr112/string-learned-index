// A-stage counterfactual: one shared old/new core, plain/checked wrappers, actual factory.
// All timed calls use v1::Index::find. These views diagnose lookup only, not full adapters.
#include "static_physical_blocks.hpp"
#include "static_physical_index.hpp"
#include "../adapters/our_static/our_static_adapter.h"
#include "static_input.hpp"
#include "unified/runner.h"
#include <chrono>
#include <iostream>
#include <tuple>

namespace sib=string_index_bench::v1;
namespace unified=string_index_bench::unified;
static volatile std::uint64_t sink=0;
static std::uint64_t token(std::optional<std::uint64_t> r) {
    return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}

template<class Core,bool Checked> class View final:public sib::Index {
    std::shared_ptr<const Core> core_;
public:
    explicit View(std::shared_ptr<const Core> core):core_(std::move(core)){}
    sib::Descriptor descriptor()const override {sib::Descriptor d;d.implementation="lookup-only attribution view";return d;}
    std::optional<sib::Value> find(std::string_view k)const override {
        if constexpr(Checked)if(!core_)throw std::logic_error("bulk_load required before data operations");
        return core_->find_model(k);
    }
    void bulk_load(const std::vector<sib::Row>&)override {throw sib::UnsupportedOperation("diagnostic view");}
    sib::InsertResult insert(std::string_view,sib::Value)override {throw sib::UnsupportedOperation("diagnostic view");}
    sib::UpdateResult update(std::string_view,sib::Value)override {throw sib::UnsupportedOperation("diagnostic view");}
    sib::EraseResult erase(std::string_view)override {throw sib::UnsupportedOperation("diagnostic view");}
    sib::Value range_sum(std::string_view,std::string_view)const override {throw sib::UnsupportedOperation("diagnostic view");}
    std::vector<sib::Row> scan(std::string_view,std::size_t)const override {throw sib::UnsupportedOperation("diagnostic view");}
    sib::MemoryUsage memory_usage()const override {sib::MemoryUsage m;m.index_owned_bytes=core_?core_->memory().total():0;return m;}
};

template<class W> static void print_work(const W& w) {
    std::cout<<"{\"queries\":"<<w.queries<<",\"rejected\":"<<w.rejected<<",\"model_steps\":"<<w.model_steps
             <<",\"window_blocks\":"<<w.window_blocks<<",\"blocks\":"<<w.blocks<<",\"exact\":"<<w.exact
             <<",\"wide\":"<<w.wide<<",\"directory_steps\":"<<w.directory_steps<<"}";
}

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin queries.bin process_offset");
        const auto offset=std::stoi(argv[3]);if(offset< -1||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=unified::read_rows(rs);const auto queries=unified::read_queries(qs);
        if(rows.empty()||queries.size()!=100000)throw std::invalid_argument("frozen 100k queries required");
        // Same standalone heap ownership on both axes. Wrapper pairs share actual core addresses.
        auto old=std::shared_ptr<const static_blocks::Index>(new static_blocks::Index(rows));
        auto current=std::shared_ptr<const static_physical::Index>(new static_physical::Index(rows));
        View<static_blocks::Index,false> op(old);View<static_blocks::Index,true> oc(old);
        View<static_physical::Index,false> np(current);View<static_physical::Index,true> nc(current);
        auto actual=make_our_static_index({});actual->bulk_load(rows);
        const sib::Index* arms[]={&op,&oc,&np,&nc,&nc,actual.get()};
        const char* names[]={"old_plain","old_checked","new_plain","new_checked","new_AA","actual"};
        View<static_blocks::Index,true> empty_old({});View<static_physical::Index,true> empty_new({});
        for(const sib::Index* v:std::vector<const sib::Index*>{&empty_old,&empty_new}) {
            bool threw=false;try{v->find("");}catch(const std::logic_error&){threw=true;}
            if(!threw)throw std::logic_error("checked view failed its empty-state contract");
        }
        std::size_t verified=0;
        for(auto* arm:arms) {
            for(const auto& row:rows) {
                if(arm->find(row.key)!=std::optional<sib::Value>(row.value))throw std::logic_error("row oracle");
                ++verified;
            }
            for(const auto& q:queries) {
                const auto found=arm->find(q.key);
                if(found.has_value()!=q.expected_present||(found&&*found!=q.expected_value))throw std::logic_error("query oracle");
                ++verified;
            }
        }
        if(offset== -1) {
            static_blocks::Work ow;static_physical::Work nw;
            std::size_t differences=0,crc_differences=0;
            for(const auto& q:queries) {
                old->check_window(q.key);current->check_window(q.key);
                const auto before_o=std::make_tuple(ow.rejected,ow.model_steps,ow.window_blocks,ow.blocks,ow.exact,ow.wide);
                const auto before_n=std::make_tuple(nw.rejected,nw.model_steps,nw.window_blocks,nw.blocks,nw.exact,nw.wide);
                const auto a=old->find_model(q.key,&ow),b=current->find_model(q.key,&nw);
                if(a!=b)throw std::logic_error("counted lookup answer mismatch");
                const auto delta=[](const auto& before,const auto& w) {
                    return std::make_tuple(w.rejected-std::get<0>(before),w.model_steps-std::get<1>(before),
                        w.window_blocks-std::get<2>(before),w.blocks-std::get<3>(before),
                        w.exact-std::get<4>(before),w.wide-std::get<5>(before));
                };
                differences+=delta(before_o,ow)!=delta(before_n,nw);
                crc_differences+=validation::Fingerprint::of(q.key)!=static_physical::fingerprint(q.key);
            }
            if(crc_differences)throw std::logic_error("CRC utility differs");
            std::cout<<"{\"phase\":\"work\",\"different_queries\":"<<differences<<",\"crc_differences\":"<<crc_differences<<",\"old\":";
            print_work(ow);std::cout<<",\"new\":";print_work(nw);std::cout<<"}\n";
        }
        std::cout<<"{\"phase\":\"gate\",\"verified_answers\":"<<verified<<",\"core_memory_equal\":"
                 <<(old->memory().total()==current->memory().total()?"true":"false")<<"}\n";
        if(offset== -1)return 0;
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

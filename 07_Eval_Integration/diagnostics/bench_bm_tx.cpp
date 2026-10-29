// Independent complete-trace latency / pure throughput / terminal mixed90 channels.
#include "bm_tx_adapter.hpp"
#include "bm_split_adapter.hpp"
#include "bm_experiment_adapter.hpp"
#include "bm_tx_trace.hpp"
#include "our_index_adapter.h"
#include "art_adapter.h"
#include <chrono>
#include <random>
#include <numeric>
#include <array>
#include <iostream>
#include <iomanip>
#include <map>

namespace sib=string_index_bench::v1;
using Clock=std::chrono::steady_clock;
static std::unique_ptr<sib::Index> factory(unsigned arm) {
    switch(arm) {
    case 0:return make_bm_experiment_index({});
    case 1:return make_bm_split_experiment_index({});
    case 2:return make_bm_tx_experiment_index({});
    case 3:return make_our_index_index({});
    case 4:return make_art_index({});
    }
    throw std::logic_error("unknown arm");
}
struct Metrics {double write=0,find=0,scan=0,pure=0;std::uint64_t checksum=0;};
static Metrics measure(unsigned arm,const std::vector<sib::Row>& rows,const std::vector<bm_tx_trace::Operation>& ops) {
    Metrics result;auto index=factory(arm);index->bulk_load(rows);
    std::array<std::uint64_t,3> ns{},count{};
    std::uint64_t sum=0;
    for(const auto& op:ops) {
        const auto start=Clock::now();const auto got=bm_tx_trace::apply(*index,op);
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
        const unsigned bucket=op.kind==1?1:(op.kind==5?2:0);
        ns[bucket]+=elapsed;++count[bucket];sum=sum*131+got;
    }
    result.write=count[0]?double(ns[0])/count[0]:0;
    result.find=count[1]?double(ns[1])/count[1]:0;
    result.scan=count[2]?double(ns[2])/count[2]:0;
    // Drop the first index before building the independent timer-free replay.
    index.reset();index=factory(arm);index->bulk_load(rows);
    std::uint64_t pure_sum=0;
    const auto start=Clock::now();
    for(const auto& op:ops)pure_sum=pure_sum*131+bm_tx_trace::apply(*index,op);
    result.pure=double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
    if(sum!=pure_sum)throw std::logic_error("latency/pure replay checksum mismatch");
    result.checksum=sum;return result;
}
struct Query {std::string key;std::uint64_t expected;};
static std::vector<Query> queries(const std::vector<sib::Row>& rows,const std::vector<bm_tx_trace::Operation>& ops) {
    std::map<std::string,std::uint64_t,bm_tx_page::ByteLess> oracle;
    for(const auto& r:rows)oracle.emplace(r.key,r.value);
    for(const auto& op:ops) {
        if(op.kind==2)oracle.emplace(op.key,op.value);
        if(op.kind==3){auto it=oracle.find(op.key);if(it!=oracle.end())it->second=op.value;}
        if(op.kind==4)oracle.erase(op.key);
    }
    std::vector<Query> live;for(const auto& [key,value]:oracle)live.push_back({key,value});
    std::mt19937_64 rng(20261030);std::shuffle(live.begin(),live.end(),rng);
    if(live.size()<1800)throw std::logic_error("insufficient terminal keys");
    live.resize(1800);
    for(unsigned i=0;i<200;++i) {
        auto key="~tx/absent/"+std::to_string(i);while(oracle.count(key))key+='~';
        live.push_back({std::move(key),UINT64_MAX});
    }
    std::shuffle(live.begin(),live.end(),rng);return live;
}
static std::pair<double,std::uint64_t> query_time(const sib::Index& index,const std::vector<Query>& qs,std::size_t loops) {
    std::uint64_t sum=0;
    const auto start=Clock::now();
    for(std::size_t n=0;n<loops;++n)for(const auto& q:qs)sum=sum*131+index.find(q.key).value_or(UINT64_MAX);
    const double ns=double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
    return {ns/(loops*qs.size()),sum};
}
int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::invalid_argument("rows ops process mixed|point");
        auto rows=bm_tx_trace::rows(argv[1]);auto ops=bm_tx_trace::operations(argv[2]);
        const unsigned process=std::stoul(argv[3]);const bool point=std::string(argv[4])=="point";
        if(point)ops.erase(std::remove_if(ops.begin(),ops.end(),[](const auto& op){return op.kind==5;}),ops.end());
        const auto qs=queries(rows,ops);const unsigned arms=point?5:4;
        const char* names[]={"bm0","bm1","tx","ctx16","art"};
        std::cout<<std::setprecision(17);
        for(unsigned arm=0;arm<arms;++arm)(void)measure(arm,rows,ops);
        std::uint64_t expected=0;bool have_expected=false;
        for(unsigned repeat=0;repeat<8;++repeat)for(unsigned position=0;position<arms;++position) {
            const unsigned offset=(position+process+repeat)%arms;
            const unsigned arm=(process/arms+repeat)%2?arms-1-offset:offset;
            auto a=measure(arm,rows,ops),aa=measure(arm,rows,ops);
            if(!have_expected){expected=a.checksum;have_expected=true;}
            if(a.checksum!=aa.checksum||a.checksum!=expected)throw std::logic_error("cross-arm checksum mismatch");
            std::cout<<"{\"channel\":\"mixed\",\"process\":"<<process<<",\"repeat\":"<<repeat
                     <<",\"position\":"<<position<<",\"arm\":\""<<names[arm]<<"\",\"write\":"<<a.write
                     <<",\"write_aa\":"<<aa.write<<",\"find\":"<<a.find<<",\"find_aa\":"<<aa.find
                     <<",\"scan\":"<<a.scan<<",\"scan_aa\":"<<aa.scan<<",\"pure\":"<<a.pure
                     <<",\"pure_aa\":"<<aa.pure<<",\"checksum\":"<<expected<<"}\n";
        }
        // Fixed same-live-set mixed90, distinct from the historical hot/cold query suite.
        for(unsigned position=0;position<arms;++position) {
            const unsigned arm=(position+process)%arms;auto index=factory(arm);index->bulk_load(rows);
            for(const auto& op:ops)(void)bm_tx_trace::apply(*index,op);
            for(const auto& q:qs)if(index->find(q.key).value_or(UINT64_MAX)!=q.expected)throw std::logic_error("terminal query oracle");
            std::size_t loops=1;
            while(query_time(*index,qs,loops).first*loops*qs.size()<40000000) {
                if(loops>=4096)throw std::logic_error("query calibration budget");loops*=2;
            }
            auto a=query_time(*index,qs,loops),aa=query_time(*index,qs,loops);
            if(a.second!=aa.second)throw std::logic_error("terminal query A/A checksum");
            std::cout<<"{\"channel\":\"terminal\",\"process\":"<<process<<",\"arm\":\""<<names[arm]
                     <<"\",\"find\":"<<a.first<<",\"find_aa\":"<<aa.first<<",\"lookups\":"<<loops*qs.size()<<"}\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

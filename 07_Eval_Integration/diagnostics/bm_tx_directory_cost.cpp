// Counter and elapsed-component diagnosis; both Index algorithms are frozen copies.
#include "bm_directory_metrics.hpp"
#include "bm_tx_index.hpp"
#include BM_DIRECTORY_INDEX_HEADER
#include "bm_tx_trace.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <type_traits>

namespace sib=string_index_bench::v1;
namespace mem=bm_followup_memory;
namespace metric=bm_directory_metrics;
using Clock=std::chrono::steady_clock;
static void need(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
static_assert(sizeof(bm_tx_page::Index)==sizeof(bm_dir_tx::Index),"Index geometry");
static_assert(sizeof(bm_n1_leaf::Page)==sizeof(bm_dir_leaf::Page),"Page geometry");

struct Expected {
    std::optional<sib::Value> found;
    bool changed=false;
    std::vector<sib::Row> scanned;
};
struct Input {
    std::vector<sib::Row> rows,terminal;
    std::vector<bm_tx_trace::Operation> operations;
    std::vector<Expected> expected;
    Input(char** argv):rows(bm_tx_trace::rows(argv[2])),terminal(bm_tx_trace::rows(argv[4])),
                       operations(bm_tx_trace::operations(argv[3])) {
        std::map<std::string,sib::Value,bm_tx_page::ByteLess> oracle;
        for(const auto& row:rows)need(oracle.emplace(row.key,row.value).second,"initial uniqueness");
        expected.reserve(operations.size());
        for(const auto& op:operations) {
            Expected result;auto found=oracle.find(op.key);
            switch(op.kind) {
            case 1:if(found!=oracle.end())result.found=found->second;break;
            case 2:result.changed=oracle.emplace(op.key,op.value).second;break;
            case 3:if(found!=oracle.end()){found->second=op.value;result.changed=true;}break;
            case 4:if(found!=oracle.end()){oracle.erase(found);result.changed=true;}break;
            case 5:
                for(auto it=oracle.lower_bound(op.key);it!=oracle.end()&&result.scanned.size()<op.limit;++it)
                    result.scanned.push_back({it->first,it->second});
                break;
            }
            expected.push_back(std::move(result));
        }
        need(oracle.size()==terminal.size(),"oracle terminal size");
        auto it=oracle.begin();
        for(const auto& row:terminal){need(it->first==row.key&&it->second==row.value,"oracle terminal map");++it;}
    }
};
struct Pass {
    std::uint64_t write_ns=0,insert_ns=0,update_ns=0,erase_ns=0;
    std::uint64_t inserts=0,updates=0,erases=0,checksum=0,block_splits=0;
    std::size_t terminal_bytes=0,maintenance_peak=0,pages=0;
    metric::Counters directory;
};
template<class Index> static Pass replay(const Input& input) {
    constexpr bool diagnostic=std::is_same<Index,bm_dir_tx::Index>::value;
    Pass result;metric::counters={};mem::begin();auto index=std::make_unique<Index>(input.rows);
    mem::peak=mem::live;
    const auto initial_splits=diagnostic?bm_dir_leaf::reasons().block_splits:bm_n1_leaf::reasons().block_splits;
    for(std::size_t i=0;i<input.operations.size();++i) {
        const auto& op=input.operations[i];const auto& expected=input.expected[i];std::uint64_t token=0;
        if(op.kind==1) {
            const auto value=index->find_base(op.key);need(value==expected.found,"replay find");
            token=value?(*value^UINT64_C(0x9e3779b97f4a7c15)):UINT64_C(0x517cc1b727220a95);
        }else if(op.kind==5) {
            const auto rows=index->scan(op.key,op.limit);need(rows.size()==expected.scanned.size(),"replay scan count");
            for(std::size_t j=0;j<rows.size();++j)
                need(rows[j].key==expected.scanned[j].key&&rows[j].value==expected.scanned[j].value,"replay scan rows");
            token=rows.size();
        }else {
            bool changed=false;const auto start=Clock::now();
            if(op.kind==2)changed=index->insert(op.key,op.value);
            else if(op.kind==3)changed=index->update(op.key,op.value);
            else changed=index->erase(op.key);
            const auto elapsed=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
            result.write_ns+=elapsed;
            if(op.kind==2){result.insert_ns+=elapsed;++result.inserts;}
            else if(op.kind==3){result.update_ns+=elapsed;++result.updates;}
            else{result.erase_ns+=elapsed;++result.erases;}
            need(changed==expected.changed,"replay write result");token=op.kind*2+changed;
        }
        result.checksum=result.checksum*131+token;
    }
    result.directory=metric::counters;result.terminal_bytes=mem::live;result.maintenance_peak=mem::peak;
    result.pages=index->page_count();
    result.block_splits=(diagnostic?bm_dir_leaf::reasons().block_splits:bm_n1_leaf::reasons().block_splits)-initial_splits;
    if constexpr(diagnostic) {
        need(result.directory.events==result.block_splits,"timer coverage");
        need(result.directory.allocations==result.directory.events*2,"directory allocation count");
        need(result.directory.object_bytes==result.directory.allocated_bytes,"exact directory request bytes");
    }
    index->verify();
    {
        const auto rows=index->scan("",SIZE_MAX);need(rows.size()==input.terminal.size(),"terminal size");
        for(std::size_t i=0;i<rows.size();++i)
            need(rows[i].key==input.terminal[i].key&&rows[i].value==input.terminal[i].value,"terminal content");
    }
    index.reset();need(mem::live==0,"destruction bytes");mem::tracking=false;return result;
}
static Pass combined(const Input& input,bool instrumented,unsigned replays) {
    Pass result;
    for(unsigned i=0;i<replays;++i) {
        const auto one=instrumented?replay<bm_dir_tx::Index>(input):replay<bm_tx_page::Index>(input);
        if(i)need(one.checksum==result.checksum&&one.terminal_bytes==result.terminal_bytes&&one.pages==result.pages,"repeat identity");
        result.checksum=one.checksum;result.terminal_bytes=one.terminal_bytes;result.pages=one.pages;
        result.maintenance_peak=std::max(result.maintenance_peak,one.maintenance_peak);
        result.write_ns+=one.write_ns;result.insert_ns+=one.insert_ns;result.update_ns+=one.update_ns;result.erase_ns+=one.erase_ns;
        result.inserts+=one.inserts;result.updates+=one.updates;result.erases+=one.erases;result.block_splits+=one.block_splits;
        result.directory.events+=one.directory.events;result.directory.block_objects+=one.directory.block_objects;
        result.directory.fence_objects+=one.directory.fence_objects;result.directory.object_bytes+=one.directory.object_bytes;
        result.directory.allocations+=one.directory.allocations;result.directory.allocated_bytes+=one.directory.allocated_bytes;
        result.directory.wall_ns+=one.directory.wall_ns;
    }
    return result;
}
static void emit(const char* arm,const Pass& r,int process,int round,int position,unsigned replays) {
    const auto& d=r.directory;
    std::cout<<"{\"arm\":\""<<arm<<"\",\"process\":"<<process<<",\"round\":"<<round
             <<",\"position\":"<<position<<",\"replays\":"<<replays<<",\"write_ns\":"<<r.write_ns
             <<",\"insert_ns\":"<<r.insert_ns<<",\"update_ns\":"<<r.update_ns<<",\"erase_ns\":"<<r.erase_ns
             <<",\"inserts\":"<<r.inserts<<",\"updates\":"<<r.updates<<",\"erases\":"<<r.erases
             <<",\"checksum\":"<<r.checksum<<",\"block_splits\":"<<r.block_splits
             <<",\"terminal_bytes\":"<<r.terminal_bytes<<",\"maintenance_peak\":"<<r.maintenance_peak
             <<",\"pages\":"<<r.pages<<",\"directory_events\":"<<d.events
             <<",\"block_objects\":"<<d.block_objects<<",\"fence_objects\":"<<d.fence_objects
             <<",\"object_bytes\":"<<d.object_bytes<<",\"directory_allocations\":"<<d.allocations
             <<",\"directory_requested_bytes\":"<<d.allocated_bytes<<",\"directory_ns\":"<<d.wall_ns<<"}\n";
}
int main(int argc,char** argv) {
    try {
        need(argc==6,"check|bench rows ops final process");Input input(argv);const int process=std::stoi(argv[5]);
        need(process>=0&&process<4,"registered process range");const std::string mode=argv[1];
        if(mode=="check") {
            const auto bare=combined(input,false,1),profile=combined(input,true,1);
            need(bare.checksum==profile.checksum&&bare.block_splits==profile.block_splits&&bare.terminal_bytes==profile.terminal_bytes,"instrumentation equivalence");
            emit("original",bare,process,0,0,1);emit("instrumented",profile,process,0,1,1);return 0;
        }
        need(mode=="bench","unknown mode");combined(input,false,1);combined(input,true,1);
        const char* arms[]={"original","instrumented","original_AA","instrumented_AA"};
        constexpr int order[4][4]={{0,1,3,2},{1,2,0,3},{2,3,1,0},{3,0,2,1}};
        for(int round=0;round<2;++round)for(int position=0;position<4;++position) {
            const int arm=order[(process+round)%4][position];
            emit(arms[arm],combined(input,arm%2,8),process,round,position,8);
        }
    }catch(const std::exception& error){mem::disarm();std::cerr<<error.what()<<'\n';return 1;}
}

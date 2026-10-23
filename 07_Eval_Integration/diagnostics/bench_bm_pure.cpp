// Stage 4/6: pure timing (no oracle in the timed loop) with A/A control, via the formal
// factories (baseline experiment entry, block-rebalance experiment entry, ART, Context16).
#include "bm_experiment_adapter.hpp"
#include "bm_split_adapter.hpp"
#include "art_adapter.h"
#include "our_index_adapter.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
using Clock=std::chrono::steady_clock;
namespace {
struct Operation {std::uint8_t kind=0;std::uint64_t value=0,limit=0;std::string key;};

std::vector<std::pair<std::string,std::uint64_t>> read_rows(const char* path) {
    std::ifstream in(path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<std::pair<std::string,std::uint64_t>> rows;rows.reserve(count);
    for(std::uint64_t i=0;i<count;++i) {
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        std::string key=bytes.substr(pos,n);pos+=n;
        std::uint64_t value=0;std::memcpy(&value,bytes.data()+pos,8);pos+=8;
        rows.emplace_back(std::move(key),value);
    }
    return rows;
}
std::vector<Operation> read_operations(const char* path) {
    std::ifstream in(path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBOPS1\0",8)!=0)throw std::runtime_error("bad ops");
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<Operation> operations;operations.reserve(count);
    for(std::uint64_t i=0;i<count;++i) {
        Operation op;op.kind=std::uint8_t(bytes[pos]);++pos;
        std::memcpy(&op.value,bytes.data()+pos,8);pos+=8;
        std::memcpy(&op.limit,bytes.data()+pos,8);pos+=8;
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        op.key=bytes.substr(pos,n);pos+=n;operations.push_back(std::move(op));
    }
    return operations;
}
std::unique_ptr<sib::Index> make_arm(const std::string& arm) {
    if(arm=="bm")return make_bm_experiment_index({{"lookup","base"}});
    if(arm=="bm_split")return make_bm_split_experiment_index({});
    if(arm=="art")return make_art_index({});
    if(arm=="ctx16")return make_our_index_index({});
    throw std::invalid_argument("unknown arm");
}
struct Result {std::uint64_t write_ns=0,find_ns=0,scan_ns=0,seg_ns=0,writes=0,finds=0,scans=0,checksum=0;};
Result run_once(const std::string& arm,const std::vector<sib::Row>& rows,const std::vector<Operation>& ops) {
    auto index=make_arm(arm);
    index->bulk_load(rows);
    Result r;
    std::uint64_t acc=0;
    const auto seg_start=Clock::now();
    for(const auto& op:ops) {
        if(op.kind==1) {
            const auto t=Clock::now();
            const auto got=index->find(op.key);
            const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());
            r.find_ns+=ns;++r.finds;
            acc=acc*131+(got?*got:0xffffffffffffffffULL);
        } else if(op.kind==2) {
            const auto t=Clock::now();
            const auto out=index->insert(op.key,op.value);
            r.write_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());
            ++r.writes;acc=acc*131+(out==sib::InsertResult::inserted?1:2);
        } else if(op.kind==3) {
            const auto t=Clock::now();
            const auto out=index->update(op.key,op.value);
            r.write_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());
            ++r.writes;acc=acc*131+(out==sib::UpdateResult::updated?3:4);
        } else if(op.kind==4) {
            const auto t=Clock::now();
            const auto out=index->erase(op.key);
            r.write_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());
            ++r.writes;acc=acc*131+(out==sib::EraseResult::erased?5:6);
        } else {
            const auto t=Clock::now();
            const auto rows_out=index->scan(op.key,op.limit);
            r.scan_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());
            ++r.scans;acc=acc*131+rows_out.size();
        }
    }
    r.seg_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now()-seg_start).count());
    r.checksum=acc;
    return r;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin operations.bin process");
        const int process=std::stoi(argv[3]);
        const auto initial=read_rows(argv[1]);
        const auto operations=read_operations(argv[2]);
        std::vector<sib::Row> rows;rows.reserve(initial.size());
        for(const auto& [k,v]:initial)rows.push_back({std::string(k),v});
        const char* arms[]={"bm","bm_split","art","ctx16"};
        // 8 blocks, full balanced cycle: each arm appears in every position twice.
        constexpr int order8[8][4]={{0,1,2,3},{1,2,3,0},{2,3,0,1},{3,0,1,2},
                                    {3,2,1,0},{2,1,0,3},{1,0,3,2},{0,3,2,1}};
        if(process<0||process>7)throw std::invalid_argument("process must be 0..7");
        for(int position=0;position<4;++position) {
            const auto arm=order8[process][position];
            (void)run_once(arms[arm],rows,operations);   // warm-up, not measured
            const auto first=run_once(arms[arm],rows,operations);
            const auto second=run_once(arms[arm],rows,operations);   // A/A control
            std::cout<<"{\"process\":"<<process<<",\"arm\":\""<<arms[arm]<<"\",\"position\":"<<position
                <<",\"write_us\":"<<(first.writes?double(first.write_ns)/first.writes/1000:0.0)
                <<",\"write_us_aa\":"<<(second.writes?double(second.write_ns)/second.writes/1000:0.0)
                <<",\"find_us\":"<<(first.finds?double(first.find_ns)/first.finds/1000:0.0)
                <<",\"find_us_aa\":"<<(second.finds?double(second.find_ns)/second.finds/1000:0.0)
                <<",\"scan_us\":"<<(first.scans?double(first.scan_ns)/first.scans/1000:0.0)
                <<",\"seg_us\":"<<double(first.seg_ns)/1000.0<<",\"seg_us_aa\":"<<double(second.seg_ns)/1000.0<<",\"ops\":"<<operations.size()
                <<",\"writes\":"<<first.writes<<",\"finds\":"<<first.finds<<",\"scans\":"<<first.scans
                <<",\"checksum\":"<<first.checksum<<",\"checksum_aa\":"<<second.checksum<<"}\n";
            if(first.checksum!=second.checksum)throw std::logic_error("A/A checksum differs");
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

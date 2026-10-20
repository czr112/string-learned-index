// R1: four fair growth cells, three arms (BM unified factory, ART, Context16) on one frozen
// initial map and operation stream. Semantics first, then read/write cost per arm.
#include "bm_experiment_adapter.hpp"
#include "art_adapter.h"
#include "our_index_adapter.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
using Clock=std::chrono::steady_clock;
namespace {
struct Operation {std::uint8_t kind=0;std::uint64_t value=0,limit=0;std::string key;};

std::vector<std::pair<std::string,std::uint64_t>> read_rows(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("cannot open rows");
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows magic");
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
    if(!in)throw std::runtime_error("cannot open operations");
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBOPS1\0",8)!=0)throw std::runtime_error("bad operations magic");
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
    if(arm=="art")return make_art_index({});
    if(arm=="ctx16")return make_our_index_index({});
    throw std::invalid_argument("arm must be bm, art or ctx16");
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin operations.bin arm");
        const std::string arm=argv[3];
        const auto initial=read_rows(argv[1]);
        const auto operations=read_operations(argv[2]);
        std::vector<sib::Row> rows;rows.reserve(initial.size());
        std::map<std::string,std::uint64_t> oracle;
        for(const auto& [key,value]:initial) {
            rows.push_back({std::string(key),value});
            oracle.emplace(key,value);
        }
        auto index=make_arm(arm);
        const auto build_start=Clock::now();
        index->bulk_load(rows);
        const auto build_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now()-build_start).count());
        // Correctness of the loaded map before any timing.
        std::uint64_t mismatches=0;
        for(const auto& [key,value]:initial)
            if(index->find(key)!=std::optional<sib::Value>(value))++mismatches;

        std::uint64_t find_ops=0,insert_ops=0,find_hits=0,insert_new=0,insert_dup=0;
        std::uint64_t find_ns=0,write_ns=0;
        for(const auto& op:operations) {
            const auto start=Clock::now();
            if(op.kind==1) {
                const auto want=oracle.find(op.key);
                const auto got=index->find(op.key);
                const bool same=want==oracle.end()?(!got.has_value()):(got&&*got==want->second);
                if(!same){++mismatches;}
                if(want!=oracle.end())++find_hits;
                ++find_ops;
                find_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
            } else if(op.kind==2) {
                const bool present=oracle.find(op.key)!=oracle.end();
                const auto result=index->insert(op.key,op.value);
                const bool inserted=result==sib::InsertResult::inserted;
                if(inserted==present)++mismatches;
                if(inserted){oracle.emplace(op.key,op.value);++insert_new;}else ++insert_dup;
                ++insert_ops;
                write_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
            } else {
                throw std::invalid_argument("growth cells carry find and insert only");
            }
        }
        // Final map: every key readable and the ordered scan agrees.
        std::size_t checked=0;
        for(const auto& [key,value]:oracle)
            if(index->find(key)!=std::optional<sib::Value>(value))++mismatches;else ++checked;
        const auto scan=index->scan("",SIZE_MAX);
        if(scan.size()!=oracle.size())++mismatches;
        else {
            std::size_t i=0;
            for(const auto& [key,value]:oracle)
                if(scan[i].key!=key||scan[i].value!=value){++mismatches;break;}else ++i;
        }
        std::cout<<"{\"arm\":\""<<arm<<"\",\"initial_rows\":"<<rows.size()
            <<",\"operations\":"<<operations.size()<<",\"find_ops\":"<<find_ops<<",\"insert_ops\":"<<insert_ops
            <<",\"find_hits\":"<<find_hits<<",\"insert_new\":"<<insert_new<<",\"insert_dup\":"<<insert_dup
            <<",\"hit_rate\":"<<(find_ops?double(find_hits)/find_ops:0.0)
            <<",\"net_growth\":"<<std::uint64_t(oracle.size())-rows.size()
            <<",\"scan_rows\":"<<scan.size()<<",\"final_read_checked\":"<<checked
            <<",\"mismatches\":"<<mismatches<<",\"build_ns\":"<<build_ns
            <<",\"find_us\":"<<(find_ops?double(find_ns)/find_ops/1000:0.0)
            <<",\"write_us\":"<<(insert_ops?double(write_ns)/insert_ops/1000:0.0)
            <<",\"index_bytes\":"<<index->memory_usage().index_owned_bytes.value_or(0)<<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

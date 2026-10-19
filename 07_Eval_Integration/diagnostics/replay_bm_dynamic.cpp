// Stage-5 entry: replay a frozen SIBOPS1 trace against the BM page and a std::map oracle.
// Correctness is per operation and at the end (full scan and write-read-back); timing is split
// into read and structural-write totals and never drives a decision in this round.
#include "bm_bounded_index.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
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
    if(pos!=bytes.size())throw std::runtime_error("rows length mismatch");
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
        Operation op;
        op.kind=std::uint8_t(bytes[pos]);++pos;
        std::memcpy(&op.value,bytes.data()+pos,8);pos+=8;
        std::memcpy(&op.limit,bytes.data()+pos,8);pos+=8;
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        op.key=bytes.substr(pos,n);pos+=n;
        operations.push_back(std::move(op));
    }
    if(pos!=bytes.size())throw std::runtime_error("operations length mismatch");
    return operations;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("initial_rows.bin operations.bin");
        const auto rows=read_rows(argv[1]);
        const auto operations=read_operations(argv[2]);
        std::vector<bm_page::Row> physical;physical.reserve(rows.size());
        std::map<std::string,std::uint64_t> oracle;
        for(const auto& [key,value]:rows){physical.push_back({key,value});oracle.emplace(key,value);}
        const auto build_start=Clock::now();
        bm_bounded_page::Index index(physical);
        const auto build_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now()-build_start).count());
        if(index.size()!=oracle.size())throw std::logic_error("initial size mismatch");
        // Full read of every initial key before any mutation: the build must already be correct.
        for(const auto& [key,value]:rows)
            if(index.find_base(key)!=std::optional<std::uint64_t>(value))throw std::logic_error("initial find mismatch");

        std::uint64_t mismatches=0,find_ops=0,insert_ops=0,update_ops=0,erase_ops=0,scan_ops=0;
        std::uint64_t find_ns=0,write_ns=0,find_hits=0,insert_new=0,insert_dup=0;
        for(const auto& op:operations) {
            const auto start=Clock::now();
            switch(op.kind) {
                case 1: {
                    const auto want=oracle.find(op.key);
                    const auto got=index.find_base(op.key);
                    const bool same=want==oracle.end()?(!got.has_value()):(got&&*got==want->second);
                    if(!same)++mismatches;
                    if(want!=oracle.end())++find_hits;
                    ++find_ops;
                    break;
                }
                case 2: {
                    const bool present=oracle.find(op.key)!=oracle.end();
                    const bool inserted=index.insert(op.key,op.value);
                    if(inserted==present)++mismatches;
                    if(inserted){oracle.emplace(op.key,op.value);++insert_new;}else ++insert_dup;
                    ++insert_ops;
                    break;
                }
                case 3: {
                    const bool present=oracle.find(op.key)!=oracle.end();
                    const bool updated=index.update(op.key,op.value);
                    if(updated!=present)++mismatches;
                    if(present)oracle[op.key]=op.value;
                    ++update_ops;
                    break;
                }
                case 4: {
                    const bool present=oracle.find(op.key)!=oracle.end();
                    const bool erased=index.erase(op.key);
                    if(erased!=present)++mismatches;
                    if(present)oracle.erase(op.key);
                    ++erase_ops;
                    break;
                }
                default: {
                    const auto got=index.scan(op.key,op.limit);
                    auto it=oracle.lower_bound(op.key);
                    std::size_t n=0;
                    for(;it!=oracle.end()&&n<op.limit;++it,++n)
                        if(n>=got.size()||got[n].key!=it->first||got[n].value!=it->second){++mismatches;break;}
                    if(got.size()!=n)++mismatches;
                    ++scan_ops;
                    break;
                }
            }
            const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
            if(op.kind==1)find_ns+=ns;else write_ns+=ns;
            if(index.size()!=oracle.size())++mismatches;
        }
        // Final map: every key readable, every absent key absent, and the full ordered scan matches.
        std::size_t checked=0;
        for(const auto& [key,value]:oracle)
            if(index.find_base(key)!=std::optional<std::uint64_t>(value)){++mismatches;}
            else ++checked;
        const auto scan=index.scan("",SIZE_MAX);
        if(scan.size()!=oracle.size())++mismatches;
        else {
            std::size_t i=0;
            for(const auto& [key,value]:oracle)
                if(scan[i].key!=key||scan[i].value!=value){++mismatches;break;}else ++i;
        }
        const auto m=index.maintenance();
        std::cout<<"{\"initial_rows\":"<<rows.size()<<",\"operations\":"<<operations.size()
            <<",\"find_ops\":"<<find_ops<<",\"insert_ops\":"<<insert_ops<<",\"update_ops\":"<<update_ops
            <<",\"erase_ops\":"<<erase_ops<<",\"scan_ops\":"<<scan_ops
            <<",\"find_hits\":"<<find_hits<<",\"insert_new\":"<<insert_new<<",\"insert_dup\":"<<insert_dup
            <<",\"final_size\":"<<oracle.size()<<",\"mismatches\":"<<mismatches
            <<",\"final_read_checked\":"<<checked<<",\"final_scan_rows\":"<<scan.size()
            <<",\"build_ns\":"<<build_ns<<",\"find_ns\":"<<find_ns<<",\"write_ns\":"<<write_ns
            <<",\"pages\":"<<index.page_count()<<",\"root_publications\":"<<m.root_publications
            <<",\"rebuilds\":"<<m.rebuilds<<",\"rows_rebuilt\":"<<m.rows_rebuilt
            <<",\"inplace_inserts\":"<<m.inplace_inserts<<",\"maintenance_bytes\":"<<index.memory_bytes()<<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

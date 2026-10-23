// Priority 3: real request-byte ledger. Every allocation is tagged with its size and tracked,
// so the transient coexistence of the old and new directory during a split is counted.
#include "bm_n1_index.hpp"
#include "bm_bounded_index.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kHeader=32;
std::uint64_t g_live=0,g_peak=0;
bool g_track=false;
}
void* operator new(std::size_t n) {
    void* raw=std::malloc(n+kHeader);
    if(!raw)throw std::bad_alloc();
    *static_cast<std::size_t*>(raw)=n;
    if(g_track){g_live+=n;if(g_live>g_peak)g_peak=g_live;}
    return static_cast<char*>(raw)+kHeader;
}
void operator delete(void* p) noexcept {
    if(!p)return;
    void* raw=static_cast<char*>(p)-kHeader;
    const std::size_t n=*static_cast<std::size_t*>(raw);
    if(g_track)g_live-=n;
    std::free(raw);
}
void operator delete(void* p,std::size_t) noexcept {operator delete(p);}

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
struct Ledger {std::uint64_t build_peak=0,run_peak=0,end_live=0;};
template<class Index>
Ledger replay(const std::vector<static_physical::Row>& rows,const std::vector<Operation>& ops) {
    Ledger out;
    g_live=0;g_peak=0;g_track=true;
    {
        Index index(rows);
        out.build_peak=g_peak;
        for(const auto& op:ops) {
            if(op.kind==1)(void)index.find_base(op.key);
            else if(op.kind==2)(void)index.insert(op.key,op.value);
            else if(op.kind==3)(void)index.update(op.key,op.value);
            else if(op.kind==4)(void)index.erase(op.key);
            else (void)index.scan(op.key,op.limit);
        }
        out.run_peak=g_peak;
        out.end_live=g_live;
    }
    g_track=false;
    return out;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin operations.bin");
        const auto initial=read_rows(argv[1]);
        const auto operations=read_operations(argv[2]);
        std::vector<static_physical::Row> rows;rows.reserve(initial.size());
        for(const auto& [k,v]:initial)rows.push_back({k,v});
        const auto base=replay<bm_bounded_page::Index>(rows,operations);
        const auto cand=replay<bm_n1_page::Index>(rows,operations);
        const auto ratio=[&](std::uint64_t a,std::uint64_t b){return b?double(a)/double(b):0.0;};
        std::cout<<"{\"rows\":"<<rows.size()<<",\"operations\":"<<operations.size()
            <<",\"baseline\":{\"build_peak\":"<<base.build_peak<<",\"run_peak\":"<<base.run_peak
            <<",\"end_live\":"<<base.end_live<<"}"
            <<",\"candidate\":{\"build_peak\":"<<cand.build_peak<<",\"run_peak\":"<<cand.run_peak
            <<",\"end_live\":"<<cand.end_live<<"}"
            <<",\"end_ratio\":"<<ratio(cand.end_live,base.end_live)
            <<",\"peak_ratio\":"<<ratio(cand.run_peak,base.run_peak)
            <<",\"build_peak_ratio\":"<<ratio(cand.build_peak,base.build_peak)
            <<",\"resident_ok\":"<<((ratio(cand.end_live,base.end_live)<=1.05)?"true":"false")<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

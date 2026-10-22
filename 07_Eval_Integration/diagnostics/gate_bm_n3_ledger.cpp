// Stage 3: incremental request-byte ledger on a real growth trace (baseline vs candidate).
#include "bm_n1_index.hpp"
#include "bm_bounded_index.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Operation {std::uint8_t kind=0;std::uint64_t value=0,limit=0;std::string key;};

std::vector<std::pair<std::string,std::uint64_t>> read_rows(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("cannot open rows");
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
    if(!in)throw std::runtime_error("cannot open operations");
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
template<class Index>
struct Ledger {std::size_t start=0,end=0,peak=0;};
template<class Index>
Ledger<Index> replay(const std::vector<static_physical::Row>& rows,const std::vector<Operation>& ops) {
    Index index(rows);
    Ledger<Index> ledger;ledger.start=index.memory_bytes();
    for(const auto& op:ops) {
        if(op.kind==1){(void)index.find_base(op.key);}
        else if(op.kind==2){(void)index.insert(op.key,op.value);}
        else if(op.kind==3){(void)index.update(op.key,op.value);}
        else if(op.kind==4){(void)index.erase(op.key);}
        else {(void)index.scan(op.key,op.limit);}
    }
    ledger.end=index.memory_bytes();
    ledger.peak=index.maintenance().accounted_peak_bytes;
    return ledger;
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
        const auto ratio=[&](std::size_t a,std::size_t b){return b?double(a)/b:0.0;};
        std::cout<<"{\"rows\":"<<rows.size()<<",\"operations\":"<<operations.size()
            <<",\"baseline\":{\"start\":"<<base.start<<",\"end\":"<<base.end
            <<",\"increment\":"<<(base.end-base.start)<<",\"peak\":"<<base.peak<<"}"
            <<",\"candidate\":{\"start\":"<<cand.start<<",\"end\":"<<cand.end
            <<",\"increment\":"<<(cand.end-cand.start)<<",\"peak\":"<<cand.peak<<"}"
            <<",\"end_ratio\":"<<ratio(cand.end,base.end)
            <<",\"increment_ratio\":"<<(base.end>base.start?ratio(cand.end-cand.start,base.end-base.start):0.0)
            <<",\"peak_ratio\":"<<ratio(cand.peak,base.peak)
            <<",\"within_1_05\":"<<((ratio(cand.end,base.end)<=1.05)?"true":"false")<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

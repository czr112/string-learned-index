// U1 second half: attribute each insert's latency to its trigger, using the page's own counters.
//   block_splits +  -> in-page block rebalance (the candidate path)
//   rebuilds    +  -> full page rebuild (page_full / arena_short / prefix_change)
//   neither        -> in-place slot write
#include "bm_split_index.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

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
struct Bucket {std::uint64_t count=0;double ns=0;};
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin operations.bin");
        const auto initial=read_rows(argv[1]);
        const auto ops=read_operations(argv[2]);
        std::vector<static_physical::Row> rows;rows.reserve(initial.size());
        std::map<std::string,std::uint64_t> oracle;
        for(const auto& [k,v]:initial){rows.push_back({k,v});oracle.emplace(k,v);}
        bm_split_page::Index index(rows);
        Bucket split,inplace,rebuild;
        auto prev_splits=bm_n1_leaf::reasons().block_splits;
        auto prev_rebuilds=index.maintenance().rebuilds;
        auto prev_rows=index.maintenance().rows_rebuilt;
        for(const auto& op:ops) {
            if(op.kind!=2)continue;
            if(oracle.count(op.key))continue;
            const auto t0=Clock::now();
            const bool ok=index.insert(op.key,op.value);
            const auto ns=double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t0).count());
            if(!ok)continue;
            oracle.emplace(op.key,op.value);
            const auto s=bm_n1_leaf::reasons().block_splits;
            const auto r=index.maintenance().rebuilds;
            if(s>prev_splits){split.count++;split.ns+=ns;}
            else if(r>prev_rebuilds){rebuild.count++;rebuild.ns+=ns;}
            else {inplace.count++;inplace.ns+=ns;}
            prev_splits=s;prev_rebuilds=r;
        }
        const auto total_ns=split.ns+inplace.ns+rebuild.ns;
        const auto share=[&](const Bucket& b){return total_ns?b.ns/total_ns:0.0;};
        const auto mean=[&](const Bucket& b){return b.count?b.ns/b.count/1000.0:0.0;};
        std::cout<<"{\"block_split\":{\"count\":"<<split.count<<",\"mean_us\":"<<mean(split)
            <<",\"time_share\":"<<share(split)<<"}"
            <<",\"page_rebuild\":{\"count\":"<<rebuild.count<<",\"mean_us\":"<<mean(rebuild)
            <<",\"time_share\":"<<share(rebuild)<<",\"rows_rebuilt\":"
            <<index.maintenance().rows_rebuilt-prev_rows<<"}"
            <<",\"inplace\":{\"count\":"<<inplace.count<<",\"mean_us\":"<<mean(inplace)
            <<",\"time_share\":"<<share(inplace)<<"}"
            <<",\"total_us\":"<<total_ns/1000.0<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// U1: how much of the write cost is the rebuild path? Per-insert latencies on the growth trace,
// split into the in-place majority and the slow rebuild minority, with the trigger counters.
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
double pct(std::vector<double> v,double q) {
    if(v.empty())return 0;std::sort(v.begin(),v.end());
    return v[std::size_t(double(v.size()-1)*q)];
}
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
        const auto before=index.maintenance();
        std::vector<double> fast,slow;
        const auto start=Clock::now();
        for(const auto& op:ops) {
            if(op.kind!=2)continue;
            if(oracle.count(op.key))continue;
            const auto t0=Clock::now();
            const bool ok=index.insert(op.key,op.value);
            const auto ns=double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t0).count());
            if(ok){oracle.emplace(op.key,op.value);}
            (ns>5000.0?slow:fast).push_back(ns);
        }
        const auto total_ns=double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
        const auto after=index.maintenance();
        double fast_sum=0;for(double x:fast)fast_sum+=x;
        double slow_sum=0;for(double x:slow)slow_sum+=x;
        const auto inserts=fast.size()+slow.size();
        std::cout<<"{\"inserts\":"<<inserts<<",\"fast\":"<<fast.size()<<",\"slow\":"<<slow.size()
            <<",\"slow_share_of_count\":"<<(inserts?double(slow.size())/inserts:0.0)
            <<",\"rebuild_path_share_of_time\":"<<((fast_sum+slow_sum)?slow_sum/(fast_sum+slow_sum):0.0)
            <<",\"fast_us\":{\"p50\":"<<pct(fast,0.50)/1000.0<<",\"p95\":"<<pct(fast,0.95)/1000.0
            <<",\"mean\":"<<(fast.empty()?0.0:(fast_sum/fast.size())/1000.0)<<"}"
            <<",\"slow_us\":{\"p50\":"<<pct(slow,0.50)/1000.0<<",\"p95\":"<<pct(slow,0.95)/1000.0
            <<",\"max\":"<<pct(slow,1.0)/1000.0<<",\"mean\":"<<(slow.empty()?0.0:(slow_sum/slow.size())/1000.0)<<"}"
            <<",\"rebuilds\":"<<after.rebuilds-before.rebuilds
            <<",\"inplace\":"<<after.inplace_inserts-before.inplace_inserts
            <<",\"rows_rebuilt\":"<<after.rows_rebuilt-before.rows_rebuilt
            <<",\"block_splits\":"<<bm_n1_leaf::reasons().block_splits
            <<",\"wall_us\":"<<total_ns/1000.0<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Historical diagnostic: its hot/hit grouping is invalid; retained for provenance.
// Use bm_query_recheck.cpp and the registered query-recheck runner for new evidence.
// Targeted query diagnosis: split complete-find cost by hit/miss and by key region, on the
// audited growth trace, after the same insert replay on both implementations.
#include "bm_split_index.hpp"
#include "bm_bounded_index.hpp"
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
template<class Index>
std::uint64_t replay(Index& index,const std::vector<Operation>& ops,std::map<std::string,std::uint64_t>& oracle) {
    std::uint64_t bad=0;
    for(const auto& op:ops) {
        if(op.kind==1)continue;
        if(op.kind==2){const bool p=oracle.count(op.key)!=0;const bool ok=index.insert(op.key,op.value);
            if(ok==p)++bad; if(ok)oracle.emplace(op.key,op.value);}
        else if(op.kind==3){const bool p=oracle.count(op.key)!=0;const bool ok=index.update(op.key,op.value);
            if(ok!=p)++bad; if(p)oracle[op.key]=op.value;}
        else if(op.kind==4){const bool p=oracle.count(op.key)!=0;const bool ok=index.erase(op.key);
            if(ok!=p)++bad; if(p)oracle.erase(op.key);}
        else {(void)index.scan(op.key,op.limit);}
    }
    return bad;
}
template<class Index>
double time_find(const Index& index,const std::vector<std::string>& keys,std::uint64_t& sink,int repeats) {
    std::uint64_t ns=0;
    for(int r=0;r<repeats;++r) {
        const auto start=Clock::now();
        for(const auto& k:keys)sink+=(index.find_base(k)?1:0);
        ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
    }
    return double(ns)/double(repeats)/double(keys.size())/1000.0;
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
        bm_split_page::Index candidate(rows);
        bm_bounded_page::Index baseline(rows);
        std::map<std::string,std::uint64_t> o1=oracle,o2=oracle;
        const auto bad1=replay(candidate,ops,o1);
        const auto bad2=replay(baseline,ops,o2);
        if(bad1||bad2||o1.size()!=o2.size())throw std::logic_error("replay mismatch");

        std::vector<std::string> hits,misses,hot,cold;
        const auto n=initial.size();
        for(std::size_t i=0;i<n;i+=7) {
            const auto& key=initial[i].first;
            hits.push_back(key);
            (i*10/n%10<1?hot:cold).push_back(key);
        }
        for(std::size_t i=0;i<n;i+=53)misses.push_back(initial[i].first+"~absent");

        std::uint64_t sink=0;
        const int repeats=20;
        struct Row {const char* name;const std::vector<std::string>* keys;};
        const Row sets[]={{"hit", &hits},{"miss",&misses},{"hot", &hot},{"cold",&cold}};
        std::cout<<"{";
        bool first=true;
        for(const auto& s:sets) {
            if(s.keys->empty())continue;
            const auto c=time_find(candidate,*s.keys,sink,repeats);
            const auto b=time_find(baseline,*s.keys,sink,repeats);
            if(!first)std::cout<<",";
            first=false;
            std::cout<<"\""<<s.name<<"\":{\"candidate_us\":"<<c<<",\"baseline_us\":"<<b
                     <<",\"ratio\":"<<(b?c/b:0.0)<<",\"keys\":"<<s.keys->size()<<"}";
        }
        std::cout<<",\"checks\":{\"candidate_bad\":"<<bad1<<",\"baseline_bad\":"<<bad2
                 <<",\"final_rows\":"<<o1.size()<<"},\"sink\":"<<sink<<"}\n";
        return (bad1||bad2)?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

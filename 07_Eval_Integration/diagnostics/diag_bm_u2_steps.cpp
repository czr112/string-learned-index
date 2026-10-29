// U2: do the candidate's pages need more directory steps after the same inserts?
// Same initial rows, same insert replay, then count directory steps per public find_base.
#include "bm_split_index.hpp"
#include "bm_bounded_index.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

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
void replay(Index& index,const std::vector<Operation>& ops,std::map<std::string,std::uint64_t>& oracle) {
    for(const auto& op:ops) {
        if(op.kind==1)continue;
        if(op.kind==2){if(!oracle.count(op.key)&&index.insert(op.key,op.value))oracle.emplace(op.key,op.value);}
        else if(op.kind==3){if(oracle.count(op.key)){index.update(op.key,op.value);oracle[op.key]=op.value;}}
        else if(op.kind==4){if(oracle.count(op.key)){index.erase(op.key);oracle.erase(op.key);}}
        else {(void)index.scan(op.key,op.limit);}
    }
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin operations.bin");
        const auto initial=read_rows(argv[1]);
        const auto ops=read_operations(argv[2]);
        std::vector<static_physical::Row> rows;rows.reserve(initial.size());
        for(const auto& [k,v]:initial)rows.push_back({k,v});
        std::map<std::string,std::uint64_t> o1,o2;
        for(const auto& [k,v]:initial){o1.emplace(k,v);o2.emplace(k,v);}

        bm_split_page::Index candidate(rows);
        bm_bounded_page::Index baseline(rows);
        replay(candidate,ops,o1);
        replay(baseline,ops,o2);

        std::vector<std::string> probes;
        for(std::size_t i=0;i<initial.size();i+=7)probes.push_back(initial[i].first);
        for(std::size_t i=0;i<initial.size();i+=53)probes.push_back(initial[i].first+"~absent");

        std::uint64_t cand_steps=0,base_steps=0,checks=0;
        for(const auto& key:probes) {
            bm_n1_leaf::Work wc; bm_leaf::Work wb;
            (void)candidate.find_counted(key,wc);
            (void)baseline.find_counted(key,wb);
            // the two index types expose different Work classes; reuse the counters we have
            cand_steps+=std::uint64_t(wc.directory_steps);
            base_steps+=std::uint64_t(wb.directory_steps);
            ++checks;
        }
        std::cout<<"{\"probes\":"<<checks<<",\"pages\":{\"candidate\":"<<candidate.page_count()
            <<",\"baseline\":"<<baseline.page_count()<<"}"
            <<",\"directory_steps\":{\"candidate\":"<<cand_steps<<",\"baseline\":"<<base_steps
            <<",\"per_lookup_candidate\":"<<(checks?double(cand_steps)/checks:0.0)
            <<",\"per_lookup_baseline\":"<<(checks?double(base_steps)/checks:0.0)
            <<",\"ratio\":"<<(base_steps?double(cand_steps)/base_steps:0.0)<<"}"
            <<",\"final_rows\":"<<o1.size()<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

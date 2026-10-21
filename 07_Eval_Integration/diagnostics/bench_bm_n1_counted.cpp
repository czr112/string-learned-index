// N1: classify every rebuild - page-count increase (split) versus in-place rebuild - on the
// four growth traces, straight from the maintenance counters. No index change.
#include "bm_n1_index.hpp"
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
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin operations.bin label");
        const std::string label=argv[3];
        const auto initial=read_rows(argv[1]);
        const auto operations=read_operations(argv[2]);
        std::vector<static_physical::Row> physical;physical.reserve(initial.size());
        std::map<std::string,std::uint64_t> oracle;
        for(const auto& [key,value]:initial){physical.push_back({key,value});oracle.emplace(key,value);}
        bm_n1_page::Index index(physical);
        std::uint64_t mismatches=0,splits=0,inplace_rebuilds=0,inplace_inserts=0,inserts=0;
        std::uint64_t rows_at_rebuild=0,page_full_rebuilds=0;
        auto pages=index.page_count();
        auto maintenance=index.maintenance();
        for(const auto& op:operations) {
            if(op.kind==1) {
                const auto want=oracle.find(op.key);
                const auto got=index.find_base(op.key);
                if((want==oracle.end())!=!got||(got&&want!=oracle.end()&&*got!=want->second))++mismatches;
                continue;
            }
            if(op.kind!=2)throw std::invalid_argument("growth only");
            const bool present=oracle.find(op.key)!=oracle.end();
            const bool ok=index.insert(op.key,op.value);
            if(ok==present)++mismatches;
            if(!ok)continue;
            oracle.emplace(op.key,op.value);
            ++inserts;
            const auto next=index.maintenance();
            const auto now_pages=index.page_count();
            if(next.rebuilds>maintenance.rebuilds) {
                if(now_pages>pages)++splits;else {++inplace_rebuilds;rows_at_rebuild+=next.rows_rebuilt-maintenance.rows_rebuilt;}
            } else ++inplace_inserts;
            pages=now_pages;maintenance=next;
        }
        std::cout<<"{\"label\":\""<<label<<"\",\"initial_rows\":"<<initial.size()
            <<",\"inserts\":"<<inserts<<",\"inplace_inserts\":"<<inplace_inserts
            <<",\"rebuilds\":"<<(splits+inplace_rebuilds)<<",\"splits\":"<<splits
            <<",\"inplace_rebuilds\":"<<inplace_rebuilds
            <<",\"split_share\":"<<((splits+inplace_rebuilds)?double(splits)/(splits+inplace_rebuilds):0.0)
            <<",\"inplace_rebuild_share\":"<<((splits+inplace_rebuilds)?double(inplace_rebuilds)/(splits+inplace_rebuilds):0.0)
            <<",\"rows_per_inplace_rebuild\":"<<(inplace_rebuilds?double(rows_at_rebuild)/inplace_rebuilds:0.0)
            <<",\"pages_after\":"<<index.page_count()<<",\"mismatches\":"<<mismatches<<",\"page_full\":"<<bm_n1_leaf::reasons().page_full<<",\"prefix_change\":"<<bm_n1_leaf::reasons().prefix_change<<",\"slot_full\":"<<bm_n1_leaf::reasons().slot_full<<",\"arena_short\":"<<bm_n1_leaf::reasons().arena_short<<",\"duplicates\":"<<bm_n1_leaf::reasons().duplicates<<",\"reasons_inserted\":"<<bm_n1_leaf::reasons().inserted<<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

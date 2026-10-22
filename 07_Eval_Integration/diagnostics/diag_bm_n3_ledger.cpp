// Diagnose the "sorted unique rows required" abort in the ledger replay.
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
} // namespace

int main(int argc,char** argv) {
    if(argc!=4)throw std::invalid_argument("rows ops which(baseline|candidate)");
    const std::string which=argv[3];
    const auto initial=read_rows(argv[1]);
    const auto operations=read_operations(argv[2]);
    std::vector<static_physical::Row> rows;rows.reserve(initial.size());
    for(const auto& [k,v]:initial)rows.push_back({k,v});
    std::cout<<"rows "<<rows.size()<<" ops "<<operations.size()<<" target "<<which<<std::endl;
    try {
        if(which=="baseline") {
            bm_bounded_page::Index index(rows);
            std::cout<<"constructed baseline"<<std::endl;
            std::size_t n=0;
            for(const auto& op:operations) {
                try {
                    if(op.kind==1){(void)index.find_base(op.key);}
                    else if(op.kind==2){(void)index.insert(op.key,op.value);}
                    else if(op.kind==3){(void)index.update(op.key,op.value);}
                    else if(op.kind==4){(void)index.erase(op.key);}
                    else {(void)index.scan(op.key,op.limit);}
                } catch(const std::exception& e) {
                    std::cout<<"FAILED at op "<<n<<" kind "<<int(op.kind)<<" key_len "<<op.key.size()
                             <<" : "<<e.what()<<std::endl;return 2;
                }
                ++n;
            }
            std::cout<<"completed baseline"<<std::endl;
        } else {
            bm_n1_page::Index index(rows);
            std::cout<<"constructed candidate"<<std::endl;
            std::size_t n=0;
            for(const auto& op:operations) {
                try {
                    if(op.kind==1){(void)index.find_base(op.key);}
                    else if(op.kind==2){(void)index.insert(op.key,op.value);}
                    else if(op.kind==3){(void)index.update(op.key,op.value);}
                    else if(op.kind==4){(void)index.erase(op.key);}
                    else {(void)index.scan(op.key,op.limit);}
                } catch(const std::exception& e) {
                    std::cout<<"FAILED at op "<<n<<" kind "<<int(op.kind)<<" key_len "<<op.key.size()
                             <<" : "<<e.what()<<std::endl;return 2;
                }
                ++n;
            }
            std::cout<<"completed candidate"<<std::endl;
        }
        return 0;
    } catch(const std::exception& e) {
        std::cout<<"FAILED before replay: "<<e.what()<<std::endl;return 3;
    }
}

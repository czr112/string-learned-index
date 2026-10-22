// Stage 2 gate: throw on a chosen allocation during a split and prove nothing is lost.
#include "bm_n1_index.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint64_t g_calls=0,g_throw_at=0;
bool g_armed=false;
}
void* operator new(std::size_t n) {
    if(g_armed){if(++g_calls==g_throw_at)throw std::bad_alloc();}
    void* p=std::malloc(n?n:1);
    if(!p)throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin throw_at");
        const std::uint64_t throw_at=std::stoull(argv[2]);
        std::ifstream in(argv[1],std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
        std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
        std::size_t pos=16;std::vector<static_physical::Row> rows;
        std::map<std::string,std::uint64_t> reference;
        for(std::uint64_t i=0;i<count;++i) {
            std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
            std::string key=bytes.substr(pos,n);pos+=n;
            std::uint64_t value=0;std::memcpy(&value,bytes.data()+pos,8);pos+=8;
            rows.push_back({key,value});reference.emplace(key,value);
        }
        bm_n1_page::Index index(rows);
        std::uint64_t injected=0,recovered=0,losses=0;
        for(int i=0;i<600;++i) {
            std::string key=rows[(std::size_t(i)*rows.size())/600].key+"~fault/"+std::to_string(i);
            g_calls=0;g_throw_at=throw_at;g_armed=true;
            try {
                const bool ok=index.insert(key,9'000+i);
                g_armed=false;
                if(ok)reference.emplace(key,9'000+i);
            } catch(const std::bad_alloc&) {
                g_armed=false;++injected;
                // Every key that existed before the failed insert must still be readable.
                bool lost=false;
                for(const auto& [k,v]:reference)
                    if(index.find_base(k)!=std::optional<std::uint64_t>(v)){lost=true;break;}
                if(lost)++losses;
                if(index.verify()==0)++losses;
                // And the index must still accept work afterwards.
                if(index.insert(key,9'000+i)){reference.emplace(key,9'000+i);++recovered;}
            }
        }
        std::uint64_t mismatches=0;
        for(const auto& [k,v]:reference)
            if(index.find_base(k)!=std::optional<std::uint64_t>(v))++mismatches;
        std::cout<<"{\"throw_at\":"<<throw_at<<",\"injected\":"<<injected
            <<",\"recovered\":"<<recovered<<",\"losses\":"<<losses
            <<",\"final_rows\":"<<reference.size()<<",\"mismatches\":"<<mismatches<<"}\n";
        return (losses==0&&mismatches==0)?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

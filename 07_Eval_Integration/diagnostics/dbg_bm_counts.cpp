#include "bm_n1_index.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
int main(int argc,char** argv){
    if(argc!=2)return 1;
    std::ifstream in(argv[1],std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<static_physical::Row> rows;
    for(std::uint64_t i=0;i<count;++i){
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        std::string key=bytes.substr(pos,n);pos+=n;
        std::uint64_t v=0;std::memcpy(&v,bytes.data()+pos,8);pos+=8;
        rows.push_back({key,v});
    }
    bm_n1_page::Index index(rows);
    std::cout<<"after build: "<<index.counts()<<"\n";
    auto before=index.maintenance().rebuilds;
    int splits=0;
    for(std::size_t i=0;i<rows.size()&&splits<1;++i){
        std::string key=rows[i].key+"~dbg";
        index.insert(key,1);
        if(index.maintenance().rebuilds>before){++splits;before=index.maintenance().rebuilds;
            std::cout<<"after split "<<i<<": "<<index.counts()<<"\n";}
    }
    return 0;
}

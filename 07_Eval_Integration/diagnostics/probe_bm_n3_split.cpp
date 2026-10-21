// N3 probe: insert one synthetic key at a time and verify() after each, to localise a crash.
#include "bm_n1_index.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin inserts");
        const std::size_t limit=std::stoul(argv[2]);
        std::ifstream in(argv[1],std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
        std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
        std::size_t pos=16;std::vector<static_physical::Row> rows;
        for(std::uint64_t i=0;i<count;++i) {
            std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
            std::string key=bytes.substr(pos,n);pos+=n;
            std::uint64_t value=0;std::memcpy(&value,bytes.data()+pos,8);pos+=8;
            rows.push_back({key,value});
        }
        bm_n1_page::Index index(rows);
        std::cout<<"loaded rows="<<rows.size()<<" checks="<<index.verify()<<std::endl;
        for(std::size_t i=0;i<limit;++i) {
            const auto source=(i*rows.size())/limit;
            std::string key=rows[source].key+"~probe/"+std::to_string(i);
            std::cout<<"insert "<<i<<" of "<<key.size()<<" bytes"<<std::flush;
            const bool ok=index.insert(key,7'000+i);
            std::cout<<" -> "<<(ok?"inserted":"duplicate")<<std::flush;
            const auto checks=index.verify();
            std::cout<<" verify="<<checks<<" splits="<<bm_n1_leaf::reasons().block_splits
                     <<" slot="<<bm_n1_leaf::reasons().slot_full<<std::endl;
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"exception: "<<e.what()<<'\n';return 1;}
}

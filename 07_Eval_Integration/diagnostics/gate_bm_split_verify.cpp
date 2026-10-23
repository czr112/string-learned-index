// Step 1 gate: the bounded candidate must keep verify() true across block splits.
#include "bm_split_index.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin dense_inserts");
        const std::size_t dense=std::stoul(argv[2]);
        std::ifstream in(argv[1],std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
        std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
        std::size_t pos=16;
        std::vector<static_physical::Row> rows;
        rows.reserve(static_cast<std::size_t>(count));
        std::map<std::string,std::uint64_t> reference;
        for(std::uint64_t i=0;i<count;++i) {
            std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
            std::string key=bytes.substr(pos,n);pos+=n;
            std::uint64_t value=0;std::memcpy(&value,bytes.data()+pos,8);pos+=8;
            rows.push_back({key,value});
            reference.emplace(key,value);
        }
        bm_split_page::Index index(rows);      // bounded build, page by page
        std::uint64_t checks=index.verify();
        const auto bytes_before=index.memory_bytes();
        const std::string root=rows[rows.size()/2].key;
        std::uint64_t inserted=0;
        for(std::size_t i=0;i<dense;++i) {
            const std::string key=root+"~v/"+std::to_string(i);
            if(index.insert(key,5'000+i)){reference.emplace(key,5'000+i);++inserted;}
        }
        checks+=index.verify();
        std::uint64_t mismatches=0;
        for(const auto& [k,v]:reference)
            if(index.find_base(k)!=std::optional<std::uint64_t>(v))++mismatches;
        const auto scan=index.scan("",SIZE_MAX);
        if(scan.size()!=reference.size())++mismatches;
        std::cout<<"{\"rows\":"<<rows.size()<<",\"dense\":"<<dense<<",\"inserted\":"<<inserted
            <<",\"verify_checks\":"<<checks<<",\"mismatches\":"<<mismatches
            <<",\"rebuilds\":"<<index.maintenance().rebuilds
            <<",\"inplace\":"<<index.maintenance().inplace_inserts
            <<",\"bytes_before\":"<<bytes_before<<",\"bytes_after\":"<<index.memory_bytes()
            <<",\"final_rows\":"<<scan.size()<<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

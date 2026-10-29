#include "bm_n1_page_root.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin split_block");
        const std::uint32_t split=std::stoul(argv[2]);
        std::ifstream in(argv[1],std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
        std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
        std::size_t pos=16;std::vector<static_physical::Row> rows;
        for(std::uint64_t i=0;i<count&&i<2048;++i) {
            std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
            std::string key=bytes.substr(pos,n);pos+=n;
            std::uint64_t v=0;std::memcpy(&v,bytes.data()+pos,8);pos+=8;
            rows.push_back({key,v});
        }
        bm_n1_leaf::Page page(rows,15);
        const auto before=page.scan("",SIZE_MAX);
        auto upper=page.split_at_block(split);
        const auto lower_scan=page.scan("",SIZE_MAX);
        const auto upper_scan=upper->scan("",SIZE_MAX);
        std::uint64_t mismatches=0;
        if(lower_scan.size()+upper_scan.size()!=before.size())++mismatches;
        else {
            std::vector<static_physical::Row> joined=lower_scan;
            joined.insert(joined.end(),upper_scan.begin(),upper_scan.end());
            for(std::size_t i=0;i<before.size();++i)
                if(joined[i].key!=before[i].key||joined[i].value!=before[i].value){++mismatches;break;}
        }
        for(const auto& row:before) {
            const auto a=page.find_base(row.key), b=upper->find_base(row.key);
            if((a.has_value()==b.has_value())&&((!a&&!b)||(a&&b&&*a==*b)))++mismatches;
            const auto got=a?a:b;
            if(!got||*got!=row.value)++mismatches;
        }
        std::cout<<"{\"rows\":"<<before.size()<<",\"split_block\":"<<split
                 <<",\"lower_rows\":"<<lower_scan.size()<<",\"upper_rows\":"<<upper_scan.size()
                 <<",\"mismatches\":"<<mismatches<<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// U3 step 2 (measurement before wiring): cost of split_at_block versus the current
// scan + reconstruct path, on the same 2,048-row page.
#include "bm_n1_page_root.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
namespace {
std::vector<static_physical::Row> read_page(const char* path,std::size_t take) {
    std::ifstream in(path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows");
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<static_physical::Row> rows;
    for(std::uint64_t i=0;i<count&&rows.size()<take;++i) {
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        std::string key=bytes.substr(pos,n);pos+=n;
        std::uint64_t v=0;std::memcpy(&v,bytes.data()+pos,8);pos+=8;
        rows.push_back({key,v});
    }
    return rows;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("rows.bin");
        const auto rows=read_page(argv[1],2048);
        if(rows.size()<2048)throw std::invalid_argument("need 2048 rows");
        const int repeats=40;

        // Current path: scan the page into strings, then build a fresh page from those rows.
        double old_us=0;
        {
            std::uint64_t sink=0;
            const auto start=Clock::now();
            for(int r=0;r<repeats;++r) {
                bm_n1_leaf::Page page(rows,15);
                auto scanned=page.scan("",SIZE_MAX);
                // the rebuild path then builds two halves from that scan
                std::vector<static_physical::Row> lo(scanned.begin(),scanned.begin()+1024);
                std::vector<static_physical::Row> hi(scanned.begin()+1024,scanned.end());
                bm_n1_leaf::Page a(lo,15),b(hi,15);
                sink+=a.size()+b.size();
            }
            const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now()-start).count());
            if(sink==0)throw std::logic_error("no rows");
            old_us=double(ns)/repeats/1000.0;
        }
        // New path: build once, then split in place (no strings materialised).
        double new_us=0;
        {
            std::uint64_t sink=0;
            const auto start=Clock::now();
            for(int r=0;r<repeats;++r) {
                bm_n1_leaf::Page page(rows,15);
                auto upper=page.split_at_block(68);   // ~half of the ~137 blocks
                sink+=page.size()+upper->size();
            }
            const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now()-start).count());
            if(sink==0)throw std::logic_error("no rows");
            new_us=double(ns)/repeats/1000.0;
        }
        std::cout<<"{\"rows\":2048,\"old_us\":"<<old_us<<",\"new_us\":"<<new_us
            <<",\"reduction\":"<<(old_us?(1.0-new_us/old_us):0.0)<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

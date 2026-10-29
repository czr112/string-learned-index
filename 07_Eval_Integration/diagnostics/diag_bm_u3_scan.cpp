// U3 step 0b: how much of a page rebuild is the scan() that materialises 2,048 std::string rows?
#include "bm_split_index.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
namespace {
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
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("rows.bin");
        const auto initial=read_rows(argv[1]);
        std::vector<static_physical::Row> rows;
        for(const auto& [k,v]:initial)rows.push_back({k,v});
        bm_split_page::Index index(rows);

        // Time the scan of one full page's worth of rows (the rebuild path does exactly this).
        const auto scan_us=[&](int repeats) {
            std::uint64_t sink=0;
            const auto start=Clock::now();
            for(int r=0;r<repeats;++r) {
                const auto out=index.scan("",SIZE_MAX);
                sink+=out.size();
            }
            const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now()-start).count());
            if(sink==0)throw std::logic_error("empty scan");
            return double(ns)/repeats/1000.0;
        };
        // Time constructing one 2,048-row page from those rows.
        std::vector<static_physical::Row> part;
        const std::size_t take=std::min<std::size_t>(2048,rows.size());
        for(std::size_t i=0;i<take;++i)part.push_back(rows[i]);
        const auto page_us=[&](int repeats) {
            const auto start=Clock::now();
            for(int r=0;r<repeats;++r){bm_n1_leaf::Page page(part,15);}
            const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now()-start).count());
            return double(ns)/repeats/1000.0;
        };
        const auto full_scan=scan_us(20);
        const auto one_page=page_us(20);
        // Total rows / rows per page = page count, so a per-page share of the full scan:
        const auto pages=index.page_count();
        std::cout<<"{\"rows\":"<<rows.size()<<",\"pages\":"<<pages
            <<",\"full_scan_us\":"<<full_scan
            <<",\"scan_per_page_us\":"<<(pages?full_scan/pages:0.0)
            <<",\"page_construct_us\":"<<one_page
            <<",\"rebuild_estimate_us\":"<<(one_page+(pages?full_scan/pages:0.0))
            <<",\"scan_share\":"
            <<((one_page+(pages?full_scan/pages:0.0))?((pages?full_scan/pages:0.0)/(one_page+(pages?full_scan/pages:0.0))):0.0)
            <<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

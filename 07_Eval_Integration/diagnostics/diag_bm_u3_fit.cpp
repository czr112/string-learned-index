// U3 step 0: inside a page rebuild, how much is model fitting versus arena copying?
// Public-API probe: time (a) constructing a page of N rows, (b) BlockModel::fit on the same
// number of block-boundary keys. The difference bounds what "do not re-encode" could save.
#include "bm_split_index.hpp"
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
        // Take 2,048 rows (one full page) to construct pages of several sizes.
        std::vector<static_physical::Row> base;
        const std::size_t take=std::min<std::size_t>(2048,initial.size());
        for(std::size_t i=0;i<take;++i)base.push_back({initial[i].first,initial[i].second});

        auto time_page=[&](std::size_t n,int repeats) {
            std::vector<static_physical::Row> part(base.begin(),base.begin()+std::ptrdiff_t(n));
            const auto start=Clock::now();
            for(int r=0;r<repeats;++r){bm_n1_leaf::Page page(part,15);}
            const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now()-start).count());
            return double(ns)/repeats/1000.0;
        };
        const auto p512=time_page(512,40);
        const auto p1024=time_page(1024,30);
        const auto p2048=time_page(2048,20);

        // Model fitting alone, over the same number of block-boundary keys the page trains on.
        std::vector<std::string_view> fences;
        for(std::size_t i=0;i<take;i+=20)fences.push_back(std::string_view(initial[i].first));
        const auto fit_start=Clock::now();
        int fits=0;
        for(int r=0;r<20;++r){auto model=blocklearn::BlockModel::fit(fences,16,1,7,3);++fits;}
        const auto fit_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now()-fit_start).count());
        const auto fit_us=double(fit_ns)/fits/1000.0;

        std::cout<<"{\"rows_available\":"<<initial.size()
            <<",\"page_us\":{\"512\":"<<p512<<",\"1024\":"<<p1024<<",\"2048\":"<<p2048<<"}"
            <<",\"fit_us\":{\"fences\":"<<fences.size()<<",\"per_fit\":"<<fit_us<<"}"
            <<",\"fit_share_of_page_2048\":"<<(p2048?fit_us/p2048:0.0)<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

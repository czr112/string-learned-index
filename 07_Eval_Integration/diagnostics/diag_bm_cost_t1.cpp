// T1 cost attribution: measure the four identified cost sources on the current candidate.
//   (1) duplicate lookup: insert(existing) should cost about two searches if the key is looked
//       up once by the caller and again inside Page::try_insert;
//   (2) directory copy per split: bytes moved per page rebuild;
//   (3) arena retention after erase: no suffix space is reclaimed;
//   (4) scan output buffering: allocations per scan call.
#include "bm_split_index.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
namespace {
std::uint64_t g_allocs=0; bool g_count=false;
}
void* operator new(std::size_t n) {
    if(g_count)++g_allocs;
    void* p=std::malloc(n?n:1);
    if(!p)throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

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
template<class F> double us_per_call(F&& f,int repeats) {
    const auto start=Clock::now();
    for(int r=0;r<repeats;++r)f();
    const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
    return double(ns)/repeats/1000.0;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("rows.bin");
        const auto initial=read_rows(argv[1]);
        std::vector<static_physical::Row> rows;rows.reserve(initial.size());
        for(const auto& [k,v]:initial)rows.push_back({k,v});
        bm_split_page::Index index(rows);

        // (1) duplicate lookup cost: one find versus one refused insert of an existing key.
        const std::string probe=initial[initial.size()/2].first;
        std::uint64_t sink=0;
        const auto find_us=us_per_call([&]{for(int i=0;i<1000;++i)sink+=index.find_base(probe)?1:0;},200);
        const auto dup_us=us_per_call([&]{for(int i=0;i<1000;++i)sink+=index.insert(probe,7)?1:0;},200);
        const double find1=find_us/1000.0, dup1=dup_us/1000.0;

        // (2) directory bytes moved per split: compare memory growth across dense inserts.
        const auto bytes_before=index.memory_bytes();
        const auto reb_before=index.maintenance().rebuilds;
        const std::string root=initial[initial.size()/2].first;
        for(int i=0;i<2000;++i)index.insert(root+"~t1/"+std::to_string(i),9'000+i);
        const auto bytes_after=index.memory_bytes();
        const auto splits=index.maintenance().rebuilds-reb_before;

        // (3) arena retention: erase 1000 keys, then insert 1000 different ones.
        const auto bytes_pre_erase=index.memory_bytes();
        std::uint64_t erased=0;
        for(std::size_t i=0;i<1000&&i<initial.size();++i)if(index.erase(initial[i].first))++erased;
        const auto bytes_post_erase=index.memory_bytes();
        for(int i=0;i<1000;++i)index.insert(std::string("~arena/")+std::to_string(i),1'000+i);
        const auto bytes_post_fill=index.memory_bytes();

        // (4) scan buffering: allocations per scan of 64 rows.
        g_count=true;g_allocs=0;
        for(int i=0;i<100;++i){(void)index.scan(initial[i*7%initial.size()].first,64);}
        g_count=false;
        const auto scan_allocs=g_allocs;

        std::cout<<"{\"rows\":"<<initial.size()<<",\"spare\":";
        (void)sink;
        std::cout<<"0,\"dup_lookup\":{\"find_us\":"<<find1<<",\"insert_existing_us\":"<<dup1
            <<",\"ratio\":"<<(find1?dup1/find1:0.0)<<"}"
            <<",\"splits\":{\"count\":"<<splits<<",\"bytes_before\":"<<bytes_before
            <<",\"bytes_after\":"<<bytes_after
            <<",\"bytes_per_split\":"<<(splits?double(bytes_after-bytes_before)/splits:0.0)<<"}"
            <<",\"arena\":{\"pre_erase\":"<<bytes_pre_erase<<",\"post_erase\":"<<bytes_post_erase
            <<",\"post_fill\":"<<bytes_post_fill<<",\"erased\":"<<erased
            <<",\"retained_after_erase\":"<<(std::int64_t(bytes_post_erase)-std::int64_t(bytes_pre_erase))
            <<",\"growth_on_refill\":"<<(std::int64_t(bytes_post_fill)-std::int64_t(bytes_post_erase))<<"}"
            <<",\"scan\":{\"calls\":100,\"rows\":64,\"allocations\":"<<scan_allocs
            <<",\"allocs_per_call\":"<<scan_allocs/100.0<<"}}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

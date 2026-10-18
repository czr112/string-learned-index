// E2 space ledger: one implementation per process, so the whole-process peak and the glibc
// heap delta belong to a single object. Reports (a) the object's own accounting,
// (b) glibc mallinfo2 in-use and mmapped deltas, (c) VmHWM/VmRSS, and (d) exact requested
// bytes for allocations that go through malloc/calloc/realloc at link time (libart's C code).
// std::operator new inside libstdc++ is NOT wrapped, so for C++ implementations the wrapped
// numbers stay near zero and the mallinfo2 delta is the figure to read.
#include "collision_refinement_adapter.hpp"
#include "our_static_refined_adapter.h"
#include "../adapters/art/art_adapter.h"
#include "../adapters/hot/hot_adapter.h"
#include "static_input.hpp"
#include <malloc.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;

static std::uint64_t w_live=0,w_peak=0,w_requested=0,w_usable_total=0,w_allocations=0,w_frees=0;
static void account(void* p,std::size_t requested) {
    if(!p)return;
    const auto usable=std::uint64_t(malloc_usable_size(p));
    ++w_allocations;
    w_usable_total+=usable;w_requested+=requested;w_live+=usable;
    if(w_live>w_peak)w_peak=w_live;
}
static void release(void* p) {
    if(!p)return;
    const auto usable=std::uint64_t(malloc_usable_size(p));
    w_live=w_live>=usable?w_live-usable:0;
    ++w_frees;
}
extern "C" {
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t,std::size_t);
void* __real_realloc(void*,std::size_t);
void __real_free(void*);
void* __wrap_malloc(std::size_t n) {void* p=__real_malloc(n);account(p,n);return p;}
void* __wrap_calloc(std::size_t a,std::size_t b) {void* p=__real_calloc(a,b);account(p,a*b);return p;}
void* __wrap_realloc(void* old,std::size_t n) {
    const std::size_t old_usable=old?std::size_t(malloc_usable_size(old)):0;
    void* p=__real_realloc(old,n);
    if(p) {w_live=w_live>=old_usable?w_live-old_usable:0;account(p,n);}
    return p;
}
void __wrap_free(void* p) {release(p);__real_free(p);}
}

struct Heap {std::uint64_t inuse=0,mmapped=0,arena=0;};
static Heap heap_now() {const auto m=mallinfo2();return {m.uordblks,m.hblkhd,m.arena};}
struct Vm {std::uint64_t hwm_kb=0,rss_kb=0;};
static Vm vm_now() {
    std::ifstream status("/proc/self/status");std::string line;Vm v;
    while(std::getline(status,line)) {
        auto pick=[&](const char* name) {
            const std::string prefix=std::string(name)+":";
            if(line.rfind(prefix,0)==0)return std::uint64_t(std::stoull(line.substr(prefix.size())));
            return std::uint64_t(0);
        };
        if(line.rfind("VmHWM:",0)==0)v.hwm_kb=pick("VmHWM");
        if(line.rfind("VmRSS:",0)==0)v.rss_kb=pick("VmRSS");
    }
    return v;
}
static void number(const std::optional<std::uint64_t>& n) {if(n)std::cout<<*n;else std::cout<<"null";}

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin arm(art|hot|integrated|prototype)");
        const std::string arm=argv[2];
        std::vector<sib::Row> rows;
        {auto rs=static_blocks::buffered(argv[1]);rows=uni::read_rows(rs);}
        if(rows.empty())throw std::invalid_argument("empty rows");

        const auto heap_before=heap_now();const auto vm_before=vm_now();
        const auto start=std::chrono::steady_clock::now();
        std::unique_ptr<sib::Index> index;
        if(arm=="art")index=make_art_index({});
        else if(arm=="hot")index=make_hot_index({});
        else if(arm=="integrated")index=make_our_static_refined_index({});
        else if(arm=="prototype")index=std::make_unique<refinement_adapter::Adapter<collision_refined::Index,static_physical::Lookup::learned>>();
        else throw std::invalid_argument("unknown arm");
        index->bulk_load(rows);
        const auto build_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start).count());
        const auto heap_after=heap_now();const auto vm_after=vm_now();
        const auto descriptor=index->descriptor();const auto usage=index->memory_usage();

        std::cout<<"{\"arm\":\""<<arm<<"\",\"rows\":"<<rows.size()
            <<",\"implementation_version\":"<<std::quoted(descriptor.implementation_version)
            <<",\"adapter_version\":"<<std::quoted(descriptor.adapter_version)
            <<",\"build_ns\":"<<build_ns
            <<",\"index_bytes\":";number(usage.index_owned_bytes);
        std::cout<<",\"adapter_bytes\":";number(usage.adapter_owned_bytes);
        std::cout<<",\"retained_input_bytes\":";number(usage.retained_input_bytes);
        std::cout<<",\"self_total\":";
        if(usage.index_owned_bytes)std::cout<<*usage.index_owned_bytes+(usage.adapter_owned_bytes?*usage.adapter_owned_bytes:0);
        else std::cout<<"null";
        std::cout<<",\"heap_inuse_delta\":"<<(heap_after.inuse>=heap_before.inuse?heap_after.inuse-heap_before.inuse:0)
            <<",\"heap_mmapped_delta\":"<<(heap_after.mmapped>=heap_before.mmapped?heap_after.mmapped-heap_before.mmapped:0)
            <<",\"heap_inuse_after\":"<<heap_after.inuse<<",\"heap_mmapped_after\":"<<heap_after.mmapped
            <<",\"heap_arena_delta\":"<<(heap_after.arena>=heap_before.arena?heap_after.arena-heap_before.arena:0)
            <<",\"wrap_requested_bytes\":"<<w_requested<<",\"wrap_usable_total_bytes\":"<<w_usable_total
            <<",\"wrap_live_bytes\":"<<w_live<<",\"wrap_peak_live_bytes\":"<<w_peak
            <<",\"wrap_allocations\":"<<w_allocations<<",\"wrap_frees\":"<<w_frees
            <<",\"vm_hwm_kb\":"<<vm_after.hwm_kb<<",\"vm_rss_kb\":"<<vm_after.rss_kb
            <<",\"vm_before_hwm_kb\":"<<vm_before.hwm_kb<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

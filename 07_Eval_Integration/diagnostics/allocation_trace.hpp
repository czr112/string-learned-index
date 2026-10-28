#pragma once
#include <cstddef>
#include <cstdint>

namespace allocation_trace {
enum Domain:unsigned {off=0,input=1,index=2,query=3,scan=4,test=5,domain_count=6};
enum Kind:unsigned {c_malloc,c_calloc,c_realloc,cpp_new,cpp_array,cpp_aligned,cpp_aligned_array,kind_count};
struct Totals {
    std::uint64_t requested=0,usable=0,objects=0,allocations=0,frees=0;
    std::uint64_t total_requested=0,total_freed=0,total_usable=0,freed_usable=0;
    std::uint64_t peak_requested=0,peak_usable=0,failed=0;
    std::uint64_t kinds[kind_count]{};
};
struct Snapshot {
    Totals domains[domain_count]{};
    std::uint64_t phase_peak_requested=0,phase_peak_usable=0,registry_bytes=0,registry_live=0;
};
#if BM_ALLOCATION_TRACE
void begin(Domain domain,bool retain_input=false)noexcept;
void end()noexcept;
Snapshot snapshot()noexcept;
void reset_test()noexcept;
#else
inline void begin(Domain,bool=false)noexcept{}
inline void end()noexcept{}
inline Snapshot snapshot()noexcept{return {};}
inline void reset_test()noexcept{}
#endif
}

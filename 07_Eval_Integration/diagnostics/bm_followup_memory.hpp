#pragma once
// Single-threaded diagnostic only. Counts C++ requested bytes, excluding this header.
// Epoch tags prevent frees of pre-existing inputs/oracles from corrupting the ledger.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#endif

namespace bm_followup_memory {
struct Header {void* raw;std::size_t bytes;std::uint64_t epoch;};
inline bool tracking=false,armed=false;
inline std::uint64_t epoch=0,requests=0,calls=0,throw_at=0;
inline std::size_t live=0,peak=0;
inline void begin() {
    if(live||tracking||armed)std::abort();
    ++epoch;requests=0;peak=0;tracking=true;
}
inline void arm(std::uint64_t ordinal) {calls=0;throw_at=ordinal;armed=true;}
inline std::uint64_t disarm() {armed=false;return calls;}
inline void* allocate(std::size_t n,std::size_t alignment) {
    if(armed&&++calls==throw_at)throw std::bad_alloc();
    alignment=std::max(alignment,alignof(Header));
    if(n>std::numeric_limits<std::size_t>::max()-alignment-sizeof(Header))throw std::bad_alloc();
    const auto total=(n?n:1)+alignment+sizeof(Header);
    auto* raw=std::malloc(total);
    if(!raw)throw std::bad_alloc();
    const auto address=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(Header)+alignment-1)&~(alignment-1);
    auto* header=reinterpret_cast<Header*>(address)-1;
    ::new (static_cast<void*>(header)) Header{raw,n,tracking?epoch:0};
    if(tracking){live+=n;peak=std::max(peak,live);++requests;}
#ifdef __SANITIZE_ADDRESS__
    // The accounting wrapper must not hide a user-buffer overrun inside its padding.
    __asan_poison_memory_region(raw,total);
    __asan_unpoison_memory_region(reinterpret_cast<void*>(address),n?n:1);
#endif
    return reinterpret_cast<void*>(address);
}
inline void release(void* p)noexcept {
    if(!p)return;
    auto* header=static_cast<Header*>(p)-1;
#ifdef __SANITIZE_ADDRESS__
    __asan_unpoison_memory_region(header,sizeof(Header));
#endif
    if(header->epoch&&header->epoch==epoch) {
        if(header->bytes>live)std::abort();
        live-=header->bytes;
    }
    std::free(header->raw);
}
} // namespace bm_followup_memory

void* operator new(std::size_t n){return bm_followup_memory::allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,std::align_val_t a){return bm_followup_memory::allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p)noexcept{bm_followup_memory::release(p);}
void operator delete[](void* p)noexcept{bm_followup_memory::release(p);}
void operator delete(void* p,std::size_t)noexcept{bm_followup_memory::release(p);}
void operator delete[](void* p,std::size_t)noexcept{bm_followup_memory::release(p);}
void operator delete(void* p,std::align_val_t)noexcept{bm_followup_memory::release(p);}
void operator delete[](void* p,std::align_val_t)noexcept{bm_followup_memory::release(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{bm_followup_memory::release(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{bm_followup_memory::release(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{bm_followup_memory::release(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{bm_followup_memory::release(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{bm_followup_memory::release(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{bm_followup_memory::release(p);}

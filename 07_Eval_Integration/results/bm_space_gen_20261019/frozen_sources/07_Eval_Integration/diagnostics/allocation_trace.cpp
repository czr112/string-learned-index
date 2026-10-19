// Measurement-only glibc interposition. No heap storage, dlsym or streams in this file.
#include "allocation_trace.hpp"
#include <cerrno>
#include <cstdlib>
#include <malloc.h>
#include <new>
#include <pthread.h>
#include <unistd.h>

extern "C" {
void* __libc_malloc(std::size_t)noexcept;
void* __libc_calloc(std::size_t,std::size_t)noexcept;
void* __libc_realloc(void*,std::size_t)noexcept;
void __libc_free(void*)noexcept;
void* __libc_memalign(std::size_t,std::size_t)noexcept;
}
namespace allocation_trace {
namespace {
struct Entry {void* p;std::uint64_t requested,usable;unsigned domain;};
constexpr std::size_t count=1u<<20;
Entry entries[count]{};
Totals totals[domain_count]{};
pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
unsigned active=0;
bool retain=false;
std::uint64_t peak_requested=0,peak_usable=0,live=0;
void* const tombstone=reinterpret_cast<void*>(std::uintptr_t(1));
[[noreturn]] void fatal()noexcept {
    constexpr char message[]="allocation tracker invariant/registry overflow\n";
    ::write(2,message,sizeof(message)-1);_exit(87);
}
std::size_t bucket(void* p)noexcept {
    auto x=reinterpret_cast<std::uintptr_t>(p)>>4;
    x^=x>>30;x*=UINT64_C(0xbf58476d1ce4e5b9);x^=x>>27;
    return x&(count-1);
}
std::size_t find(void* p)noexcept {
    auto at=bucket(p);
    for(std::size_t n=0;n<count;++n,at=(at+1)&(count-1)) {
        if(entries[at].p==p)return at;
        if(!entries[at].p)return count;
    }
    return count;
}
void update_peak()noexcept {
    if(!active)return;
    std::uint64_t req=0,use=0;
    for(unsigned d=index;d<=scan;++d){req+=totals[d].requested;use+=totals[d].usable;}
    if(retain){req+=totals[input].requested;use+=totals[input].usable;}
    // The fixture domain is isolated from production measurements.
    req+=totals[test].requested;use+=totals[test].usable;
    if(req>peak_requested)peak_requested=req;if(use>peak_usable)peak_usable=use;
}
unsigned remove(void* p)noexcept {
    if(!p)return 0;
    const auto at=find(p);if(at==count)return 0;
    const auto old=entries[at];auto& t=totals[old.domain];
    if(t.requested<old.requested||t.usable<old.usable||!t.objects||!live)fatal();
    t.requested-=old.requested;t.usable-=old.usable;--t.objects;++t.frees;
    t.total_freed+=old.requested;t.freed_usable+=old.usable;
    entries[at].p=tombstone;--live;return old.domain;
}
void add(void* p,std::size_t n,Kind kind,unsigned owner=0)noexcept {
    if(!p)return;
    const auto domain=owner?owner:active;if(!domain)return;
    if(domain>=domain_count||live>=count/2)fatal();
    auto at=bucket(p),first_tomb=count;
    for(std::size_t probes=0;probes<count;++probes,at=(at+1)&(count-1)) {
        if(entries[at].p==p)fatal();
        if(entries[at].p==tombstone&&first_tomb==count)first_tomb=at;
        if(!entries[at].p) {
            if(first_tomb!=count)at=first_tomb;
            const auto usable=malloc_usable_size(p);if(usable<n)fatal();
            entries[at]={p,n,usable,domain};++live;
            auto& t=totals[domain];t.requested+=n;t.usable+=usable;++t.objects;++t.allocations;
            t.total_requested+=n;t.total_usable+=usable;++t.kinds[kind];
            if(t.requested>t.peak_requested)t.peak_requested=t.requested;
            if(t.usable>t.peak_usable)t.peak_usable=t.usable;
            update_peak();return;
        }
    }
    fatal();
}
void failed()noexcept {if(active)++totals[active].failed;}
}
void begin(Domain d,bool keep_input)noexcept {
    pthread_mutex_lock(&lock);active=d;retain=keep_input;peak_requested=peak_usable=0;update_peak();pthread_mutex_unlock(&lock);
}
void end()noexcept {pthread_mutex_lock(&lock);active=0;pthread_mutex_unlock(&lock);}
Snapshot snapshot()noexcept {
    pthread_mutex_lock(&lock);Snapshot result;
    for(unsigned d=0;d<domain_count;++d)result.domains[d]=totals[d];
    result.phase_peak_requested=peak_requested;result.phase_peak_usable=peak_usable;
    result.registry_bytes=sizeof(entries);result.registry_live=live;
    pthread_mutex_unlock(&lock);return result;
}
void reset_test()noexcept {
    pthread_mutex_lock(&lock);if(totals[test].objects)fatal();totals[test]={};pthread_mutex_unlock(&lock);
}
void* allocate(std::size_t n,Kind kind,std::size_t alignment=0)noexcept {
    void* p=alignment?__libc_memalign(alignment,n?n:1):__libc_malloc(n?n:1);
    const auto saved=errno;pthread_mutex_lock(&lock);if(p)add(p,n,kind);else failed();pthread_mutex_unlock(&lock);errno=saved;return p;
}
void release(void* p)noexcept {
    if(!p)return;const auto saved=errno;
    pthread_mutex_lock(&lock);remove(p);pthread_mutex_unlock(&lock);__libc_free(p);errno=saved;
}
}
extern "C" void* malloc(std::size_t n)noexcept {
    void* p=__libc_malloc(n);const auto saved=errno;
    pthread_mutex_lock(&allocation_trace::lock);
    if(p)allocation_trace::add(p,n,allocation_trace::c_malloc);else allocation_trace::failed();
    pthread_mutex_unlock(&allocation_trace::lock);errno=saved;return p;
}
extern "C" void* calloc(std::size_t a,std::size_t b)noexcept {
    void* p=__libc_calloc(a,b);const auto saved=errno;
    pthread_mutex_lock(&allocation_trace::lock);
    if(p){if(b&&a>SIZE_MAX/b)allocation_trace::fatal();allocation_trace::add(p,a*b,allocation_trace::c_calloc);}
    else allocation_trace::failed();
    pthread_mutex_unlock(&allocation_trace::lock);errno=saved;return p;
}
extern "C" void* realloc(void* old,std::size_t n)noexcept {
    // Keep bookkeeping locked while realloc may move the pointer; its backend bypasses these hooks.
    pthread_mutex_lock(&allocation_trace::lock);
    const auto at=old?allocation_trace::find(old):allocation_trace::count;
    const auto owner=at==allocation_trace::count?0:allocation_trace::entries[at].domain;
    void* p=__libc_realloc(old,n);const auto saved=errno;
    if(p||!n) {
        allocation_trace::remove(old);
        if(p)allocation_trace::add(p,n,allocation_trace::c_realloc,owner);
    }else allocation_trace::failed();
    pthread_mutex_unlock(&allocation_trace::lock);errno=saved;return p;
}
extern "C" void free(void* p)noexcept {allocation_trace::release(p);}

static void* cpp_allocate(std::size_t n,allocation_trace::Kind kind,std::size_t alignment=0) {
    for(;;) {
        if(auto p=allocation_trace::allocate(n,kind,alignment))return p;
        if(auto h=std::get_new_handler())h();else throw std::bad_alloc();
    }
}
void* operator new(std::size_t n){return cpp_allocate(n,allocation_trace::cpp_new);}
void* operator new[](std::size_t n){return cpp_allocate(n,allocation_trace::cpp_array);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void* operator new(std::size_t n,std::align_val_t a){return cpp_allocate(n,allocation_trace::cpp_aligned,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return cpp_allocate(n,allocation_trace::cpp_aligned_array,std::size_t(a));}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p)noexcept{allocation_trace::release(p);}
void operator delete[](void* p)noexcept{allocation_trace::release(p);}
void operator delete(void* p,std::size_t)noexcept{allocation_trace::release(p);}
void operator delete[](void* p,std::size_t)noexcept{allocation_trace::release(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{allocation_trace::release(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{allocation_trace::release(p);}
void operator delete(void* p,std::align_val_t)noexcept{allocation_trace::release(p);}
void operator delete[](void* p,std::align_val_t)noexcept{allocation_trace::release(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{allocation_trace::release(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{allocation_trace::release(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{allocation_trace::release(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{allocation_trace::release(p);}

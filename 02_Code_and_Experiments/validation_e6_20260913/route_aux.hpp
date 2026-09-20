#pragma once
// E6: auxiliary outer-route variants built on top of the unchanged baseline router.
// All variants must answer locate(key) == Index::locate(key) for every key; the
// equivalence is checked per query in check_router.cpp, not assumed.
// Key representation fact (reviewed 2026-09-13): the first-8-byte value is built
// BIG-ENDIAN (first byte at the most significant position), zero-padded at the end.
// For unsigned-byte lexicographic order, comparing these values decides the order
// exactly whenever the values differ (padding acts like an implicit 0x00 tail, which
// matches the shorter-string-first rule); equal values fall back to a full compare.
// Length is never a secondary sort key. The query's prefix value is computed once
// per locate() call, never per binary-search step.
#include "index.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace validation {
using RouteClock=std::chrono::steady_clock;
inline double route_ns(RouteClock::time_point a,RouteClock::time_point b){
    return std::chrono::duration<double,std::nano>(b-a).count();
}
// E6-9: execution counters for diagnostic runs only. Compiled out of the timed
// benchmark binary; diag_route.cpp builds with -DE6_ROUTE_COUNTERS.
#ifdef E6_ROUTE_COUNTERS
#define E6_RC(x) (x)+=1
#else
#define E6_RC(x) ((void)0)
#endif

inline std::uint64_t prefix_u64(std::string_view s){
    const std::size_t n=std::min<std::size_t>(8,s.size());
    std::uint64_t u=0;
    for(std::size_t i=0;i<n;++i)u=(u<<8)|static_cast<std::uint64_t>(static_cast<unsigned char>(s[i]));
    if(n<8)u<<=8u*(8u-n);
    return u;
}

struct FenceDiag {                     // how much filtering the first-8 key can do
    std::size_t fences=0,distinct_u=0,max_equal_run=1;
    double equal_share=0;              // fraction of fences whose predecessor shares U
};

// E6 baseline wrapper: the untouched Index::locate, uniform interface for benches.
struct RouterBaseline {
    std::size_t rebuilds=0;double rebuild_ns=0;
    void sync(const Index&){}
    std::size_t metadata_bytes()const{return 0;}
    std::size_t locate(const Index& i,const std::string&key)const{return i.locate(key);}
    FenceDiag diag()const{return {};}
};

// E6-2: baseline binary search, but each step compares one big-endian prefix value;
// equal values fall back to a full compare refined by binary search inside the
// equal-value block. Metadata rebuilds whenever the index structure version moves.
class RouterA {
    const Index* idx=nullptr;
    std::uint64_t version=~0ULL;
    std::vector<std::uint64_t> u_;     // per fence, non-decreasing
public:
    std::size_t rebuilds=0;double rebuild_ns=0;
    std::uint64_t int_cmps=0,str_cmps=0;   // diagnostic counters (E6-9)
    void sync(const Index& i){
        if(idx==&i&&version==i.structure_version())return;
        idx=&i;version=i.structure_version();
        const auto t0=RouteClock::now();
        const std::size_t m=i.region_count();
        u_.resize(m);
        for(std::size_t k=0;k<m;++k)u_[k]=prefix_u64(i.fence(k));
        rebuild_ns+=route_ns(t0,RouteClock::now());++rebuilds;
    }
    std::size_t metadata_bytes()const{return u_.capacity()*sizeof(std::uint64_t);}
    std::size_t locate(const Index& i,const std::string&key){
        sync(i);
        const std::uint64_t uk=prefix_u64(key);
        // hand-rolled binary searches (same algorithms as lower_bound/upper_bound)
        // so integer comparisons are countable in diagnostic builds.
        std::size_t lo=0,hi=u_.size();                 // first index with u >= uk
        while(lo<hi){const auto mid=lo+(hi-lo)/2;E6_RC(int_cmps);
            if(u_[mid]<uk)lo=mid+1;else hi=mid;}
        const std::size_t c1=lo;                       // fences with U < U_k: all <= key
        lo=c1;hi=u_.size();                            // first index with u > uk
        while(lo<hi){const auto mid=lo+(hi-lo)/2;E6_RC(int_cmps);
            if(!(u_[mid]>uk))lo=mid+1;else hi=mid;}
        const std::size_t c2end=lo;                    // equal-U block = [c1, c2end)
        lo=c1;hi=c2end;                                // fences in block are sorted
        while(lo<hi){const auto mid=lo+(hi-lo)/2;E6_RC(str_cmps);
            if(compare(i.fence(mid),key)<=0)lo=mid+1;else hi=mid;}
        const std::size_t total=lo;                    // fences <= key
        return total?total-1:0;
    }
    FenceDiag diag()const{
        FenceDiag d;d.fences=u_.size();
        std::size_t eq=0;
        for(std::size_t k=0;k<u_.size();++k){
            if(k==0||u_[k]!=u_[k-1])++d.distinct_u;
            else{++eq;d.max_equal_run=0;}      // marker: at least one adjacency
        }
        // second pass for the longest equal run (max_equal_run reused as counter)
        std::size_t run=1;d.max_equal_run=u_.size()?1:0;
        for(std::size_t k=1;k<u_.size();++k){
            if(u_[k]==u_[k-1]){++run;d.max_equal_run=std::max(d.max_equal_run,run);}
            else run=1;
        }
        (void)eq;
        d.equal_share=u_.size()?static_cast<double>(u_.size()-d.distinct_u)/u_.size():0.0;
        return d;
    }
};

// E6-3: same prefix metadata arranged in Eytzinger (level-order) layout with the
// standard in-order construction; descent per level with the same equal-value
// fallback. Layout benefit is an empirical question, not an assumption.
class RouterB {
    const Index* idx=nullptr;
    std::uint64_t version=~0ULL;
    std::size_t n=0;                                   // fence count
    std::vector<std::uint64_t> ue_;                    // 1-based Eytzinger positions
    std::vector<std::uint32_t> orig_;                  // Eytzinger pos -> fence index
    void build(std::size_t eyt,std::size_t&j){
        if(eyt>n)return;
        build(2*eyt,j);
        ue_[eyt]=prefix_u64(idx->fence(j));orig_[eyt]=static_cast<std::uint32_t>(j);++j;
        build(2*eyt+1,j);
    }
public:
    std::size_t rebuilds=0;double rebuild_ns=0;
    std::uint64_t int_cmps=0,str_cmps=0;   // diagnostic counters (E6-9)
    void sync(const Index& i){
        if(idx==&i&&version==i.structure_version())return;
        idx=&i;version=i.structure_version();
        const auto t0=RouteClock::now();
        n=i.region_count();
        ue_.assign(n+1,0);orig_.assign(n+1,0);
        std::size_t j=0;build(1,j);
        rebuild_ns+=route_ns(t0,RouteClock::now());++rebuilds;
    }
    std::size_t metadata_bytes()const{return (ue_.capacity()+orig_.capacity())*sizeof(std::uint64_t);}
    std::size_t locate(const Index& i,const std::string&key){
        sync(i);
        const std::uint64_t uk=prefix_u64(key);
        std::size_t pos=1,best=n+1;                    // best = first fence >= key (Eytzinger pos)
        while(pos<=n){
            const std::uint64_t u=ue_[pos];
            E6_RC(int_cmps);
            const bool ge=u>uk||(u==uk&&(E6_RC(str_cmps),compare(key,i.fence(orig_[pos]))<=0));
            if(ge){best=pos;pos=2*pos;}else pos=2*pos+1;
        }
        if(best>n)return n-1;                          // every fence < key: last region
        const std::size_t ofo=orig_[best];
        return compare(i.fence(ofo),key)==0?ofo:(ofo?ofo-1:0);
    }
    FenceDiag diag()const{
        FenceDiag d;d.fences=n;d.distinct_u=0;
        std::vector<std::uint64_t> tmp;tmp.reserve(n);
        for(std::size_t k=0;k<n;++k)tmp.push_back(prefix_u64(idx->fence(k)));
        std::sort(tmp.begin(),tmp.end());
        for(std::size_t k=0;k<tmp.size();++k)if(k==0||tmp[k]!=tmp[k-1])++d.distinct_u;
        return d;
    }
};
}

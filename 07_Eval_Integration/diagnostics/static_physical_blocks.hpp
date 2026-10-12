#pragma once
// Experimental owning, immutable physical-block index. No production Index changes.
// The model certificate covers coordinate collisions AND gaps, including absent keys.
#include "index.hpp" // existing unsigned-byte comparison and CRC implementation only
#include "index_contract.h"
#include <optional>
#include <limits>
#include <cmath>

namespace static_blocks {
using Row = string_index_bench::v1::Row;
using validation::compare;

struct Entry { std::uint32_t offset=0, length=0; std::uint64_t value=0; };
struct alignas(16) Block {
    alignas(16) std::uint8_t tags[16]{};
    std::uint32_t count=0;
    Entry entries[16]{};
};
static_assert(sizeof(Entry)==16 && alignof(Block)>=16, "unexpected geometry");
struct Run { std::uint64_t x; std::uint32_t first, last; };
struct Segment {
    std::uint64_t xmin=0, xmax=0;
    double slope=0;
    std::uint32_t anchor=0, lower=0, upper=0;
    std::int32_t error_low=0, error_high=0;
    std::uint32_t predict(std::uint64_t x) const {
        const double dx=x>=xmin ? double(x-xmin) : -double(xmin-x);
        double y=double(anchor)+slope*dx;
        if (!(y>double(lower))) return lower;
        if (y>=double(upper)) return upper;
        return static_cast<std::uint32_t>(std::floor(y));
    }
};
struct Window { std::uint32_t lo=0, hi=0; }; // inclusive block IDs local to region
struct Region {
    std::uint32_t first=0, count=0;
    std::string prefix;
    std::vector<Segment> segments;
};
struct Work {
    std::uint64_t queries=0, rejected=0, model_steps=0, window_blocks=0;
    std::uint64_t blocks=0, exact=0, wide=0, directory_steps=0;
    std::vector<std::size_t> windows, entered;
};
struct Memory { std::size_t object=0, arena=0, blocks=0, regions=0, prefixes=0, fences=0, model=0, routing=0;
    std::size_t total() const {return object+arena+blocks+regions+prefixes+fences+model+routing;}
};

inline std::uint64_t coordinate(std::string_view suffix) {
    std::uint64_t x=0;
    for (unsigned i=0;i<7;++i)
        x=(x<<9)|(i<suffix.size()?std::uint64_t(static_cast<unsigned char>(suffix[i]))+1:0);
    return x;
}
inline std::uint64_t digest8(std::string_view s) {
    std::uint64_t x=0;
    for(unsigned i=0;i<8;++i) x=(x<<8)|(i<s.size()?static_cast<unsigned char>(s[i]):0);
    return x;
}
inline std::size_t common_prefix(std::string_view a,std::string_view b) {
    std::size_t i=0;while(i<std::min(a.size(),b.size())&&a[i]==b[i])++i;return i;
}
// Count string heap buffers without double-counting SSO within the object.
inline std::size_t string_heap(const std::string& s) {
    auto p=reinterpret_cast<std::uintptr_t>(s.data());
    auto b=reinterpret_cast<std::uintptr_t>(&s);
    return p>=b&&p<b+sizeof(s)?0:s.capacity()+1;
}

class Index {
    std::vector<char> arena_;
    std::vector<Block> blocks_;
    std::vector<Region> regions_;
    std::vector<std::string> fences_;
    std::vector<std::uint64_t> digests_;
    std::size_t fence_prefix_=0, size_=0;

    std::string_view suffix(const Entry& e) const {
        // Keep a valid address for a zero-byte arena, e.g. a singleton empty key.
        return std::string_view(arena_.empty()?"":arena_.data()+e.offset,e.length);
    }
    std::string_view boundary(const Region& r,std::size_t block) const {
        return suffix(blocks_[r.first+block].entries[0]);
    }
    static Segment certificate(const std::vector<Run>& runs,std::size_t begin,std::size_t end) {
        Segment s;
        s.xmin=runs[begin].x;s.xmax=runs[end].x;s.anchor=runs[begin].first;
        s.lower=begin?runs[begin-1].last:0;s.upper=runs[end].last;
        s.slope=s.xmin==s.xmax?0:double(s.upper-s.anchor)/double(s.xmax-s.xmin);
        s.error_low=std::numeric_limits<std::int32_t>::max();
        s.error_high=std::numeric_limits<std::int32_t>::min();
        const auto record=[&](std::uint64_t x,std::uint32_t lo,std::uint32_t hi) {
            const auto p=static_cast<std::int32_t>(s.predict(x));
            s.error_low=std::min(s.error_low,static_cast<std::int32_t>(lo)-p);
            s.error_high=std::max(s.error_high,static_cast<std::int32_t>(hi)-p);
        };
        for(std::size_t i=begin;i<=end;++i) {
            // q with the same coordinate can precede the first fence sharing x.
            record(runs[i].x,runs[i].first?runs[i].first-1:0,runs[i].last);
            const std::uint64_t gap_lo=i?runs[i-1].x+1:0;
            if(gap_lo<runs[i].x) {
                const std::uint32_t predecessor=i?runs[i-1].last:0;
                record(gap_lo,predecessor,predecessor);
                record(runs[i].x-1,predecessor,predecessor);
            }
        }
        return s;
    }
    void train(Region& r) {
        std::vector<Run> runs;
        for(std::uint32_t i=0;i<r.count;++i) {
            const auto x=coordinate(boundary(r,i));
            if(!runs.empty()&&x<runs.back().x) throw std::logic_error("coordinate order");
            if(!runs.empty()&&x==runs.back().x) runs.back().last=i;
            else runs.push_back({x,i,i});
        }
        for(std::size_t begin=0;begin<runs.size();) {
            std::size_t end=begin;
            auto best=certificate(runs,begin,end);
            // A collision run wider than the target retains its full certificate.
            if(best.error_high-best.error_low+1<=3) {
                while(end+1<runs.size()) {
                    auto next=certificate(runs,begin,end+1);
                    if(next.error_high-next.error_low+1>3) break;
                    best=next;++end;
                }
            }
            r.segments.push_back(best);begin=end+1;
        }
    }
    Window model_window_x(const Region& r,std::uint64_t x,Work* work) const {
        if(x>r.segments.back().xmax) return {r.count-1,r.count-1};
        std::size_t lo=0,hi=r.segments.size();
        while(lo<hi) {
            if(work)++work->model_steps;
            const auto mid=lo+(hi-lo)/2;
            if(r.segments[mid].xmax<x)lo=mid+1;else hi=mid;
        }
        const auto& segment=r.segments[lo];
        const auto p=static_cast<std::int64_t>(segment.predict(x));
        return {static_cast<std::uint32_t>(std::max<std::int64_t>(0,p+segment.error_low)),
                static_cast<std::uint32_t>(std::min<std::int64_t>(r.count-1,p+segment.error_high))};
    }
    Window model_window(const Region& r,std::string_view s,Work* work) const {
        return model_window_x(r,coordinate(s),work);
    }
    std::uint32_t directory_block(const Region& r,std::string_view s,Work* work) const {
        std::uint32_t lo=0,hi=r.count;
        while(lo<hi) {
            if(work)++work->directory_steps;
            const auto mid=lo+(hi-lo)/2;
            if(compare(boundary(r,mid),s)<=0)lo=mid+1;else hi=mid;
        }
        return lo?lo-1:0;
    }
    template<bool Learned>
    std::optional<std::uint64_t> find_impl(std::string_view key,Work* work) const {
        if(work)++work->queries;
        if(regions_.empty())return {};
        const auto& r=regions_[locate(key)];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0) {
            if(work)++work->rejected;
            return {};
        }
        const auto s=key.substr(r.prefix.size());
        Window w;
        if constexpr(Learned)w=model_window(r,s,work);
        else {const auto b=directory_block(r,s,work);w={b,b};}
        if(w.lo>w.hi||w.hi>=r.count)throw std::logic_error("invalid certificate");
        if(work) {work->window_blocks+=w.hi-w.lo+1;work->windows.push_back(w.hi-w.lo+1);
            work->wide+=(w.hi-w.lo+1)>3;}
        const auto tag=validation::Fingerprint::of(s);
        std::size_t visited=0;
        for(std::uint32_t b=w.lo;b<=w.hi;++b) {
            const auto& block=blocks_[r.first+b];++visited;
            const auto bytes=_mm_load_si128(reinterpret_cast<const __m128i*>(block.tags));
            std::uint32_t mask=static_cast<std::uint32_t>(_mm_movemask_epi8(
                _mm_cmpeq_epi8(bytes,_mm_set1_epi8(static_cast<char>(tag)))));
            mask&=(1u<<block.count)-1;
            while(mask) {
                const auto& e=block.entries[__builtin_ctz(mask)];mask&=mask-1;
                if(e.length!=s.size())continue;
                if(work)++work->exact;
                if(compare(suffix(e),s)==0) {
                    if(work){work->blocks+=visited;work->entered.push_back(visited);}
                    return e.value;
                }
            }
        }
        if(work){work->blocks+=visited;work->entered.push_back(visited);}return {};
    }
public:
    explicit Index(const std::vector<Row>& rows,std::size_t region_keys=2048) {
        if(!region_keys||region_keys>65536)throw std::invalid_argument("region size");
        for(std::size_t i=1;i<rows.size();++i)
            if(compare(rows[i-1].key,rows[i].key)>=0)throw std::invalid_argument("sorted unique rows required");
        size_=rows.size();
        const auto nr=(rows.size()+region_keys-1)/region_keys;
        regions_.reserve(nr);fences_.reserve(nr);digests_.reserve(nr);
        std::size_t bytes=0,nb=0;
        for(std::size_t first=0;first<rows.size();first+=region_keys) {
            const auto end=std::min(first+region_keys,rows.size());
            const auto prefix=common_prefix(rows[first].key,rows[end-1].key);
            for(auto i=first;i<end;++i)bytes+=rows[i].key.size()-prefix;
            nb+=(end-first+15)/16;
        }
        if(bytes>UINT32_MAX||nb>UINT32_MAX)throw std::length_error("prototype 32-bit arena/block limit");
        arena_.reserve(bytes);blocks_.reserve(nb);
        for(std::size_t first=0;first<rows.size();first+=region_keys) {
            const auto end=std::min(first+region_keys,rows.size());
            Region r;r.first=static_cast<std::uint32_t>(blocks_.size());
            r.prefix=rows[first].key.substr(0,common_prefix(rows[first].key,rows[end-1].key));
            fences_.push_back(rows[first].key);
            for(std::size_t i=first;i<end;) {
                blocks_.emplace_back();auto& b=blocks_.back();
                while(i<end&&b.count<16) {
                    const auto s=std::string_view(rows[i].key).substr(r.prefix.size());
                    if(s.size()>UINT32_MAX)throw std::length_error("prototype key limit");
                    auto& e=b.entries[b.count];e.offset=static_cast<std::uint32_t>(arena_.size());
                    e.length=static_cast<std::uint32_t>(s.size());e.value=rows[i].value;
                    b.tags[b.count]=validation::Fingerprint::of(s);
                    arena_.insert(arena_.end(),s.begin(),s.end());++b.count;++i;
                }
            }
            r.count=static_cast<std::uint32_t>(blocks_.size())-r.first;
            train(r);regions_.push_back(std::move(r));
        }
        if(!fences_.empty())fence_prefix_=common_prefix(fences_.front(),fences_.back());
        for(const auto& f:fences_)digests_.push_back(digest8(std::string_view(f).substr(fence_prefix_)));
    }
    std::size_t locate(std::string_view key) const {
        if(regions_.empty())return 0;
        const auto pref=key.substr(0,std::min(key.size(),fence_prefix_));
        const int pc=compare(pref,std::string_view(fences_.front()).substr(0,fence_prefix_));
        if(pc<0)return 0;if(pc>0)return regions_.size()-1;
        const auto d=digest8(key.substr(fence_prefix_));
        std::size_t lo=0,hi=digests_.size();
        while(lo<hi) {
            const auto mid=lo+(hi-lo)/2;
            const bool le=digests_[mid]<d||(digests_[mid]==d&&compare(fences_[mid],key)<=0);
            if(le)lo=mid+1;else hi=mid;
        }
        return lo?lo-1:0;
    }
    // Independent byte-fence reference checks every prediction, not just successful hits.
    void check_window(std::string_view key) const {
        if(regions_.empty())return;
        const auto& r=regions_[locate(key)];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0)return;
        const auto s=key.substr(r.prefix.size());const auto w=model_window(r,s,nullptr);
        const auto expected=directory_block(r,s,nullptr);
        if(w.lo>expected||w.hi<expected||w.hi>=r.count)throw std::logic_error("certificate excludes byte-fence reference");
    }
    std::optional<std::uint64_t> find_model(std::string_view k,Work* w=nullptr)const{return find_impl<true>(k,w);}
    std::optional<std::uint64_t> find_directory(std::string_view k,Work* w=nullptr)const{return find_impl<false>(k,w);}
    std::size_t size()const{return size_;}
    std::size_t segment_count()const {std::size_t n=0;for(const auto& r:regions_)n+=r.segments.size();return n;}
    std::size_t block_count()const{return blocks_.size();}
    Memory memory()const {
        Memory m;m.object=sizeof(*this);m.arena=arena_.capacity();m.blocks=blocks_.capacity()*sizeof(Block);
        m.regions=regions_.capacity()*sizeof(Region);m.fences=fences_.capacity()*sizeof(std::string);
        m.routing=digests_.capacity()*sizeof(std::uint64_t);
        for(const auto& r:regions_){m.prefixes+=string_heap(r.prefix);m.model+=r.segments.capacity()*sizeof(Segment);}
        for(const auto& f:fences_)m.fences+=string_heap(f);return m;
    }
    std::size_t verify_numeric_certificate()const {
        std::size_t checked=0;
        for(const auto& r:regions_) {
            std::vector<std::uint64_t> xs,probes{0,UINT64_C(0x7fffffffffffffff)};
            for(std::uint32_t b=0;b<r.count;++b)xs.push_back(coordinate(boundary(r,b)));
            for(auto x:xs){probes.push_back(x);if(x)probes.push_back(x-1);if(x<UINT64_C(0x7fffffffffffffff))probes.push_back(x+1);}
            for(auto x:probes) {
                const auto first=static_cast<std::uint32_t>(std::lower_bound(xs.begin(),xs.end(),x)-xs.begin());
                const auto last=static_cast<std::uint32_t>(std::upper_bound(xs.begin(),xs.end(),x)-xs.begin());
                const auto lo=first?first-1:0,hi=last?last-1:0;
                const auto w=model_window_x(r,x,nullptr);
                if(w.lo>lo||w.hi<hi||w.hi>=r.count)throw std::logic_error("numeric interval/collision certificate mismatch");
                ++checked;
            }
        }
        return checked;
    }
};
} // namespace static_blocks

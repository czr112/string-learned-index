#pragma once
// D1 experimental owning core: one local-prefix refinement level. B/S3 unchanged.
#include "collision_refinement.hpp"

namespace collision_refined {
using static_physical::Row;
using static_physical::Entry;
using static_physical::Block;
using static_physical::Run;
using static_physical::Segment;
using static_physical::Window;
using static_physical::Region;
using collision_refinement::Work;
using static_physical::Memory;
using static_physical::Lookup;
using static_physical::compare;
using static_physical::coordinate;
using static_physical::digest8;
using static_physical::common_prefix;
using static_physical::string_heap;
using static_physical::fingerprint;

enum class LocationState { empty, prefix_rejected, candidates, exact_predecessor };
struct Location {
    LocationState state=LocationState::empty;
    std::size_t region=SIZE_MAX;
    std::uint32_t begin=0, end=0; // region-local half-open block range
    bool prefix_verified=false;
};
struct BuildIdentity {
    std::string_view layout="s3-static16-v1", encoding="suffix-7x9-v1";
    std::string_view model="s3-collision-gap-segments-v1";
    std::size_t region_keys;
    std::uint32_t block_entries=16, coordinate_symbols=7, certificate_target=3;
};
class Index {
    std::vector<char> arena_;
    std::vector<Block> blocks_;
    std::vector<Region> regions_;
    std::vector<std::string> fences_;
    std::vector<std::uint64_t> digests_;
    std::size_t fence_prefix_=0, size_=0, region_keys_=0;
    collision_refinement::Table refinement_;

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
    std::uint32_t directory_block(const Region& r,std::string_view s,Work* work,
                                  Window range) const {
        std::uint32_t lo=range.lo,hi=range.hi+1;
        while(lo<hi) {
            if(work)++work->directory_steps;
            const auto mid=lo+(hi-lo)/2;
            if(compare(boundary(r,mid),s)<=0)lo=mid+1;else hi=mid;
        }
        return lo>range.lo?lo-1:range.lo;
    }
    std::uint32_t directory_block(const Region& r,std::string_view s,Work* work) const {
        return directory_block(r,s,work,{0,r.count-1});
    }
    // Both strategies enter the same complete-key verifier. Locations are produced
    // and consumed in one query; callers cannot submit an arbitrary saved range.
    std::optional<std::uint64_t> verify_range(std::string_view key,const Location& range,Work* work) const {
        if(range.state==LocationState::empty||range.state==LocationState::prefix_rejected)return {};
        const auto& r=regions_[range.region];
        if(!range.prefix_verified||range.begin>=range.end||range.end>r.count)
            throw std::logic_error("invalid physical range");
        const auto s=key.substr(r.prefix.size());
        const auto tag=fingerprint(s);
        std::size_t visited=0;
        for(std::uint32_t b=range.begin;b<range.end;++b) {
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
    template<Lookup Policy>
    std::optional<std::uint64_t> find_impl(std::string_view key,Work* work) const {
        if(work)++work->queries;
        return verify_range(key,locate_blocks<Policy>(key,work),work);
    }
public:
    template<class InputRow> explicit Index(const std::vector<InputRow>& rows,std::size_t region_keys=2048) {
        if(!region_keys||region_keys>65536)throw std::invalid_argument("region size");
        for(const auto& row:rows)if(row.key.size()>UINT32_MAX)
            throw std::length_error("key exceeds 32-bit implementation limit");
        for(std::size_t i=1;i<rows.size();++i)
            if(compare(rows[i-1].key,rows[i].key)>=0)throw std::invalid_argument("sorted unique rows required");
        size_=rows.size();region_keys_=region_keys;
        const auto nr=rows.size()/region_keys+(rows.size()%region_keys!=0);
        regions_.reserve(nr);fences_.reserve(nr);digests_.reserve(nr);
        std::size_t bytes=0,nb=0;
        for(std::size_t first=0;first<rows.size();first+=region_keys) {
            const auto end=std::min(first+region_keys,rows.size());
            const auto prefix=common_prefix(rows[first].key,rows[end-1].key);
            for(auto i=first;i<end;++i) {
                const auto n=rows[i].key.size()-prefix;
                if(n>UINT32_MAX-bytes)throw std::length_error("32-bit suffix arena limit");
                bytes+=n;
            }
            const auto nblocks=(end-first+15)/16;
            if(nblocks>UINT32_MAX-nb)throw std::length_error("32-bit block limit");
            nb+=nblocks;
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
                    b.tags[b.count]=fingerprint(s);
                    arena_.insert(arena_.end(),s.begin(),s.end());++b.count;++i;
                }
            }
            r.count=static_cast<std::uint32_t>(blocks_.size())-r.first;
            train(r);regions_.push_back(std::move(r));
        }
        if(!fences_.empty())fence_prefix_=common_prefix(fences_.front(),fences_.back());
        for(const auto& f:fences_)digests_.push_back(digest8(std::string_view(f).substr(fence_prefix_)));
        refinement_=collision_refinement::Table(regions_.size(),
            [&](std::size_t ri){return regions_[ri].count;},
            [&](std::size_t ri,std::uint32_t b){return boundary(regions_[ri],b);});
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
    BuildIdentity build_identity()const { return {"s3-static16-v1","suffix-7x9-v1",
        "s3-local-lcp-collision-gap-v1",region_keys_}; }
    // Shape validation is also exposed for diagnostics; it is not a proof that an
    // arbitrary caller-created range belongs to a query. No lookup accepts one.
    void validate_location(const Location& p)const {
        if(p.state==LocationState::empty) {
            if(!regions_.empty()||p.region!=SIZE_MAX||p.begin||p.end||p.prefix_verified)
                throw std::logic_error("invalid empty location");
            return;
        }
        if(p.region>=regions_.size())throw std::logic_error("invalid region identity");
        const auto& r=regions_[p.region];
        if(r.first>blocks_.size()||r.count>blocks_.size()-r.first)
            throw std::logic_error("invalid global block bounds");
        if(p.state==LocationState::prefix_rejected) {
            if(p.begin||p.end||p.prefix_verified)throw std::logic_error("invalid rejection");
            return;
        }
        if(p.state!=LocationState::candidates&&p.state!=LocationState::exact_predecessor)
            throw std::logic_error("unknown location state");
        if(!p.prefix_verified||p.begin>=p.end||p.end>r.count)
            throw std::logic_error("invalid physical range");
        if(p.state==LocationState::exact_predecessor&&p.end-p.begin!=1)
            throw std::logic_error("non-singleton exact location");
    }
    template<Lookup Policy> Location locate_blocks(std::string_view key,Work* work=nullptr)const {
        if(regions_.empty())return {};
        const auto ri=locate(key);const auto& r=regions_[ri];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0) {
            if(work)++work->rejected;
            return {LocationState::prefix_rejected,ri,0,0,false};
        }
        const auto s=key.substr(r.prefix.size());
        Window w;
        if constexpr(Policy==Lookup::directory) {
            const auto b=directory_block(r,s,work);w={b,b};
        } else {
            const auto x=coordinate(s);const auto local=refinement_.locate(ri,s,x,work);
            if(local.used) {
                w=local.range;
                if(local.correction) {
                    const auto b=directory_block(r,s,work,w);w={b,b};
                    if(work)++work->fallback;
                }
            } else w=model_window_x(r,x,work);
        }
        if(w.lo>w.hi||w.hi>=r.count)throw std::logic_error("invalid certificate");
        if(work) {work->window_blocks+=w.hi-w.lo+1;work->windows.push_back(w.hi-w.lo+1);
            work->wide+=(w.hi-w.lo+1)>3;}
        bool exact=Policy==Lookup::directory;
        if constexpr(Policy==Lookup::wide_directory) {
            if(w.hi-w.lo+1>3) {
                const auto b=directory_block(r,s,work,w);w={b,b};exact=true;
                if(work)++work->fallback;
            }
        }
        // The sole inclusive-to-half-open conversion. Region size <=65536 keys,
        // hence w.hi+1 and r.first+end fit the constructor-checked block bounds.
        return {exact?LocationState::exact_predecessor:LocationState::candidates,
            ri,w.lo,w.hi+1,true};
    }
    // Independent byte-fence reference checks every prediction, not just successful hits.
    void check_window(std::string_view key) const {
        if(regions_.empty())return;
        const auto p=locate_blocks<Lookup::learned>(key);validate_location(p);
        if(p.state==LocationState::prefix_rejected)return;
        const auto& r=regions_[p.region];const auto s=key.substr(r.prefix.size());
        const auto expected=directory_block(r,s,nullptr);
        if(p.begin>expected||p.end<=expected)throw std::logic_error("certificate excludes byte-fence reference");
    }
    std::optional<std::uint64_t> find_model(std::string_view k,Work* w=nullptr)const{return find_impl<Lookup::learned>(k,w);}
    std::optional<std::uint64_t> find_directory(std::string_view k,Work* w=nullptr)const{return find_impl<Lookup::directory>(k,w);}
    std::optional<std::uint64_t> find_hybrid(std::string_view k,Work* w=nullptr)const{return find_impl<Lookup::wide_directory>(k,w);}
    std::vector<Row> scan(std::string_view lower,std::size_t limit)const {
        std::vector<Row> out;
        if(!limit||regions_.empty())return out;
        auto ri=locate(lower);
        const auto full_compare=[&](const Region& r,const Entry& e) {
            // Compare prefix+suffix without allocating a temporary full key.
            const auto n=std::min(lower.size(),r.prefix.size());
            const int c=n?std::memcmp(r.prefix.data(),lower.data(),n):0;
            if(c)return c;
            if(lower.size()<r.prefix.size())return 1;
            return compare(suffix(e),lower.substr(r.prefix.size()));
        };
        const auto& start=regions_[ri];
        std::uint32_t lo=0,hi=start.count;
        while(lo<hi) {
            const auto mid=lo+(hi-lo)/2;
            if(full_compare(start,blocks_[start.first+mid].entries[0])<=0)lo=mid+1;
            else hi=mid;
        }
        std::uint32_t b=lo?lo-1:0;
        bool seek=true;
        for(;ri<regions_.size();++ri,b=0) {
            const auto& r=regions_[ri];
            for(;b<r.count;++b) {
                const auto& block=blocks_[r.first+b];
                for(std::uint32_t i=0;i<block.count;++i) {
                    const auto& e=block.entries[i];
                    if(seek&&full_compare(r,e)<0)continue;
                    seek=false;
                    std::string key=r.prefix;key.append(suffix(e));
                    out.push_back({std::move(key),e.value});
                    if(out.size()==limit)return out;
                }
            }
        }
        return out;
    }
    std::size_t size()const{return size_;}
    std::size_t segment_count()const {std::size_t n=0;for(const auto& r:regions_)n+=r.segments.size();return n;}
    std::size_t block_count()const{return blocks_.size();}
    std::size_t refinement_bytes()const{return refinement_.memory();}
    Memory memory()const {
        Memory m;m.object=sizeof(*this);m.arena=arena_.capacity();m.blocks=blocks_.capacity()*sizeof(Block);
        m.regions=regions_.capacity()*sizeof(Region);m.fences=fences_.capacity()*sizeof(std::string);
        m.routing=digests_.capacity()*sizeof(std::uint64_t);
        for(const auto& r:regions_){m.prefixes+=string_heap(r.prefix);m.model+=r.segments.capacity()*sizeof(Segment);}
        for(const auto& f:fences_)m.fences+=string_heap(f);
        m.model+=refinement_.memory()-sizeof(refinement_);return m;
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
        return checked+refinement_.verify_numeric(
            [&](std::size_t ri,std::uint32_t b){return boundary(regions_[ri],b);});
    }
};
} // namespace collision_refined

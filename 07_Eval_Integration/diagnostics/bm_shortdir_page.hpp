#pragma once
// Read-only candidate page: BM leaf core plus upstream ShortDirectory routing. Unlike the entry
// diagnostic this copy carries no counting replica and no local code array; it keeps one stable
// fence-view vector (counted in memory()) and calls blocklearn::ShortDirectory directly.
#include "static_physical_index.hpp"
#include "block_model.h"
#include "short_directory.h"

namespace bm_sdpage {
using static_physical::Row;
using static_physical::Entry;
using static_physical::Block;
using static_physical::Run;
using static_physical::Segment;
using static_physical::Window;
using static_physical::Region;
struct Work:static_physical::Work {
    std::uint64_t bm_calls=0,bm_raw_blocks=0,bm_proposal_blocks=0,bm_boundary_compares=0,bm_outside=0,bm_local_searches=0;
};
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
class Page {
    struct Fence {std::uint32_t offset,length;};
    std::vector<Fence> stable_fences_;
    std::vector<char> arena_;
    std::vector<Block> blocks_;
    std::vector<Region> regions_;
    std::vector<std::string> fences_;
    std::vector<std::uint64_t> digests_;
    std::size_t fence_prefix_=0, size_=0, region_keys_=0;
    // Its prefix view borrows immutable fence bytes in the reserved arena.
    blocklearn::BlockModel bm_;
    // Candidate routing: upstream short directory plus the stable fence views it needs. Both are
    // owned capacity and reported by short_directory_bytes()/fence_view_bytes().
    std::vector<std::string_view> fence_views_;
    blocklearn::ShortDirectory short_dir_;

    std::string_view suffix(const Entry& e) const {
        // Keep a valid address for a zero-byte arena, e.g. a singleton empty key.
        return std::string_view(arena_.empty()?"":arena_.data()+e.offset,e.length);
    }
    std::string_view boundary(const Region& r,std::size_t block) const {
        const auto& f=stable_fences_[r.first+block];
        return std::string_view(arena_.empty()?"":arena_.data()+f.offset,f.length);
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
    template<bool Correct,bool Directory> Location locate_bm(std::string_view key,Work* work)const {
        if(regions_.empty())return {};
        const auto& r=regions_[0];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0) {
            if(work)++work->rejected;
            return {LocationState::prefix_rejected,0,0,0,false};
        }
        const auto suffix=key.substr(r.prefix.size());
        if constexpr(Directory) {
            const auto b=directory_block(r,suffix,work);
            return {LocationState::exact_predecessor,0,b,b+1,true};
        }else {
            // An upstream training radius is a proposal, not an absent-key certificate.
            if(work)++work->bm_calls;
            const auto proposed=bm_.window(suffix);
            if(proposed.first>=proposed.last||proposed.last>r.count)throw std::logic_error("invalid upstream window");
            const auto lo=std::uint32_t(proposed.first?proposed.first-1:0),hi=std::uint32_t(proposed.last);
            if(work){work->bm_raw_blocks+=proposed.last-proposed.first;work->bm_proposal_blocks+=hi-lo;}
            const auto boundary_compare=[&](std::uint32_t b) {
                if(work)++work->bm_boundary_compares;
                return compare(boundary(r,b),suffix);
            };
            const bool too_left=lo&&boundary_compare(lo)>0;
            const bool too_right=hi<r.count&&boundary_compare(hi)<=0;
            if(too_left||too_right) {
                if(work){++work->bm_outside;++work->fallback;}
                // Search only the proven complementary side; base remains a full-directory control.
                std::uint32_t b;
                if constexpr(Correct)b=directory_block(r,suffix,work,too_left?Window{0,lo-1}:Window{hi,r.count-1});
                else b=directory_block(r,suffix,work);
                return {LocationState::exact_predecessor,0,b,b+1,true};
            }
            if constexpr(Correct) {
                if(work)++work->bm_local_searches;
                const auto b=directory_block(r,suffix,work,{lo,hi-1});
                return {LocationState::exact_predecessor,0,b,b+1,true};
            }else {
                return {LocationState::candidates,0,lo,hi,true};
            }
        }
    }
    // Candidate: upstream short directory first; a failed bracket keeps the complete original path.
    Location locate_short(std::string_view key,Work* work)const {
        if(regions_.empty())return {};
        const auto& r=regions_[0];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0) {
            if(work)++work->rejected;
            return {LocationState::prefix_rejected,0,0,0,false};
        }
        const auto s=key.substr(r.prefix.size());
        const auto found=short_dir_.predecessor(fence_views_,s,
            [](std::string_view a,std::string_view b){return blocklearn::compare(a,b)<0;});
        if(found)return {LocationState::exact_predecessor,0,std::uint32_t(*found),
                         std::uint32_t(*found)+1,true};
        if(work)++work->fallback;
        return locate_bm<true,false>(key,work);
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
    template<class InputRow> explicit Page(const std::vector<InputRow>& rows,std::size_t occupancy=15) {
        const std::size_t region_keys=2048;
        if(rows.size()>region_keys||(occupancy!=15&&occupancy!=16))throw std::invalid_argument("page size/occupancy");
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
            const auto nblocks=(end-first+occupancy-1)/occupancy;
            if(nblocks>UINT32_MAX-nb)throw std::length_error("32-bit block limit");
            nb+=nblocks;
        }
        if(bytes>UINT32_MAX||nb>UINT32_MAX)throw std::length_error("prototype 32-bit arena/block limit");
        const auto extra=occupancy==15?bytes/32+(bytes%32!=0):0;
        if(extra>UINT32_MAX-bytes)throw std::length_error("page arena reserve limit");
        arena_.reserve(bytes+extra);blocks_.reserve(nb);stable_fences_.reserve(nb);
        for(std::size_t first=0;first<rows.size();first+=region_keys) {
            const auto end=std::min(first+region_keys,rows.size());
            Region r;r.first=static_cast<std::uint32_t>(blocks_.size());
            r.prefix=rows[first].key.substr(0,common_prefix(rows[first].key,rows[end-1].key));
            fences_.push_back(rows[first].key);
            for(std::size_t i=first;i<end;) {
                blocks_.emplace_back();auto& b=blocks_.back();
                while(i<end&&b.count<occupancy) {
                    const auto s=std::string_view(rows[i].key).substr(r.prefix.size());
                    if(s.size()>UINT32_MAX)throw std::length_error("prototype key limit");
                    auto& e=b.entries[b.count];e.offset=static_cast<std::uint32_t>(arena_.size());
                    e.length=static_cast<std::uint32_t>(s.size());e.value=rows[i].value;
                    b.tags[b.count]=fingerprint(s);
                    arena_.insert(arena_.end(),s.begin(),s.end());++b.count;++i;
                }
                stable_fences_.push_back({b.entries[0].offset,b.entries[0].length});
            }
            r.count=static_cast<std::uint32_t>(blocks_.size())-r.first;
            regions_.push_back(std::move(r));
        }
        if(!fences_.empty())fence_prefix_=common_prefix(fences_.front(),fences_.back());
        for(const auto& f:fences_)digests_.push_back(digest8(std::string_view(f).substr(fence_prefix_)));
        std::vector<std::string_view> training;
        if(!regions_.empty())for(std::uint32_t b=0;b<regions_[0].count;++b)training.push_back(boundary(regions_[0],b));
        bm_=blocklearn::BlockModel::fit(training,1,7,8,1);
        if(!regions_.empty())for(std::uint32_t b=0;b<regions_[0].count;++b)
            fence_views_.push_back(boundary(regions_[0],b));
        short_dir_.rebuild(fence_views_);
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
    Page(const Page&)=delete;Page& operator=(const Page&)=delete;
    Page(Page&&)=delete;Page& operator=(Page&&)=delete;
    BuildIdentity build_identity()const {return {"s3-static16-v1","upstream-BM-7x9-v1","BlockModel-fence-1-7-8-1",region_keys_};}
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
        if constexpr(Policy==Lookup::directory)return locate_bm<true,true>(key,work);
        return locate_bm<true,false>(key,work);
    }
    // Independent byte-fence reference checks every prediction, not just successful hits.
    void check_window(std::string_view key)const {
        if(regions_.empty())return;
        const auto exact=locate_bm<true,true>(key,nullptr);
        for(const bool corrected:{false,true}) {
            const auto found=corrected?locate_bm<true,false>(key,nullptr):locate_bm<false,false>(key,nullptr);
            validate_location(found);
            if(found.state==LocationState::prefix_rejected)continue;
            if(found.begin>exact.begin||found.end<=exact.begin)throw std::logic_error("BM correction excludes byte predecessor");
        }
    }
    std::optional<std::uint64_t> find_model(std::string_view k,Work* w=nullptr)const{return find_impl<Lookup::learned>(k,w);}
    std::optional<std::uint64_t> find_short(std::string_view k,Work* w=nullptr)const{
        if(w)++w->queries;
        return verify_range(k,locate_short(k,w),w);
    }
    std::size_t short_directory_bytes()const{return short_dir_.owned_bytes();}
    std::size_t fence_view_bytes()const{return fence_views_.capacity()*sizeof(std::string_view);}
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
            if(compare_boundary(start,mid,lower)<=0)lo=mid+1;
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
    std::size_t segment_count()const{return bm_.segment_count();}
    std::size_t block_count()const{return blocks_.size();}
    bool training_certified()const{return bm_.certified();}
    Memory memory()const {
        Memory m;m.object=sizeof(*this);m.routing=stable_fences_.capacity()*sizeof(Fence);m.arena=arena_.capacity();m.blocks=blocks_.capacity()*sizeof(Block);
        m.regions=regions_.capacity()*sizeof(Region);m.fences=fences_.capacity()*sizeof(std::string);
        m.routing+=digests_.capacity()*sizeof(std::uint64_t);
        for(const auto& r:regions_){m.prefixes+=string_heap(r.prefix);m.model+=r.segments.capacity()*sizeof(Segment);}
        for(const auto& f:fences_)m.fences+=string_heap(f);
        m.model+=bm_.owned_bytes();return m;
    }
    std::size_t verify_model_probes()const {
        std::size_t checks=0;
        for(const auto& r:regions_)for(std::uint32_t b=0;b<r.count;++b) {
            const auto k=r.prefix+std::string(boundary(r,b));
            check_window(k);check_window(k+std::string(1,'\0'));check_window(k+"~");checks+=3;
            if(!k.empty()){check_window(std::string_view(k).substr(0,k.size()-1));++checks;}
        }
        return checks;
    }
private:

    int compare_boundary(const Region& r,std::uint32_t b,std::string_view key)const {
        const auto n=std::min(key.size(),r.prefix.size());
        const int c=n?std::memcmp(r.prefix.data(),key.data(),n):0;
        if(c)return c;
        if(key.size()<r.prefix.size())return 1;
        return compare(boundary(r,b),key.substr(r.prefix.size()));
    }
    struct Slot {std::uint32_t block=0,entry=0;bool found=false;};
    Slot seek(std::string_view key)const {
        const auto location=locate_blocks<Lookup::learned>(key);
        if(!location.prefix_verified)return {};
        const auto s=key.substr(regions_[0].prefix.size());const auto tag=fingerprint(s);
        for(auto b=location.begin;b<location.end;++b) {
            const auto& block=blocks_[b];
            const auto tags=_mm_load_si128(reinterpret_cast<const __m128i*>(block.tags));
            auto mask=std::uint32_t(_mm_movemask_epi8(_mm_cmpeq_epi8(tags,_mm_set1_epi8(static_cast<char>(tag)))));
            mask&=(1u<<block.count)-1;
            while(mask) {
                const auto i=std::uint32_t(__builtin_ctz(mask));mask&=mask-1;
                if(block.entries[i].length==s.size()&&compare(suffix(block.entries[i]),s)==0)return {b,i,true};
            }
        }
        return {};
    }
public:
    enum class Insert {inserted,duplicate,rebuild};
    std::optional<std::uint64_t> find(std::string_view key)const{return find_model(key);}
    bool update(std::string_view key,std::uint64_t value) {
        const auto slot=seek(key);if(!slot.found)return false;
        blocks_[slot.block].entries[slot.entry].value=value;return true;
    }
    bool erase(std::string_view key) {
        const auto slot=seek(key);if(!slot.found)return false;
        auto& block=blocks_[slot.block];
        for(auto i=slot.entry+1;i<block.count;++i){block.entries[i-1]=block.entries[i];block.tags[i-1]=block.tags[i];}
        --block.count;--size_;return true;
    }
    Insert try_insert(std::string_view key,std::uint64_t value) {
        if(key.size()>UINT32_MAX)throw std::length_error("key exceeds page limit");
        if(find(key))return Insert::duplicate;
        if(size_>=2048||regions_.empty())return Insert::rebuild;
        const auto& r=regions_[0];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0)return Insert::rebuild;
        const auto s=key.substr(r.prefix.size());
        const auto b=directory_block(r,s,nullptr);auto& block=blocks_[b];
        if(block.count==16||s.size()>arena_.capacity()-arena_.size())return Insert::rebuild;
        std::uint32_t at=0;while(at<block.count&&compare(suffix(block.entries[at]),s)<0)++at;
        const auto tag=fingerprint(s);const auto offset=std::uint32_t(arena_.size());
        arena_.insert(arena_.end(),s.begin(),s.end()); // reserved, char construction cannot throw
        for(auto i=block.count;i>at;--i){block.entries[i]=block.entries[i-1];block.tags[i]=block.tags[i-1];}
        block.entries[at]={offset,std::uint32_t(s.size()),value};block.tags[at]=tag;
        ++block.count;++size_;return Insert::inserted;
    }
    std::vector<std::string> fence_keys()const {
        std::vector<std::string> out;
        if(!regions_.empty())for(std::uint32_t b=0;b<regions_[0].count;++b)out.push_back(regions_[0].prefix+std::string(boundary(regions_[0],b)));
        return out;
    }
    std::size_t verify()const {
        std::size_t live=0,checks=verify_model_probes();
        if(blocks_.size()!=stable_fences_.size())throw std::logic_error("stable fence count");
        for(std::size_t b=0;b<blocks_.size();++b) {
            const auto& block=blocks_[b];const auto& r=regions_[0];
            if(block.count>16)throw std::logic_error("slot count");
            for(std::uint32_t i=0;i<block.count;++i) {
                const auto& e=block.entries[i];
                if(e.offset>arena_.size()||e.length>arena_.size()-e.offset)throw std::logic_error("slot arena");
                const auto s=suffix(e);
                if((b&&compare(s,boundary(r,b))<0)||(b+1<blocks_.size()&&compare(s,boundary(r,b+1))>=0))throw std::logic_error("slot range");
                if(i&&compare(suffix(block.entries[i-1]),s)>=0)throw std::logic_error("slot order");
                if(block.tags[i]!=fingerprint(s))throw std::logic_error("slot fingerprint");
                const auto key=r.prefix+std::string(s);check_window(key);
                if(find(key)!=e.value)throw std::logic_error("slot find");
                ++checks;++live;
            }
        }
        if(live!=size_)throw std::logic_error("slot cardinality");
        return checks;
    }

public:
    template<Lookup Policy> Location locate_blocks_single(std::string_view key,Work* work=nullptr)const {
        return locate_blocks<Policy>(key,work);
    }
    std::optional<std::uint64_t> find_single(std::string_view key)const{return find_model(key);}
    std::optional<std::uint64_t> find_base(std::string_view key,Work* work=nullptr)const {
        if(work)++work->queries;
        return verify_range(key,locate_bm<false,false>(key,work),work);
    }
    std::optional<std::uint64_t> find_counted(std::string_view key,Work& work)const{return find_model(key,&work);}
    std::optional<std::uint64_t> find_exact(std::string_view key)const{return find_directory(key);}
    // Physical payload identity excludes model metadata, for same-layout controls.
    std::uint64_t payload_identity()const {
        std::uint64_t h=0;
        for(const auto& r:regions_)for(std::uint32_t b=0;b<r.count;++b) {
            for(const unsigned char c:boundary(r,b))h=h*131+c;h=h*131+blocks_[b].count;
            for(std::uint32_t i=0;i<blocks_[b].count;++i) {
                const auto& e=blocks_[b].entries[i];for(const unsigned char c:suffix(e))h=h*131+c;
                h=h*131+e.value;h=h*131+blocks_[b].tags[i];
            }
        }
        return h;
    }

};
} // namespace bm_sdpage

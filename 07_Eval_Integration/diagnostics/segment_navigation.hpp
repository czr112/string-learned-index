#pragma once
#include "physical_locator_index.hpp"

namespace segment_navigation {
using static_physical::compare;
struct Selection {std::uint32_t region=0,segment=0;};
struct Cut {std::string key;Selection selection;};
struct Work {std::uint64_t steps=0,full_compares=0,full_bytes=0,prefix_bytes=0,digest_bytes=0;};
inline int counted_compare(std::string_view a,std::string_view b,std::uint64_t& count) {
    std::size_t i=0;while(i<std::min(a.size(),b.size())&&a[i]==b[i])++i;
    count+=i+(i<std::min(a.size(),b.size()));return compare(a,b);
}
// Smallest byte string whose 7-symbol coordinate is strictly greater than x.
// x must be an actual fence coordinate, hence EOS padding is well formed.
inline std::optional<std::string> successor(std::uint64_t x) {
    std::string key;
    for(unsigned i=0;i<7;++i) {
        const auto symbol=(x>>(9*(6-i)))&511;
        if(!symbol) {
            const auto remaining=9*(6-i);
            if(remaining&&(x&((UINT64_C(1)<<remaining)-1)))throw std::logic_error("invalid EOS coordinate");
            key.push_back('\0');return key;
        }
        if(symbol>256)throw std::logic_error("invalid byte coordinate");
        key.push_back(static_cast<char>(symbol-1));
    }
    for(std::size_t i=key.size();i>0;--i) {
        const auto b=static_cast<unsigned char>(key[i-1]);
        if(b!=255) {key[i-1]=static_cast<char>(b+1);key.resize(i);return key;}
    }
    return {};
}
class Directory {
    std::vector<Cut> cuts_;
    std::vector<std::uint64_t> digests_;
    std::size_t prefix_=0;
public:
    template<class Core> explicit Directory(const Core& core) {
        std::size_t units=0;for(std::size_t r=0;r<core.region_count();++r)units+=core.region(r).segments.size();
        cuts_.reserve(units);digests_.reserve(units);
        for(std::size_t ri=0;ri<core.region_count();++ri) {
            const auto& r=core.region(ri);cuts_.push_back({core.fence(ri),{static_cast<std::uint32_t>(ri),0}});
            for(std::size_t s=1;s<r.segments.size();++s) {
                auto next=successor(r.segments[s-1].xmax);
                if(!next)throw std::logic_error("internal segment has no successor");
                std::string cut=r.prefix;cut+=*next;
                if(compare(cuts_.back().key,cut)>=0||
                   (ri+1<core.region_count()&&compare(cut,core.fence(ri+1))>=0))
                    throw std::logic_error("segment cut outside region");
                cuts_.push_back({std::move(cut),{static_cast<std::uint32_t>(ri),static_cast<std::uint32_t>(s)}});
            }
        }
        for(std::size_t i=1;i<cuts_.size();++i)if(compare(cuts_[i-1].key,cuts_[i].key)>=0)throw std::logic_error("unordered cuts");
        if(!cuts_.empty())prefix_=static_physical::common_prefix(cuts_.front().key,cuts_.back().key);
        for(const auto& c:cuts_)digests_.push_back(static_physical::digest8(std::string_view(c.key).substr(prefix_)));
    }
    Selection select(std::string_view key,Work* work=nullptr)const {
        if(cuts_.empty())throw std::logic_error("empty navigation has no selection");
        const auto pref=key.substr(0,std::min(key.size(),prefix_));
        const auto reference=std::string_view(cuts_.front().key).substr(0,prefix_);
        const auto pc=work?counted_compare(pref,reference,work->prefix_bytes):compare(pref,reference);
        if(pc<0)return cuts_.front().selection;if(pc>0)return cuts_.back().selection;
        if(work)work->digest_bytes+=std::min<std::size_t>(8,key.size()-prefix_);
        const auto digest=static_physical::digest8(key.substr(prefix_));
        std::size_t lo=0,hi=cuts_.size();
        while(lo<hi) {
            if(work)++work->steps;
            const auto mid=lo+(hi-lo)/2;const bool equal=digests_[mid]==digest;
            if(work&&equal)++work->full_compares;
            const bool le=digests_[mid]<digest||(equal&&
                (work?counted_compare(cuts_[mid].key,key,work->full_bytes):compare(cuts_[mid].key,key))<=0);
            if(le)lo=mid+1;else hi=mid;
        }
        return cuts_[lo?lo-1:0].selection;
    }
    std::size_t size()const{return cuts_.size();}
    const std::vector<Cut>& cuts()const{return cuts_;}
    std::size_t memory()const {
        std::size_t bytes=sizeof(*this)+cuts_.capacity()*sizeof(Cut)+digests_.capacity()*sizeof(std::uint64_t);
        for(const auto& c:cuts_)bytes+=static_physical::string_heap(c.key);return bytes;
    }
};
} // namespace segment_navigation

#pragma once
#include "route_attribution_index.hpp"

namespace route_audit {
struct RouteWork {
    std::uint64_t tree_compares=0,tree_bytes=0,digest_steps=0,flat_compares=0,flat_bytes=0,prefix_bytes=0;
};
inline int compare_bytes(std::string_view a,std::string_view b,std::uint64_t& bytes) {
    const auto n=std::min(a.size(),b.size());std::size_t i=0;
    while(i<n&&a[i]==b[i])++i;
    bytes+=i+(i<n);return compare(a,b);
}
// Read-only diagnostic directory; binding lifetimes and topology must be stable.
class FlatRoute {
    std::vector<Index::Binding> bindings_;
    std::vector<std::uint64_t> digests_;
    std::size_t prefix_=0;
public:
    explicit FlatRoute(std::vector<Index::Binding> bindings):bindings_(std::move(bindings)) {
        if(bindings_.empty()||!bindings_[0].lower.empty())throw std::invalid_argument("root sentinel");
        for(std::size_t i=0;i<bindings_.size();++i) {
            if(!bindings_[i].page||(i&&compare(bindings_[i-1].lower,bindings_[i].lower)>=0))throw std::invalid_argument("root bounds");
        }
        if(bindings_.size()>1)prefix_=static_physical::common_prefix(bindings_[1].lower,bindings_.back().lower);
        digests_.reserve(bindings_.size());digests_.push_back(0);
        for(std::size_t i=1;i<bindings_.size();++i)digests_.push_back(static_physical::digest8(std::string_view(bindings_[i].lower).substr(prefix_)));
    }
    template<bool Count=false> std::size_t locate(std::string_view key,RouteWork* work=nullptr)const {
        if(bindings_.size()==1)return 0;
        const auto pref=key.substr(0,std::min(key.size(),prefix_));
        const auto first=std::string_view(bindings_[1].lower).substr(0,prefix_);
        int pc;
        if constexpr(Count)pc=compare_bytes(pref,first,work->prefix_bytes);
        else pc=compare(pref,first);
        if(pc<0)return 0;
        if(pc>0)return bindings_.size()-1;
        const auto digest=static_physical::digest8(key.substr(prefix_));
        std::size_t lo=1,hi=bindings_.size();
        while(lo<hi) {
            if constexpr(Count)++work->digest_steps;
            const auto mid=lo+(hi-lo)/2;
            bool le=digests_[mid]<digest;
            if(digests_[mid]==digest) {
                if constexpr(Count){++work->flat_compares;le=compare_bytes(bindings_[mid].lower,key,work->flat_bytes)<=0;}
                else le=compare(bindings_[mid].lower,key)<=0;
            }
            if(le)lo=mid+1;else hi=mid;
        }
        return lo-1;
    }
    const Page* page(std::string_view key)const{return bindings_[locate(key)].page;}
    const Page* counted_page(std::string_view key,RouteWork& work)const{return bindings_[locate<true>(key,&work)].page;}
    std::size_t memory_bytes()const {
        std::size_t bytes=sizeof(*this)+bindings_.capacity()*sizeof(Index::Binding)+digests_.capacity()*sizeof(std::uint64_t);
        for(const auto& b:bindings_)bytes+=string_heap(b.lower);
        return bytes;
    }
};
struct CountedLess {
    using is_transparent=void;
    RouteWork* work;
    bool operator()(std::string_view a,std::string_view b)const {
        ++work->tree_compares;return compare_bytes(a,b,work->tree_bytes)<0;
    }
};
class CountedTree {
    RouteWork work_;
    std::map<std::string,const Page*,CountedLess> tree_{CountedLess{&work_}};
public:
    explicit CountedTree(const std::vector<Index::Binding>& bindings) {
        for(const auto& b:bindings)tree_.emplace(b.lower,b.page);
    }
    const Page* page(std::string_view key,RouteWork& work) {
        work_={};auto it=tree_.upper_bound(key);if(it!=tree_.begin())--it;
        work.tree_compares=work_.tree_compares;work.tree_bytes=work_.tree_bytes;return it->second;
    }
};
template<bool Flat,bool Single> std::optional<std::uint64_t> find(const Index& index,const FlatRoute& flat,std::string_view key) {
    const auto* page=Flat?flat.page(key):index.tree_page(key);
    if constexpr(Single)return page->find_single(key);
    else return page->find(key);
}
} // namespace route_audit

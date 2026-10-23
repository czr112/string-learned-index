#pragma once
#include "route_attribution_page.hpp"

namespace coupled_page {
using route_audit_leaf::Page;
using route_audit_leaf::Row;
using route_audit_leaf::compare;
using route_audit_leaf::string_heap;
struct Binding {std::string lower;const Page* page;};
// Owns separators, borrows pages. Only a fully prepared root may be published.
class FlatRoot {
    std::vector<Binding> bindings_;
    std::vector<std::uint64_t> digests_;
    std::size_t prefix_=0;
public:
    explicit FlatRoot(std::vector<Binding> bindings):bindings_(std::move(bindings)) {
        if(bindings_.empty()||!bindings_[0].lower.empty())throw std::invalid_argument("root sentinel");
        for(std::size_t i=0;i<bindings_.size();++i)
            if(!bindings_[i].page||(i&&compare(bindings_[i-1].lower,bindings_[i].lower)>=0))throw std::invalid_argument("root bounds");
        if(bindings_.size()>1)prefix_=static_physical::common_prefix(bindings_[1].lower,bindings_.back().lower);
        digests_.reserve(bindings_.size());digests_.push_back(0);
        for(std::size_t i=1;i<bindings_.size();++i)
            digests_.push_back(static_physical::digest8(std::string_view(bindings_[i].lower).substr(prefix_)));
    }
    std::size_t locate(std::string_view key)const {
        if(bindings_.size()==1)return 0;
        const auto pref=key.substr(0,std::min(key.size(),prefix_));
        const auto pc=compare(pref,std::string_view(bindings_[1].lower).substr(0,prefix_));
        if(pc<0)return 0;
        if(pc>0)return bindings_.size()-1;
        const auto digest=static_physical::digest8(key.substr(prefix_));
        std::size_t lo=1,hi=bindings_.size();
        while(lo<hi) {
            const auto mid=lo+(hi-lo)/2;
            const bool le=digests_[mid]<digest||(digests_[mid]==digest&&compare(bindings_[mid].lower,key)<=0);
            if(le)lo=mid+1;else hi=mid;
        }
        return lo-1;
    }
    const Page* page(std::string_view key)const{return bindings_[locate(key)].page;}
    const std::vector<Binding>& bindings()const{return bindings_;}
    std::size_t key_bytes()const {std::size_t n=0;for(const auto& b:bindings_)n+=b.lower.size();return n;}
    std::size_t memory_bytes()const {
        std::size_t n=sizeof(*this)+bindings_.capacity()*sizeof(Binding)+digests_.capacity()*sizeof(std::uint64_t);
        for(const auto& b:bindings_)n+=string_heap(b.lower);
        return n;
    }
};
} // namespace coupled_page

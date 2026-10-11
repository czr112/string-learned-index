#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

// Static prototype: owns hot summaries, borrows cold fences from immutable input.
// Numeric summaries preserve unsigned-byte order. Equal summaries ALWAYS compare
// full keys, including short keys, embedded zeros and common prefixes beyond 8 bytes.
class CompactFenceRouter {
    std::vector<std::uint64_t> summaries_;
    std::vector<std::string_view> fences_;
    std::string_view prefix_;
    static std::uint64_t summary(std::string_view key) {
        std::uint64_t out=0;
        for (std::size_t i=0;i<8;++i)
            out=(out<<8)|(i<key.size()?static_cast<unsigned char>(key[i]):0);
        return out;
    }
    static int compare(std::string_view a,std::string_view b) {
        return a.compare(b); // char_traits<char>::compare uses unsigned-byte memcmp
    }
public:
    explicit CompactFenceRouter(std::vector<std::string_view> fences):fences_(std::move(fences)) {
        if (fences_.empty()) throw std::invalid_argument("router needs a fence");
        for (std::size_t i=1;i<fences_.size();++i)
            if (compare(fences_[i-1],fences_[i])>=0) throw std::invalid_argument("fences not ordered");
        const auto first=fences_.front(),last=fences_.back();
        std::size_t common=0;
        while (common<std::min(first.size(),last.size()) && first[common]==last[common]) ++common;
        prefix_=first.substr(0,common);
        summaries_.reserve(fences_.size());
        for (const auto fence:fences_) summaries_.push_back(summary(fence.substr(common)));
    }
    std::size_t locate(std::string_view key) const {
        if (!prefix_.empty()) {
            const int order=compare(key.substr(0,std::min(key.size(),prefix_.size())),prefix_);
            if (order<0) return 0;
            if (order>0) return fences_.size()-1;
        }
        const auto query=summary(key.substr(prefix_.size()));
        std::size_t lo=0,hi=summaries_.size();
        while (lo<hi) {
            const auto mid=lo+(hi-lo)/2;
            const bool before=query==summaries_[mid]?compare(key,fences_[mid])<0:query<summaries_[mid];
            if (before) hi=mid; else lo=mid+1;
        }
        return lo?lo-1:0;
    }
    std::size_t auxiliary_bytes() const {
        return sizeof(*this)+summaries_.capacity()*sizeof(std::uint64_t)+
            fences_.capacity()*sizeof(std::string_view);
    }
    std::size_t prefix_bytes() const { return prefix_.size(); }
};

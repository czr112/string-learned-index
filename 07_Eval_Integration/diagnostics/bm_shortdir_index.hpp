#pragma once
// Read-only candidate index: owns its pages and its sorted routing vector, and routes every find
// through the short-directory leaf. No dynamic operations, no publication machinery; this is the
// static candidate the plan's full read gate has to judge.
#include "bm_shortdir_page.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bm_sdindex {
struct Index {
    std::vector<std::string> lowers_;
    std::vector<std::unique_ptr<bm_sdpage::Page>> pages_;
    std::size_t size_=0;

    explicit Index(const std::vector<static_physical::Row>& rows) {
        constexpr std::size_t page_rows=2048;
        for(std::size_t first=0;first<rows.size();first+=page_rows) {
            const auto last=std::min(first+page_rows,rows.size());
            std::vector<static_physical::Row> part(rows.begin()+first,rows.begin()+last);
            lowers_.push_back(first?rows[first].key:std::string());
            pages_.push_back(std::make_unique<bm_sdpage::Page>(part,15));
        }
        size_=rows.size();
    }
    Index(const Index&)=delete;Index& operator=(const Index&)=delete;

    std::size_t route(std::string_view key)const {
        std::size_t lo=0,hi=lowers_.size();
        while(lo<hi) {
            const auto mid=lo+(hi-lo)/2;
            if(static_physical::compare(lowers_[mid],key)<=0)lo=mid+1;else hi=mid;
        }
        return lo?lo-1:0;
    }
    std::optional<std::uint64_t> find(std::string_view key)const {
        if(pages_.empty())return {};
        return pages_[route(key)]->find_short(key);
    }
    std::vector<static_physical::Row> scan(std::string_view lower,std::size_t limit)const {
        std::vector<static_physical::Row> out;
        if(!limit||pages_.empty())return out;
        for(auto page=route(lower);page<pages_.size()&&out.size()<limit;++page) {
            auto part=pages_[page]->scan(lower,limit-out.size());
            for(auto& row:part)out.push_back(std::move(row));
        }
        return out;
    }
    std::size_t size()const{return size_;}
    std::size_t page_count()const{return pages_.size();}
    std::size_t short_directory_bytes()const {
        std::size_t bytes=0;for(const auto& page:pages_)bytes+=page->short_directory_bytes();return bytes;
    }
    std::size_t fence_view_bytes()const {
        std::size_t bytes=0;for(const auto& page:pages_)bytes+=page->fence_view_bytes();return bytes;
    }
    // Owning capacity: routing strings, page vector, page cores, short-directory codes, fence views.
    std::size_t memory()const {
        std::size_t bytes=sizeof(*this)+lowers_.capacity()*sizeof(std::string)
            +pages_.capacity()*sizeof(std::unique_ptr<bm_sdpage::Page>);
        for(const auto& lower:lowers_)bytes+=lower.capacity();
        for(const auto& page:pages_)
            bytes+=page->memory().total()+page->short_directory_bytes()+page->fence_view_bytes();
        return bytes;
    }
};
} // namespace bm_sdindex

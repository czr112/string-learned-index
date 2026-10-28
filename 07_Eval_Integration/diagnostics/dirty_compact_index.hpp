#pragma once
#include "slotted_page_core.hpp"
#include <map>
#include <memory>
#include <iterator>
#include <new>
#include <set>

namespace dirty_compact {
using slotted_page::Page;
using slotted_page::Row;
using slotted_page::compare;
using slotted_page::string_heap;

struct ByteLess {
    using is_transparent=void;
    bool operator()(std::string_view a,std::string_view b)const noexcept{return compare(a,b)<0;}
};
struct Maintenance {
    std::uint64_t compact_calls=0,compact_targets=0,dirty_marks=0,dirty_clears=0;

    std::uint64_t inplace_updates=0,inplace_erases=0,inplace_inserts=0;
    std::uint64_t rebuilds=0,splits=0,coalesces=0,failed_rebuilds=0,rows_rebuilt=0,key_value_bytes_rebuilt=0;
    std::size_t max_source_pages=0,max_rebuild_rows=0,accounted_peak_bytes=0;
};
struct PageState {std::string lower;std::size_t rows;std::uint64_t version;};
class Index {
    struct Node {std::unique_ptr<Page> core;std::uint64_t version;};
    using Pages=std::map<std::string,Node,ByteLess>;
    Pages pages_;
    std::size_t size_=0,resident_=0,occupancy_=15;
    std::uint64_t generation_=0;
    Maintenance maintenance_;
    std::optional<std::size_t> fail_after_pages_;
    using Iterator=Pages::iterator;
    struct DirtyLess {
        bool operator()(const Iterator& a,const Iterator& b)const noexcept{return compare(a->first,b->first)<0;}
    };
    std::set<Iterator,DirtyLess> dirty_;
    bool fail_dirty_mark_=false;
    bool mark_dirty(Iterator it) {
        if(dirty_.find(it)!=dirty_.end())return false;
        if(fail_dirty_mark_){fail_dirty_mark_=false;throw std::bad_alloc();}
        dirty_.insert(it);resident_+=sizeof(Iterator);++maintenance_.dirty_marks;peak();return true;
    }
    void clear_dirty(Iterator it) {
        auto found=dirty_.find(it);
        if(found!=dirty_.end()){dirty_.erase(found);resident_-=sizeof(Iterator);++maintenance_.dirty_clears;}
    }
    static std::size_t payload(const Pages::value_type& item) {
        return sizeof(std::string)+string_heap(item.first)+sizeof(Node)+item.second.core->memory().total();
    }
    static std::size_t route_bytes(const Pages& pages) {
        std::size_t bytes=0;for(const auto& p:pages)bytes+=payload(p);return bytes;
    }
    static std::size_t row_bytes(const std::vector<Row>& rows) {
        std::size_t bytes=rows.capacity()*sizeof(Row);for(const auto& r:rows)bytes+=string_heap(r.key);return bytes;
    }
    template<class P> static auto route(P& pages,std::string_view key) {
        auto it=pages.upper_bound(key);return it==pages.begin()?it:std::prev(it);
    }
    void peak(std::size_t extra=0){maintenance_.accounted_peak_bytes=std::max(maintenance_.accounted_peak_bytes,resident_+extra);}
    void fault(std::size_t count) {
        if(fail_after_pages_&&count==*fail_after_pages_){fail_after_pages_.reset();throw std::bad_alloc();}
    }
    Pages::iterator rebuild(Pages::iterator target,std::vector<Row> rows,bool coalesce) {
        try {
            auto first=target,neighbor=pages_.end();
            if(coalesce&&rows.size()<512&&pages_.size()>1) {
                // Dirty work is consumed in key order. Prefer the earlier page
                // so zero-row results cannot leave separate clean empty islands.
                if(target!=pages_.begin()) {
                    auto prev=std::prev(target);
                    if(rows.size()+prev->second.core->size()<=2048){neighbor=prev;first=prev;}
                }
                if(neighbor==pages_.end()) {
                    auto next=std::next(target);
                    if(next!=pages_.end()&&rows.size()+next->second.core->size()<=2048)neighbor=next;
                }
            }
            if(neighbor!=pages_.end()) {
                auto other=neighbor->second.core->scan("",SIZE_MAX);
                std::vector<Row> joined;joined.reserve(rows.size()+other.size());
                peak(row_bytes(rows)+row_bytes(other)+joined.capacity()*sizeof(Row));
                auto append=[&](std::vector<Row>& from){for(auto& r:from)joined.push_back(std::move(r));};
                if(first==neighbor){append(other);append(rows);}else{append(rows);append(other);}
                rows=std::move(joined);
            }
            const auto pieces=rows.size()>2048?std::size_t(2):std::size_t(1);
            Pages staged;
            std::uint64_t bytes=0;for(const auto& r:rows)bytes+=r.key.size()+sizeof(r.value);
            for(std::size_t i=0;i<pieces;++i) {
                fault(i);
                const auto begin=i?rows.size()/2:0;
                const auto end=pieces==1||i?rows.size():rows.size()/2;
                std::vector<Row> part(rows.begin()+begin,rows.begin()+end);
                auto core=std::make_unique<Page>(part,occupancy_);
                staged.emplace(i?rows[begin].key:first->first,Node{std::move(core),generation_+i+1});
                peak(row_bytes(rows)+row_bytes(part)+route_bytes(staged));
            }
            fault(pieces);
            const auto removed=payload(*target)+(neighbor==pages_.end()?0:payload(*neighbor));
            const auto added=route_bytes(staged);
            const bool merging=neighbor!=pages_.end();
            clear_dirty(target);if(merging)clear_dirty(neighbor);
            auto replacement=staged.begin();std::swap(first->second,replacement->second);staged.erase(replacement);
            if(neighbor!=pages_.end())pages_.erase(first==target?neighbor:target);
            while(!staged.empty()) {
                auto node=staged.extract(staged.begin());
                if(!pages_.insert(std::move(node)).inserted)std::terminate();
            }
            resident_=resident_-removed+added;generation_+=pieces;
            ++maintenance_.rebuilds;maintenance_.splits+=pieces-1;maintenance_.coalesces+=merging;
            maintenance_.rows_rebuilt+=rows.size();maintenance_.key_value_bytes_rebuilt+=bytes;
            maintenance_.max_source_pages=std::max(maintenance_.max_source_pages,merging?std::size_t(2):std::size_t(1));
            maintenance_.max_rebuild_rows=std::max(maintenance_.max_rebuild_rows,rows.size());
            return first;
        }catch(...){++maintenance_.failed_rebuilds;throw;}
    }
public:
    explicit Index(const std::vector<Row>& rows={},std::size_t occupancy=15):occupancy_(occupancy) {
        if(occupancy!=15&&occupancy!=16)throw std::invalid_argument("occupancy");
        for(std::size_t i=1;i<rows.size();++i)
            if(compare(rows[i-1].key,rows[i].key)>=0)throw std::invalid_argument("sorted unique rows required");
        for(std::size_t begin=0;begin<rows.size()||pages_.empty();) {
            const auto end=std::min(begin+2048,rows.size());
            std::vector<Row> part(rows.begin()+begin,rows.begin()+end);
            pages_.emplace(begin?rows[begin].key:std::string{},Node{std::make_unique<Page>(part,occupancy_),++generation_});
            if(end==rows.size())break;
            begin=end;
        }
        size_=rows.size();resident_=memory_bytes();peak();
    }
    Index(const Index&)=delete;
    Index& operator=(const Index&)=delete;
    std::optional<std::uint64_t> find(std::string_view key)const{return route(pages_,key)->second.core->find(key);}
    bool insert(std::string_view key,std::uint64_t value) {
        auto it=route(pages_,key);
        if(it->second.core->find(key))return false;
        const bool marked=mark_dirty(it);
        try {
            const auto result=it->second.core->try_insert(key,value);
            if(result==Page::Insert::duplicate){if(marked)clear_dirty(it);return false;}
            if(result==Page::Insert::inserted){++maintenance_.inplace_inserts;it->second.version=++generation_;}
            else {
                auto rows=it->second.core->scan("",SIZE_MAX);
                const auto at=std::lower_bound(rows.begin(),rows.end(),key,[](const Row& r,std::string_view k){return compare(r.key,k)<0;});
                rows.insert(at,Row{std::string(key),value});rebuild(it,std::move(rows),false);
            }
            ++size_;return true;
        }catch(...){if(marked)clear_dirty(it);throw;}
    }
    bool update(std::string_view key,std::uint64_t value) {
        auto it=route(pages_,key);if(!it->second.core->update(key,value))return false;
        ++maintenance_.inplace_updates;it->second.version=++generation_;return true;
    }
    bool erase(std::string_view key) {
        auto it=route(pages_,key);if(!it->second.core->find(key))return false;
        const bool marked=mark_dirty(it);
        try {
            if(!it->second.core->erase(key))throw std::logic_error("erase changed between seek and commit");
            --size_;++maintenance_.inplace_erases;it->second.version=++generation_;return true;
        }catch(...){if(marked)clear_dirty(it);throw;}
    }
    std::vector<Row> scan(std::string_view lower,std::size_t limit)const {
        std::vector<Row> out;
        if(!limit)return out;
        for(auto it=route(pages_,lower);it!=pages_.end()&&out.size()<limit;++it) {
            auto part=it->second.core->scan(lower,limit-out.size());
            for(auto& row:part)out.push_back(std::move(row));
        }
        return out;
    }
    void compact() {
        ++maintenance_.compact_calls;
        while(!dirty_.empty()) {
            auto it=*dirty_.begin();++maintenance_.compact_targets;
            rebuild(it,it->second.core->scan("",SIZE_MAX),true);
        }
    }
    void fail_next_dirty_mark_for_testing(){fail_dirty_mark_=true;}
    std::size_t dirty_page_count()const{return dirty_.size();}
    void fail_next_rebuild_for_testing(std::size_t count=0){fail_after_pages_=count;}
    std::size_t size()const{return size_;}
    std::size_t page_count()const{return pages_.size();}
    std::size_t memory_bytes()const{return sizeof(*this)+route_bytes(pages_)+dirty_.size()*sizeof(Iterator);}
    const Maintenance& maintenance()const{return maintenance_;}
    std::vector<PageState> page_states()const {
        std::vector<PageState> out;
        for(const auto& p:pages_)out.push_back({p.first,p.second.core->size(),p.second.version});
        return out;
    }
    std::vector<std::vector<std::string>> fence_keys()const {
        std::vector<std::vector<std::string>> out;
        for(const auto& p:pages_)out.push_back(p.second.core->fence_keys());
        return out;
    }
    void check_window(std::string_view key)const{route(pages_,key)->second.core->check_window(key);}
    std::size_t verify()const {
        if(pages_.empty()||!pages_.begin()->first.empty())throw std::logic_error("sentinel");
        std::size_t live=0,checks=0;
        for(auto it=pages_.begin();it!=pages_.end();++it) {
            const auto next=std::next(it);checks+=it->second.core->verify();
            if(!it->second.version||it->second.core->size()>2048)throw std::logic_error("page limits");
            const auto rows=it->second.core->scan("",SIZE_MAX);
            for(const auto& r:rows) {
                if(compare(r.key,it->first)<0||(next!=pages_.end()&&compare(r.key,next->first)>=0))throw std::logic_error("page range");
                if(find(r.key)!=r.value)throw std::logic_error("top lookup");
                ++checks;
            }
            live+=rows.size();
        }
        for(const auto it:dirty_)if(pages_.find(it->first)!=it)throw std::logic_error("dirty membership");
        if(live!=size_||resident_!=memory_bytes())throw std::logic_error("size/memory");
        return checks;
    }
};
} // namespace dirty_compact

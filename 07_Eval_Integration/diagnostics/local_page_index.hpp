#pragma once
// F1 experimental dynamic type. The frozen D1 core is an immutable page base.
#include "collision_refined_index.hpp"
#include <iterator>
#include <map>
#include <memory>
#include <new>

namespace local_page {
using static_physical::Row;
using static_physical::compare;
using static_physical::string_heap;

struct ByteLess {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return compare(a,b)<0; }
};

struct Maintenance {
    std::uint64_t rebuild_events=0, pages_built=0, splits=0, coalesces=0, failed_rebuilds=0;
    std::uint64_t rows_rebuilt=0, key_value_bytes_rebuilt=0, source_base_rows_read=0;
    std::uint64_t delta_entries_copied=0, delta_key_bytes_copied=0;
    std::uint64_t route_insertions=0, route_removals=0;
    std::size_t max_source_pages=0, max_rebuild_rows=0, accounted_peak_bytes=0;
};
struct PageState {
    std::string lower;
    std::size_t base_rows=0, live_rows=0, delta_entries=0, delta_bytes=0;
    std::uint64_t version=0;
};
struct Memory {
    std::size_t object=0, bases=0, deltas=0, route_payloads=0;
    std::size_t total() const { return object+bases+deltas+route_payloads; }
};

class Index {
public:
    static constexpr std::size_t page_rows=2048, delta_limit=128, merge_below=512;
private:
    struct Delta { std::string key; std::optional<std::uint64_t> value; };
    using Deltas=std::vector<Delta>;
    struct Page {
        std::unique_ptr<collision_refined::Index> base;
        Deltas delta;
        std::size_t live_rows=0;
        std::uint64_t version=0;
    };
    using Pages=std::map<std::string,Page,ByteLess>;
    Pages pages_;
    std::size_t size_=0;
    std::size_t resident_bytes_=0;
    std::uint64_t generation_=0;
    Maintenance maintenance_;
    std::optional<std::size_t> fail_after_pages_;

    template<class D> static auto delta_lower(D& delta,std::string_view key) {
        return std::lower_bound(delta.begin(),delta.end(),key,
            [](const Delta& d,std::string_view k){return compare(d.key,k)<0;});
    }
    static std::size_t delta_bytes(const Deltas& delta) {
        std::size_t bytes=delta.capacity()*sizeof(Delta);
        for(const auto& d:delta)bytes+=string_heap(d.key);
        return bytes;
    }
    static std::size_t row_bytes(const std::vector<Row>& rows) {
        std::size_t bytes=rows.capacity()*sizeof(Row);
        for(const auto& r:rows)bytes+=string_heap(r.key);
        return bytes;
    }
    static std::size_t page_bytes(const Page& page) {
        return sizeof(Page)+page.base->memory().total()+delta_bytes(page.delta);
    }
    static std::size_t route_bytes(const Pages& pages) {
        std::size_t bytes=0;
        for(const auto& item:pages)bytes+=sizeof(std::string)+string_heap(item.first)+page_bytes(item.second);
        return bytes;
    }
    void observe_peak(std::size_t extra=0) {
        maintenance_.accounted_peak_bytes=std::max(maintenance_.accounted_peak_bytes,resident_bytes_+extra);
    }
    template<class P> static auto route(P& pages,std::string_view key) {
        auto it=pages.upper_bound(key);
        return it==pages.begin()?it:std::prev(it);
    }
    static std::optional<std::uint64_t> find_page(const Page& page,std::string_view key) {
        if(!page.delta.empty()) {
            const auto d=delta_lower(page.delta,key);
            if(d!=page.delta.end()&&compare(d->key,key)==0)return d->value;
        }
        return page.base->find_model(key);
    }
    // Bounded by one page. The returned keys own their bytes; neither scan nor
    // rebuilding keeps views into the old arena or the caller's input.
    static std::vector<Row> merged(const Page& page,const Deltas& delta,
                                   std::string_view lower,std::size_t limit,
                                   std::size_t* scratch_peak=nullptr) {
        auto base=page.base->scan(lower,page.base->size());
        std::vector<Row> out;
        out.reserve(std::min(limit,base.size()+delta.size()));
        auto b=base.begin();auto d=delta_lower(delta,lower);
        while(out.size()<limit&&(b!=base.end()||d!=delta.end())) {
            if(d==delta.end()||(b!=base.end()&&compare(b->key,d->key)<0)) {
                out.push_back(*b++);
            } else if(b==base.end()||compare(d->key,b->key)<0) {
                if(d->value)out.push_back({d->key,*d->value});
                ++d;
            } else {
                if(d->value)out.push_back({d->key,*d->value});
                ++d;++b;
            }
        }
        if(scratch_peak)*scratch_peak=std::max(*scratch_peak,row_bytes(base)+row_bytes(out));
        return out;
    }
    static Page make_page(const std::vector<Row>& rows,std::uint64_t version) {
        Page page;
        page.base=std::make_unique<collision_refined::Index>(rows,page_rows);
        page.live_rows=rows.size();page.version=version;
        return page;
    }
    void fault_point(std::size_t completed) {
        if(fail_after_pages_&&completed==*fail_after_pages_) {
            fail_after_pages_.reset();throw std::bad_alloc();
        }
    }
    // Prepare everything that allocates before touching the published pages.
    // std::map node transfer uses equal standard allocators and ByteLess cannot
    // throw, so the publication below neither allocates nor calls user code.
    Pages::iterator rebuild(Pages::iterator target,const Deltas& delta,std::size_t live) {
        try {
            const auto resident=resident_bytes_;
            std::size_t scratch=0;
            auto rows=merged(target->second,delta,"",SIZE_MAX,&scratch);
            if(rows.size()!=live)throw std::logic_error("page cardinality");
            auto first=target,neighbor=pages_.end();
            if(live<merge_below&&pages_.size()>1) {
                auto next=std::next(target);
                if(next!=pages_.end()&&live+next->second.live_rows<=page_rows)neighbor=next;
                else if(target!=pages_.begin()) {
                    auto prev=std::prev(target);
                    if(live+prev->second.live_rows<=page_rows){neighbor=prev;first=prev;}
                }
            }
            std::size_t sources=1,source_rows=target->second.base->size();
            if(neighbor!=pages_.end()) {
                std::size_t other_scratch=0;
                auto other=merged(neighbor->second,neighbor->second.delta,"",SIZE_MAX,&other_scratch);
                scratch=std::max(scratch,row_bytes(rows)+other_scratch);
                std::vector<Row> combined;
                combined.reserve(rows.size()+other.size());
                const auto append=[&](std::vector<Row>& from) {
                    for(auto& r:from)combined.push_back(std::move(r));
                };
                // Before moving string buffers, account both input arrays and
                // the combined array. Internal D1 builder scratch is excluded.
                scratch=std::max(scratch,row_bytes(rows)+row_bytes(other)+combined.capacity()*sizeof(Row));
                if(first==neighbor){append(other);append(rows);}else{append(rows);append(other);}
                rows=std::move(combined);
                sources=2;source_rows+=neighbor->second.base->size();
            }
            const auto delta_scratch=&delta==&target->second.delta?0:delta_bytes(delta);
            maintenance_.accounted_peak_bytes=std::max(maintenance_.accounted_peak_bytes,resident+delta_scratch+scratch);
            Pages staged;
            const std::size_t pieces=rows.size()>page_rows?2:1;
            std::uint64_t key_bytes=0;
            for(const auto& r:rows)key_bytes+=r.key.size()+sizeof(r.value);
            for(std::size_t i=0;i<pieces;++i) {
                fault_point(i);
                const auto begin=i?rows.size()/2:0;
                const auto end=pieces==1||i?rows.size():rows.size()/2;
                std::vector<Row> part(rows.begin()+begin,rows.begin()+end);
                auto page=make_page(part,generation_+i+1);
                const std::string lower=i?rows[begin].key:first->first;
                staged.emplace(lower,std::move(page));
                observe_peak(delta_scratch+row_bytes(rows)+row_bytes(part)+route_bytes(staged));
            }
            fault_point(pieces);
            const auto removed_bytes=sizeof(std::string)+string_heap(target->first)+page_bytes(target->second)
                +(neighbor==pages_.end()?0:sizeof(std::string)+string_heap(neighbor->first)+page_bytes(neighbor->second));
            const auto added_bytes=route_bytes(staged);
            auto replacement=staged.begin();
            std::swap(first->second,replacement->second);
            staged.erase(replacement); // only destroys the replaced old page
            if(neighbor!=pages_.end())pages_.erase(first==target?neighbor:target);
            while(!staged.empty()) {
                auto node=staged.extract(staged.begin());
                const auto inserted=pages_.insert(std::move(node));
                // All split bounds are strictly inside the original range.
                if(!inserted.inserted)std::terminate();
            }
            resident_bytes_=resident-removed_bytes+added_bytes;
            generation_+=pieces;
            ++maintenance_.rebuild_events;maintenance_.pages_built+=pieces;
            maintenance_.splits+=pieces-1;maintenance_.coalesces+=sources-1;
            maintenance_.route_insertions+=pieces-1;maintenance_.route_removals+=sources-1;
            maintenance_.rows_rebuilt+=rows.size();maintenance_.key_value_bytes_rebuilt+=key_bytes;
            maintenance_.source_base_rows_read+=source_rows;
            maintenance_.max_source_pages=std::max(maintenance_.max_source_pages,sources);
            maintenance_.max_rebuild_rows=std::max(maintenance_.max_rebuild_rows,rows.size());
            return first;
        } catch(...) {
            ++maintenance_.failed_rebuilds;throw;
        }
    }
    enum class Write { insert, update, erase };
    bool write(std::string_view key,std::uint64_t value,Write mode) {
        if(key.size()>UINT32_MAX)throw std::length_error("page key limit");
        auto it=route(pages_,key);auto& page=it->second;
        const auto old=find_page(page,key);
        if(mode==Write::insert?bool(old):!old)return false;
        const auto original=delta_lower(page.delta,key);
        const bool exists=original!=page.delta.end()&&compare(original->key,key)==0;
        Deltas staged;staged.reserve(page.delta.size()+(exists?0:1));
        for(const auto& d:page.delta) {
            staged.push_back(d);++maintenance_.delta_entries_copied;
            maintenance_.delta_key_bytes_copied+=d.key.size();
        }
        auto d=delta_lower(staged,key);
        if(mode==Write::erase&&!page.base->find_model(key)) {
            // A delta-only insertion can be removed instead of leaving a tombstone.
            if(d!=staged.end()&&compare(d->key,key)==0)staged.erase(d);
        } else {
            const auto next=mode==Write::erase?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{value};
            if(exists)d->value=next;
            else staged.insert(d,Delta{std::string(key),next});
        }
        const auto live=page.live_rows+(mode==Write::insert?1:0)-(mode==Write::erase?1:0);
        const auto budget=std::max<std::size_t>(1,page.base->memory().total()/10);
        observe_peak(delta_bytes(staged));
        if(live>page_rows||live==0||staged.size()>=delta_limit||delta_bytes(staged)>=budget) {
            rebuild(it,staged,live);
        } else {
            const auto old_bytes=page_bytes(page);
            page.delta.swap(staged);page.live_rows=live;page.version=++generation_;
            resident_bytes_=resident_bytes_-old_bytes+page_bytes(page);
        }
        size_+=(mode==Write::insert?1:0);size_-=(mode==Write::erase?1:0);
        return true;
    }
public:
    explicit Index(const std::vector<Row>& rows={}) {
        for(std::size_t i=1;i<rows.size();++i)
            if(compare(rows[i-1].key,rows[i].key)>=0)throw std::invalid_argument("sorted unique rows required");
        for(std::size_t begin=0;begin<rows.size()||pages_.empty();) {
            const auto end=std::min(begin+page_rows,rows.size());
            std::vector<Row> part(rows.begin()+begin,rows.begin()+end);
            pages_.emplace(begin?rows[begin].key:std::string{},make_page(part,++generation_));
            if(end==rows.size())break;
            begin=end;
        }
        size_=rows.size();resident_bytes_=memory().total();observe_peak();
    }
    Index(const Index&)=delete;
    Index& operator=(const Index&)=delete;
    std::optional<std::uint64_t> find(std::string_view key) const {return find_page(route(pages_,key)->second,key);}
    bool insert(std::string_view key,std::uint64_t value){return write(key,value,Write::insert);}
    bool update(std::string_view key,std::uint64_t value){return write(key,value,Write::update);}
    bool erase(std::string_view key){return write(key,0,Write::erase);}
    std::vector<Row> scan(std::string_view lower,std::size_t limit) const {
        std::vector<Row> out;
        if(!limit)return out;
        for(auto it=route(pages_,lower);it!=pages_.end()&&out.size()<limit;++it) {
            const auto part=merged(it->second,it->second.delta,lower,limit-out.size());
            for(const auto& r:part)out.push_back(r);
        }
        return out;
    }
    void compact() {
        for(auto it=pages_.begin();it!=pages_.end();++it)
            if(!it->second.delta.empty())it=rebuild(it,it->second.delta,it->second.live_rows);
    }
    std::size_t size()const{return size_;}
    std::size_t page_count()const{return pages_.size();}
    const Maintenance& maintenance()const{return maintenance_;}
    Memory memory()const {
        Memory m;m.object=sizeof(*this);
        for(const auto& item:pages_) {
            m.bases+=item.second.base->memory().total();m.deltas+=delta_bytes(item.second.delta);
            m.route_payloads+=sizeof(std::string)+string_heap(item.first)+sizeof(Page);
        }
        return m;
    }
    std::vector<PageState> page_states()const {
        std::vector<PageState> result;
        for(const auto& item:pages_)result.push_back({item.first,item.second.base->size(),item.second.live_rows,
            item.second.delta.size(),delta_bytes(item.second.delta),item.second.version});
        return result;
    }
    void fail_next_rebuild_for_testing(std::size_t completed_pages=0){fail_after_pages_=completed_pages;}
    std::size_t verify()const {
        if(pages_.empty()||!pages_.begin()->first.empty())throw std::logic_error("missing sentinel");
        std::size_t count=0,checks=0;
        for(auto it=pages_.begin();it!=pages_.end();++it) {
            const auto& page=it->second;const auto next=std::next(it);
            auto in_range=[&](std::string_view key) {
                return compare(key,it->first)>=0&&(next==pages_.end()||compare(key,next->first)<0);
            };
            if(page.live_rows>page_rows||!page.version||page.delta.size()>=delta_limit)
                throw std::logic_error("page limits");
            if(!page.delta.empty()&&delta_bytes(page.delta)>=std::max<std::size_t>(1,page.base->memory().total()/10))
                throw std::logic_error("delta byte limit");
            for(std::size_t d=0;d<page.delta.size();++d) {
                if(!in_range(page.delta[d].key)||(d&&compare(page.delta[d-1].key,page.delta[d].key)>=0))
                    throw std::logic_error("delta range/order");
            }
            const auto base=page.base->scan("",SIZE_MAX);
            for(const auto& r:base)if(!in_range(r.key))throw std::logic_error("base range");
            auto live=merged(page,page.delta,"",SIZE_MAX);
            if(live.size()!=page.live_rows)throw std::logic_error("live count");
            checks+=page.base->verify_numeric_certificate();
            for(const auto& r:live) {
                if(!in_range(r.key)||find(r.key)!=r.value)throw std::logic_error("live lookup/range");
                page.base->check_window(r.key);++checks;
            }
            count+=live.size();
        }
        if(count!=size_)throw std::logic_error("global cardinality");
        if(memory().total()!=resident_bytes_)throw std::logic_error("resident accounting");
        return checks;
    }
};
} // namespace local_page

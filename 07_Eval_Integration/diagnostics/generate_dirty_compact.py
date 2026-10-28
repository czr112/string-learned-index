"""Derive a maintenance-only integration from frozen F3; leave leaf/read/Node unchanged."""
from pathlib import Path

HERE=Path(__file__).resolve().parent
source=(HERE/'slotted_page_index.hpp').read_text(encoding='utf-8')


def replace(old,new):
    global source
    assert source.count(old)==1,(old[:100],source.count(old))
    source=source.replace(old,new)


replace('#include <new>','#include <new>\n#include <set>')
replace('namespace slotted_page {','''namespace dirty_compact {
using slotted_page::Page;
using slotted_page::Row;
using slotted_page::compare;
using slotted_page::string_heap;
''')
replace('} // namespace slotted_page','} // namespace dirty_compact')
replace('struct Maintenance {','''struct Maintenance {
    std::uint64_t compact_calls=0,compact_targets=0,dirty_marks=0,dirty_clears=0;
''')
replace('    std::optional<std::size_t> fail_after_pages_;','''    std::optional<std::size_t> fail_after_pages_;
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
    }''')
replace('            auto replacement=staged.begin();std::swap(first->second,replacement->second);staged.erase(replacement);',
        '''            clear_dirty(target);if(merging)clear_dirty(neighbor);
            auto replacement=staged.begin();std::swap(first->second,replacement->second);staged.erase(replacement);''')
replace('''                auto next=std::next(target);
                if(next!=pages_.end()&&rows.size()+next->second.core->size()<=2048)neighbor=next;
                else if(target!=pages_.begin()) {
                    auto prev=std::prev(target);
                    if(rows.size()+prev->second.core->size()<=2048){neighbor=prev;first=prev;}
                }''','''                // Dirty work is consumed in key order. Prefer the earlier page
                // so zero-row results cannot leave separate clean empty islands.
                if(target!=pages_.begin()) {
                    auto prev=std::prev(target);
                    if(rows.size()+prev->second.core->size()<=2048){neighbor=prev;first=prev;}
                }
                if(neighbor==pages_.end()) {
                    auto next=std::next(target);
                    if(next!=pages_.end()&&rows.size()+next->second.core->size()<=2048)neighbor=next;
                }''')
start=source.index('    bool insert(std::string_view key,')
end=source.index('    bool update(std::string_view key,',start)
source=source[:start]+'''    bool insert(std::string_view key,std::uint64_t value) {
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
'''+source[end:]
start=source.index('    bool erase(std::string_view key)')
end=source.index('    std::vector<Row> scan(',start)
source=source[:start]+'''    bool erase(std::string_view key) {
        auto it=route(pages_,key);if(!it->second.core->find(key))return false;
        const bool marked=mark_dirty(it);
        try {
            if(!it->second.core->erase(key))throw std::logic_error("erase changed between seek and commit");
            --size_;++maintenance_.inplace_erases;it->second.version=++generation_;return true;
        }catch(...){if(marked)clear_dirty(it);throw;}
    }
'''+source[end:]
start=source.index('    void compact() {');end=source.index('    void fail_next_rebuild_for_testing',start)
source=source[:start]+'''    void compact() {
        ++maintenance_.compact_calls;
        while(!dirty_.empty()) {
            auto it=*dirty_.begin();++maintenance_.compact_targets;
            rebuild(it,it->second.core->scan("",SIZE_MAX),true);
        }
    }
    void fail_next_dirty_mark_for_testing(){fail_dirty_mark_=true;}
    std::size_t dirty_page_count()const{return dirty_.size();}
'''+source[end:]
replace('std::size_t memory_bytes()const{return sizeof(*this)+route_bytes(pages_);}',
        'std::size_t memory_bytes()const{return sizeof(*this)+route_bytes(pages_)+dirty_.size()*sizeof(Iterator);}')
replace('        if(live!=size_||resident_!=memory_bytes())throw std::logic_error("size/memory");',
        '''        for(const auto it:dirty_)if(pages_.find(it->first)!=it)throw std::logic_error("dirty membership");
        if(live!=size_||resident_!=memory_bytes())throw std::logic_error("size/memory");''')
(HERE/'dirty_compact_index.hpp').write_text(source,encoding='utf-8',newline='\n')
print('generated dirty_compact_index.hpp; frozen F3 Page and point lookup retained')

test=(HERE/'check_slotted_page.cpp').read_text(encoding='utf-8')
test=test.replace('#include "slotted_page_index.hpp"','#include "dirty_compact_index.hpp"')
test=test.replace('using slotted_page::Index;','using dirty_compact::Index;')
test=test.replace('slotted_page::ByteLess','dirty_compact::ByteLess')
test=test.replace('const std::vector<slotted_page::PageState>& before','const std::vector<dirty_compact::PageState>& before')
old='    const auto states=index.page_states();index.fail_next_rebuild_for_testing();failed=false;'
assert test.count(old)==1
test=test.replace(old,'    op(index,ref,2,key(4),0);\n'+old)
extra=r'''
static void dirty_locality() {
    Reference ref;for(std::uint32_t i=0;i<8192;++i)ref.emplace(key(i*4),i);
    Index index(rows(ref));
    for(int i=0;i<10;++i)index.compact();
    require(index.maintenance().rebuilds==0&&index.maintenance().compact_targets==0,"clean compaction visits zero pages");
    const auto fences=index.fence_keys();
    for(std::uint32_t i=0;i<1024;++i)op(index,ref,1,key(i*4),UINT64_MAX);
    require(index.dirty_page_count()==0&&index.fence_keys()==fences,"value updates need no compaction");
    const auto untouched=index.page_states();index.compact();
    require(index.maintenance().rebuilds==0&&index.maintenance().compact_targets==0,"updated clean pages skipped");
    op(index,ref,2,key(4),0);require(index.dirty_page_count()==1,"erase marks exactly one page");
    index.compact();compare(index,ref);
    require(index.maintenance().compact_targets==1&&index.maintenance().rows_rebuilt==2047,"one dirty page bounded compaction");
    require(index.dirty_page_count()==0,"compact clears dirty");
    const auto after=index.page_states();
    for(std::size_t i=1;i<after.size();++i)require(after[i].version==untouched[i].version,"clean neighbor version preserved");
    const auto rebuilt=index.maintenance().rebuilds;index.compact();
    require(index.maintenance().rebuilds==rebuilt&&index.maintenance().compact_targets==1,"compact is idempotent");
    index.fail_next_dirty_mark_for_testing();
    require(!index.erase("missing")&&!index.insert(key(8),1),"no-op writes do not allocate dirty marker");
    op(index,ref,1,key(8),0);const auto state=index.page_states();bool failed=false;
    try{index.erase(key(8));}catch(const std::bad_alloc&){failed=true;}
    require(failed&&index.dirty_page_count()==0,"dirty marker failure before erase");failure(index,ref,state);

    Reference small_ref;for(std::uint32_t i=0;i<1000;++i)small_ref.emplace(key(i*4),i);
    Index small(rows(small_ref));const auto small_state=small.page_states();small.fail_next_dirty_mark_for_testing();failed=false;
    try{small.insert(key(1),0);}catch(const std::bad_alloc&){failed=true;}
    require(failed&&small.dirty_page_count()==0,"dirty marker failure before slot insert");failure(small,small_ref,small_state);
    op(small,small_ref,0,key(1),0);require(small.dirty_page_count()==1,"slot insert marks dirty");
    const auto dirty_state=small.page_states();small.fail_next_rebuild_for_testing(1);failed=false;
    try{small.compact();}catch(const std::bad_alloc&){failed=true;}
    require(failed&&small.dirty_page_count()==1,"failed compact retains old dirty iterator");failure(small,small_ref,dirty_state);
    small.compact();require(small.dirty_page_count()==0,"retry compact clears dirty");compare(small,small_ref);

    Reference merge_ref;for(std::uint32_t i=0;i<4096;++i)merge_ref.emplace(key(i*4),i);
    Index merge(rows(merge_ref));for(std::uint32_t i=0;i<3848;++i)op(merge,merge_ref,2,key(i*4),0);
    require(merge.dirty_page_count()==2,"two adjacent dirty pages");
    const auto merge_state=merge.page_states();merge.fail_next_rebuild_for_testing(1);failed=false;
    try{merge.compact();}catch(const std::bad_alloc&){failed=true;}
    require(failed&&merge.dirty_page_count()==2,"failed adjacent merge retains both iterators");failure(merge,merge_ref,merge_state);
    merge.compact();compare(merge,merge_ref);
    require(merge.dirty_page_count()==0&&merge.page_count()==1&&merge.maintenance().coalesces==1,"merged dirty nodes removed without stale iterator");
    require(merge.maintenance().max_source_pages==2,"neighbor absorption bounded");

    Reference split_ref;for(std::uint32_t i=0;i<2048;++i)split_ref.emplace(key(i*4),i);
    Index split(rows(split_ref));const auto split_state=split.page_states();split.fail_next_rebuild_for_testing(1);failed=false;
    try{split.insert(key(1),0);}catch(const std::bad_alloc&){failed=true;}
    require(failed&&split.dirty_page_count()==0,"failed split rolls back newly allocated dirty marker");failure(split,split_ref,split_state);
    op(split,split_ref,0,key(1),0);require(split.dirty_page_count()==0,"new split pages start clean");compare(split,split_ref);
    std::cout<<"{\"phase\":\"dirty_locality\",\"value_updates\":1024,\"clean_compact_targets\":0,\"one_dirty_rebuilt_rows\":2047"
        <<",\"failure_scenarios\":5,\"adjacent_sources\":"<<merge.maintenance().max_source_pages<<"}\n";
}
'''
assert test.count('int main(int argc,char** argv)')==1
test=test.replace('int main(int argc,char** argv)',extra+'\nint main(int argc,char** argv)')
test=test.replace('if(argc==1){stable_fences();domain();locality();randomized();}',
                  'if(argc==1){dirty_locality();stable_fences();domain();locality();randomized();}')
old='    compare(index,ref);index.compact();compare(index,ref);\n    const auto& m=index.maintenance();'
assert test.count(old)==1
test=test.replace(old,'''    compare(index,ref);const auto dirty_before=index.dirty_page_count();index.compact();compare(index,ref);
    require(index.dirty_page_count()==0,"corpus compact drained dirty");
    const auto& m=index.maintenance();
    require(m.compact_targets<=dirty_before,"compact visits at most initially dirty pages");''')
old='<<",\\\"inplace_updates\\\":"<<m.inplace_updates'
new='<<",\\\"dirty_before_compact\\\":"<<dirty_before<<",\\\"compact_targets\\\":"<<m.compact_targets<<",\\\"inplace_updates\\\":"<<m.inplace_updates'
assert test.count(old)==1
test=test.replace(old,new)
(HERE/'check_dirty_compact.cpp').write_text(test,encoding='utf-8',newline='\n')
print('generated check_dirty_compact.cpp with inherited F3 semantic suite and dirty-locality regressions')

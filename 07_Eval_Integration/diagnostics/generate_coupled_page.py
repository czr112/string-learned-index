"""Derive F5 ownership/maintenance from frozen F4M; retain the F4 leaf verbatim."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent
s=(ROOT/'dirty_compact_index.hpp').read_text(encoding='utf-8')
def replace(old,new):
    global s
    assert s.count(old)==1,old
    s=s.replace(old,new)
replace('#include "slotted_page_core.hpp"','#include "coupled_root.hpp"')
s=s.replace('namespace dirty_compact','namespace coupled_page')
replace('using slotted_page::Page;\nusing slotted_page::Row;\nusing slotted_page::compare;\nusing slotted_page::string_heap;','')
replace('std::uint64_t compact_calls=0,','std::uint64_t root_publications=0,root_prepare_attempts=0,root_prepared=0,root_entries_copied=0,root_key_bytes_copied=0;\n    std::size_t largest_root_entries=0;\n    std::uint64_t compact_calls=0,')
replace('bool fail_dirty_mark_=false;', '''bool fail_dirty_mark_=false;
    std::unique_ptr<FlatRoot> root_;
    std::optional<std::size_t> fail_root_stage_;
    void root_fault(std::size_t stage) {
        if(fail_root_stage_&&stage==*fail_root_stage_){fail_root_stage_.reset();throw std::bad_alloc();}
    }
    std::unique_ptr<FlatRoot> prepare_root(Iterator target,Iterator neighbor,const Pages& staged,std::size_t extra) {
        ++maintenance_.root_prepare_attempts;
        root_fault(0);
        std::vector<Binding> bindings;bindings.reserve(pages_.size()-(target!=pages_.end())-(neighbor!=pages_.end())+staged.size());
        auto old=pages_.begin();auto fresh=staged.begin();
        while(old!=pages_.end()||fresh!=staged.end()) {
            if(old!=pages_.end()&&(old==target||old==neighbor)){++old;continue;}
            if(fresh!=staged.end()&&(old==pages_.end()||compare(fresh->first,old->first)<0)) {
                bindings.push_back({fresh->first,fresh->second.core.get()});++fresh;
            }else{bindings.push_back({old->first,old->second.core.get()});++old;}
        }
        auto next=std::make_unique<FlatRoot>(std::move(bindings));
        ++maintenance_.root_prepared;
        maintenance_.root_entries_copied+=next->bindings().size();
        maintenance_.root_key_bytes_copied+=next->key_bytes();
        maintenance_.largest_root_entries=std::max(maintenance_.largest_root_entries,next->bindings().size());
        peak(extra+next->memory_bytes());
        root_fault(1);return next;
    }''')
replace('const bool merging=neighbor!=pages_.end();','''const bool merging=neighbor!=pages_.end();
            auto next_root=prepare_root(target,neighbor,staged,row_bytes(rows)+added);
            const auto old_root_bytes=root_->memory_bytes(),new_root_bytes=next_root->memory_bytes();
            peak(row_bytes(rows)+added+new_root_bytes);''')
replace('resident_=resident_-removed+added;generation_+=pieces;','''// All allocations are complete; map node transfer and root swap cannot throw.
            root_.swap(next_root);
            resident_=resident_-removed+added-old_root_bytes+new_root_bytes;generation_+=pieces;
            ++maintenance_.root_publications;''')
replace('size_=rows.size();resident_=memory_bytes();peak();','''root_=prepare_root(pages_.end(),pages_.end(),Pages{},sizeof(*this)+route_bytes(pages_));++maintenance_.root_publications;
        size_=rows.size();resident_=memory_bytes();peak();''')
replace('std::optional<std::uint64_t> find(std::string_view key)const{return route(pages_,key)->second.core->find(key);}',
        'std::optional<std::uint64_t> find(std::string_view key)const{return root_->page(key)->find_single(key);}')
replace('void fail_next_dirty_mark_for_testing()', '''void fail_next_root_for_testing(std::size_t stage){if(stage>1)throw std::invalid_argument("root stage");fail_root_stage_=stage;}
    const FlatRoot* root_identity_for_testing()const{return root_.get();}
    std::size_t root_memory_bytes()const{return root_->memory_bytes();}
    void fail_next_dirty_mark_for_testing()''')
replace('return sizeof(*this)+route_bytes(pages_)+dirty_.size()*sizeof(Iterator);',
        'return sizeof(*this)+route_bytes(pages_)+dirty_.size()*sizeof(Iterator)+root_->memory_bytes();')
replace('void check_window(std::string_view key)const{route(pages_,key)->second.core->check_window(key);}', '''void check_window(std::string_view key)const {
        const auto* selected=root_->page(key);
        if(selected!=route(pages_,key)->second.core.get())throw std::logic_error("root selection");
        selected->check_window(key);
    }''')
replace('std::size_t live=0,checks=0;', '''// Establish pointer membership before any borrowed pointer is dereferenced.
        if(!root_||root_->bindings().size()!=pages_.size())throw std::logic_error("root count");
        std::size_t binding=0;
        for(const auto& p:pages_) {
            const auto& b=root_->bindings()[binding++];
            if(b.lower!=p.first||b.page!=p.second.core.get())throw std::logic_error("root membership");
        }
        std::size_t live=0,checks=binding;''')
replace('if(find(r.key)!=r.value)throw std::logic_error("top lookup");',
        'check_window(r.key);if(find(r.key)!=r.value)throw std::logic_error("top lookup");')
(ROOT/'coupled_page_index.hpp').write_text(s,encoding='utf-8',newline='\n')

"""Generate an isolated BM leaf extension; frozen baseline paths stay intact."""
from pathlib import Path

HERE = Path(__file__).resolve().parent

FIELDS = '''
    std::uint64_t fence_bytes=0,family_steps=0,family_encodes=0,family_hits=0;
    std::uint64_t local_calls=0,local_boundary_compares=0,local_prefix_bytes=0;
    std::uint64_t local_prefix_rejects=0,parent_corrections=0;
'''

PRIVATE = r'''
    struct Family {
        std::uint64_t x;
        std::uint32_t first,last;
        blocklearn::BlockModel model;
    };
    std::vector<Family> families_;

    static int tracked_fence(std::string_view a,std::string_view b,Work* w) {
        if(w) {
            std::size_t i=0,n=std::min(a.size(),b.size());
            while(i<n&&a[i]==b[i])++i;
            w->fence_bytes+=i+(i<n);
        }
        return compare(a,b);
    }
    void build_families() {
        if(regions_.empty())return;
        const auto& r=regions_[0];
        struct Span {std::uint64_t x;std::uint32_t first,last;};
        std::vector<Span> spans;
        std::size_t eligible=0;
        for(std::uint32_t first=0;first<r.count;) {
            auto last=first;
            const auto x=blocklearn::encode(boundary(r,first),bm_.prefix_size(),7);
            while(last+1<r.count&&blocklearn::encode(boundary(r,last+1),bm_.prefix_size(),7)==x)++last;
            if(last-first+1>=4&&common_prefix(boundary(r,first),boundary(r,last))>=bm_.prefix_size()+9) {
                spans.push_back({x,first,last});eligible+=last-first+1;
            }
            first=last+1;
        }
        if(eligible*10<r.count)return;
        families_.reserve(spans.size());
        for(const auto& s:spans) {
            std::vector<std::string_view> views;views.reserve(s.last-s.first+1);
            for(auto b=s.first;b<=s.last;++b)views.push_back(boundary(r,b));
            auto model=blocklearn::BlockModel::fit(views,1,7,8,1);
            families_.push_back({s.x,s.first,s.last,std::move(model)});
        }
    }
    template<bool Local> Location locate_refined(std::string_view key,Work* w)const {
        if(families_.empty())return locate_bm<true,false>(key,w);
        const auto& r=regions_[0];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0) {
            if(w)++w->rejected;
            return {LocationState::prefix_rejected,0,0,0,false};
        }
        const auto s=key.substr(r.prefix.size());
        // Encoding skips the parent's model prefix; prove it before using the sparse bounds.
        if(!bm_.accepts_prefix(s))return locate_bm<true,false>(key,w);
        const auto x=blocklearn::encode(s,bm_.prefix_size(),7);
        if(w)++w->family_encodes;
        std::size_t lo=0,hi=families_.size();
        while(lo<hi) {
            if(w)++w->family_steps;
            const auto mid=lo+(hi-lo)/2;
            if(families_[mid].x<x)lo=mid+1;else hi=mid;
        }
        if(lo==families_.size()||families_[lo].x!=x)return locate_bm<true,false>(key,w);
        const auto& f=families_[lo];
        if(w)++w->family_hits;
        const auto floor=f.first?f.first-1:0;
        const Window parent{floor,f.last};
        auto exact_parent=[&]() {
            const auto b=directory_block(r,s,w,parent);
            return Location{LocationState::exact_predecessor,0,b,b+1,true};
        };
        if constexpr(!Local)return exact_parent();
        const auto pref=boundary(r,f.first).substr(0,f.model.prefix_size());
        if(w) {
            std::size_t i=0,n=std::min(pref.size(),s.size());
            while(i<n&&pref[i]==s[i])++i;
            w->local_prefix_bytes+=i+(i<n);
        }
        if(!f.model.accepts_prefix(s)) {
            if(w){++w->local_prefix_rejects;++w->parent_corrections;}
            return exact_parent();
        }
        if(w)++w->local_calls;
        const auto proposed=f.model.window(s);
        if(proposed.first>=proposed.last||proposed.last>f.last-f.first+1)
            throw std::logic_error("invalid local BM proposal");
        const auto begin=proposed.first?f.first+std::uint32_t(proposed.first)-1:floor;
        const auto end=f.first+std::uint32_t(proposed.last);
        auto guard=[&](std::uint32_t b) {
            if(w)++w->local_boundary_compares;
            return tracked_fence(boundary(r,b),s,w);
        };
        if((begin>floor&&guard(begin)>0)||(end<=f.last&&guard(end)<=0)) {
            if(w)++w->parent_corrections;
            return exact_parent();
        }
        const auto b=directory_block(r,s,w,{begin,end-1});
        return {LocationState::exact_predecessor,0,b,b+1,true};
    }
'''

PUBLIC = r'''
    std::optional<std::uint64_t> find_refined(std::string_view k,Work* w=nullptr)const {
        if(w)++w->queries;
        return verify_range(k,locate_refined<true>(k,w),w);
    }
    std::optional<std::uint64_t> find_family_directory(std::string_view k,Work* w=nullptr)const {
        if(w)++w->queries;
        return verify_range(k,locate_refined<false>(k,w),w);
    }
    void check_refinement(std::string_view k)const {
        const auto exact=locate_bm<true,true>(k,nullptr);
        for(bool local:{false,true}) {
            const auto got=local?locate_refined<true>(k,nullptr):locate_refined<false>(k,nullptr);
            validate_location(got);
            if(got.state!=exact.state||got.begin!=exact.begin||got.end!=exact.end)
                throw std::logic_error("local BM excludes exact byte predecessor");
        }
    }
    std::size_t family_count()const{return families_.size();}
    std::size_t family_fences()const {
        std::size_t n=0;for(const auto& f:families_)n+=f.last-f.first+1;return n;
    }
    std::size_t refinement_bytes()const {
        std::size_t bytes=sizeof(families_)+families_.capacity()*sizeof(Family);
        for(const auto& f:families_)bytes+=f.model.owned_bytes();return bytes;
    }
    std::size_t local_model_bytes()const {
        std::size_t bytes=0;for(const auto& f:families_)bytes+=f.model.owned_bytes();return bytes;
    }
    std::size_t refined_memory()const {
        // memory().object already includes the families_ vector object.
        return memory().total()+refinement_bytes()-sizeof(families_);
    }
'''


def rendered():
    text = (HERE / 'bm_page_core.hpp').read_text(encoding='utf-8')
    text = text.replace('namespace bm_leaf', 'namespace bm_local_refine')
    text = text.replace('#include "block_model.h"', '#include "block_model.h"\n#include "key_bytes.h"')
    mark = 'struct Work:static_physical::Work {\n'
    assert text.count(mark) == 1
    text = text.replace(mark, mark + FIELDS)
    mark = '    std::string_view suffix(const Entry& e) const {'
    assert text.count(mark) == 1
    text = text.replace(mark, PRIVATE + '\n' + mark)
    text = text.replace('compare(boundary(r,mid),s)', 'tracked_fence(boundary(r,mid),s,work)')
    text = text.replace('compare(boundary(r,b),suffix)', 'tracked_fence(boundary(r,b),suffix,work)')
    mark = '        bm_=blocklearn::BlockModel::fit(training,1,7,8,1);'
    assert text.count(mark) == 1
    text = text.replace(mark, mark + '\n        build_families();')
    mark = '    // Physical payload identity excludes model metadata, for same-layout controls.'
    assert text.count(mark) == 1
    return text.replace(mark, PUBLIC + '\n' + mark)


if __name__ == '__main__':
    (HERE / 'bm_local_refine_page.hpp').write_text(rendered(), encoding='utf-8', newline='\n')

"""Derive BM leaf/ownership from frozen F4/F5. Upstream BlockModel is linked unchanged."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent
s=(ROOT/'route_attribution_page.hpp').read_text(encoding='utf-8')
def replace(old,new):
    global s
    assert s.count(old)==1,old
    s=s.replace(old,new)
def function(signature):
    start=s.index(signature);begin=s.index('{',start);depth=0
    for end in range(begin,len(s)):
        if s[end]=='{':depth+=1
        if s[end]=='}':
            depth-=1
            if not depth:return s[start:end+1]
    raise AssertionError(signature)
s=s.replace('namespace route_audit_leaf','namespace bm_leaf')
replace('// F3 generated from frozen D1. Model/certificate algorithms are unchanged; see generator.',
        '// BM integration: frozen physical slots and CRC; original upstream BlockModel, byte-fence correction.')
replace('#include "collision_refinement.hpp"','#include "static_physical_index.hpp"\n#include "block_model.h"')
replace('using collision_refinement::Work;', '''struct Work:static_physical::Work {
    std::uint64_t bm_calls=0,bm_raw_blocks=0,bm_proposal_blocks=0,bm_boundary_compares=0,bm_outside=0,bm_local_searches=0;
};''')
replace('collision_refinement::Table refinement_;','''// Its prefix view borrows immutable fence bytes in the reserved arena.
    blocklearn::BlockModel bm_;''')
for signature in ['static Segment certificate(', 'void train(', 'Window model_window_x(', 'Window model_window(']:
    replace(function(signature),'')
replace('train(r);regions_.push_back(std::move(r));','regions_.push_back(std::move(r));')
replace('''refinement_=collision_refinement::Table(regions_.size(),
            [&](std::size_t ri){return regions_[ri].count;},
            [&](std::size_t ri,std::uint32_t b){return boundary(regions_[ri],b);});''', '''std::vector<std::string_view> training;
        if(!regions_.empty())for(std::uint32_t b=0;b<regions_[0].count;++b)training.push_back(boundary(regions_[0],b));
        bm_=blocklearn::BlockModel::fit(training,1,7,8,1);''')
replace('''BuildIdentity build_identity()const { return {"s3-static16-v1","suffix-7x9-v1",
        "s3-local-lcp-collision-gap-v1",region_keys_}; }''', '''Page(const Page&)=delete;Page& operator=(const Page&)=delete;
    Page(Page&&)=delete;Page& operator=(Page&&)=delete;
    BuildIdentity build_identity()const {return {"s3-static16-v1","upstream-BM-7x9-v1","BlockModel-fence-1-7-8-1",region_keys_};}''')
replace(function('template<Lookup Policy> Location locate_blocks('), '''template<Lookup Policy> Location locate_blocks(std::string_view key,Work* work=nullptr)const {
        if constexpr(Policy==Lookup::directory)return locate_bm<true,true>(key,work);
        return locate_bm<true,false>(key,work);
    }''')
replace('std::size_t segment_count()const {std::size_t n=0;for(const auto& r:regions_)n+=r.segments.size();return n;}',
        'std::size_t segment_count()const{return bm_.segment_count();}')
replace('std::size_t refinement_bytes()const{return refinement_.memory();}',
        'bool training_certified()const{return bm_.certified();}')
replace('m.model+=refinement_.memory()-sizeof(refinement_);return m;', 'm.model+=bm_.owned_bytes();return m;')
replace(function('std::size_t verify_numeric_certificate()'), '''std::size_t verify_model_probes()const {
        std::size_t checks=0;
        for(const auto& r:regions_)for(std::uint32_t b=0;b<r.count;++b) {
            const auto k=r.prefix+std::string(boundary(r,b));
            check_window(k);check_window(k+std::string(1,'\\0'));check_window(k+"~");checks+=3;
            if(!k.empty()){check_window(std::string_view(k).substr(0,k.size()-1));++checks;}
        }
        return checks;
    }''')
replace('checks=verify_numeric_certificate()', 'checks=verify_model_probes()')
replace(function('void check_window(std::string_view key)'), '''void check_window(std::string_view key)const {
        if(regions_.empty())return;
        const auto exact=locate_bm<true,true>(key,nullptr);
        for(const bool corrected:{false,true}) {
            const auto found=corrected?locate_bm<true,false>(key,nullptr):locate_bm<false,false>(key,nullptr);
            validate_location(found);
            if(found.state==LocationState::prefix_rejected)continue;
            if(found.begin>exact.begin||found.end<=exact.begin)throw std::logic_error("BM correction excludes byte predecessor");
        }
    }''')
replace(function('template<Lookup Policy> Location locate_blocks_single('), '''template<Lookup Policy> Location locate_blocks_single(std::string_view key,Work* work=nullptr)const {
        return locate_blocks<Policy>(key,work);
    }''')
replace(function('std::optional<std::uint64_t> find_single('), '''std::optional<std::uint64_t> find_single(std::string_view key)const{return find_model(key);}
    std::optional<std::uint64_t> find_base(std::string_view key,Work* work=nullptr)const {
        if(work)++work->queries;
        return verify_range(key,locate_bm<false,false>(key,work),work);
    }
    std::optional<std::uint64_t> find_counted(std::string_view key,Work& work)const{return find_model(key,&work);}
    std::optional<std::uint64_t> find_exact(std::string_view key)const{return find_directory(key);}
    // Physical payload identity excludes model metadata, for same-layout controls.
    std::uint64_t payload_identity()const {
        std::uint64_t h=0;
        for(const auto& r:regions_)for(std::uint32_t b=0;b<r.count;++b) {
            for(const unsigned char c:boundary(r,b))h=h*131+c;h=h*131+blocks_[b].count;
            for(std::uint32_t i=0;i<blocks_[b].count;++i) {
                const auto& e=blocks_[b].entries[i];for(const unsigned char c:suffix(e))h=h*131+c;
                h=h*131+e.value;h=h*131+blocks_[b].tags[i];
            }
        }
        return h;
    }''')
anchor='    static std::size_t unused_anchor_for_generator();'
replace('    // Both strategies enter the same complete-key verifier.', '''    template<bool Correct,bool Directory> Location locate_bm(std::string_view key,Work* work)const {
        if(regions_.empty())return {};
        const auto& r=regions_[0];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0) {
            if(work)++work->rejected;
            return {LocationState::prefix_rejected,0,0,0,false};
        }
        const auto suffix=key.substr(r.prefix.size());
        if constexpr(Directory) {
            const auto b=directory_block(r,suffix,work);
            return {LocationState::exact_predecessor,0,b,b+1,true};
        }else {
            // An upstream training radius is a proposal, not an absent-key certificate.
            if(work)++work->bm_calls;
            const auto proposed=bm_.window(suffix);
            if(proposed.first>=proposed.last||proposed.last>r.count)throw std::logic_error("invalid upstream window");
            const auto lo=std::uint32_t(proposed.first?proposed.first-1:0),hi=std::uint32_t(proposed.last);
            if(work){work->bm_raw_blocks+=proposed.last-proposed.first;work->bm_proposal_blocks+=hi-lo;}
            const auto boundary_compare=[&](std::uint32_t b) {
                if(work)++work->bm_boundary_compares;
                return compare(boundary(r,b),suffix);
            };
            const bool too_left=lo&&boundary_compare(lo)>0;
            const bool too_right=hi<r.count&&boundary_compare(hi)<=0;
            if(too_left||too_right) {
                if(work){++work->bm_outside;++work->fallback;}
                // Search only the proven complementary side; base remains a full-directory control.
                std::uint32_t b;
                if constexpr(Correct)b=directory_block(r,suffix,work,too_left?Window{0,lo-1}:Window{hi,r.count-1});
                else b=directory_block(r,suffix,work);
                return {LocationState::exact_predecessor,0,b,b+1,true};
            }
            if constexpr(Correct) {
                if(work)++work->bm_local_searches;
                const auto b=directory_block(r,suffix,work,{lo,hi-1});
                return {LocationState::exact_predecessor,0,b,b+1,true};
            }else {
                return {LocationState::candidates,0,lo,hi,true};
            }
        }
    }
    // Both strategies enter the same complete-key verifier.''')
assert 'refinement_' not in s and 'model_window_x' not in s and 'slope=' not in s
s='\n'.join(line.rstrip() for line in s.splitlines())+'\n'
(ROOT/'bm_page_core.hpp').write_text(s,encoding='utf-8',newline='\n')
s=(ROOT/'coupled_root.hpp').read_text(encoding='utf-8').replace('"route_attribution_page.hpp"','"bm_page_core.hpp"').replace('route_audit_leaf::','bm_leaf::').replace('namespace coupled_page','namespace bm_page')
(ROOT/'bm_page_root.hpp').write_text(s,encoding='utf-8',newline='\n')
s=(ROOT/'coupled_page_index.hpp').read_text(encoding='utf-8').replace('"coupled_root.hpp"','"bm_page_root.hpp"').replace('namespace coupled_page','namespace bm_page')
needle='    bool insert(std::string_view key,std::uint64_t value)'
assert s.count(needle)==1
s=s.replace(needle,'''    std::optional<std::uint64_t> find_base(std::string_view key,bm_leaf::Work* work=nullptr)const{return root_->page(key)->find_base(key,work);}
    std::optional<std::uint64_t> find_counted(std::string_view key,bm_leaf::Work& work)const{return root_->page(key)->find_counted(key,work);}
    std::optional<std::uint64_t> find_exact(std::string_view key)const{return root_->page(key)->find_exact(key);}
    std::size_t bm_segment_count()const {std::size_t n=0;for(const auto& p:pages_)n+=p.second.core->segment_count();return n;}
    std::size_t bm_certified_pages()const {std::size_t n=0;for(const auto& p:pages_)n+=p.second.core->training_certified();return n;}
''' +needle)
(ROOT/'bm_page_index.hpp').write_text(s,encoding='utf-8',newline='\n')
# Reuse the full F5 regression, with frozen F5 itself as an independent implementation control.
s=(ROOT/'check_coupled_page.cpp').read_text(encoding='utf-8')
s=s.replace('#include "coupled_page_index.hpp"','#include "bm_page_index.hpp"\n#include "coupled_page_index.hpp"')
s=s.replace('using coupled_page::Index;','using bm_page::Index;').replace('using route_audit_leaf::Page;','using bm_leaf::Page;')
s=s.replace('vector<coupled_page::PageState>','vector<bm_page::PageState>').replace('dirty_compact::Index','coupled_page::Index')
s=s.replace('static void corpus(const char* path)', '''static void bm_work(Index& index,const Reference& ref) {
    bm_leaf::Work base,opt;std::size_t queries=0;
    for(const auto& r:ref)for(const bool absent:{false,true}) {
        auto k=r.first;if(absent)k+="~bm/miss";
        const auto at=ref.find(k);const auto value=at==ref.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{at->second};
        require(index.find_base(k,&base)==value&&index.find_counted(k,opt)==value&&index.find_exact(k)==value,"BM raw/optimized/directory/map");++queries;
    }
    require(base.bm_calls>0&&opt.bm_calls==base.bm_calls,"original BM used by both arms");
    require(base.bm_calls+base.rejected==queries&&opt.bm_calls+opt.rejected==queries,"no silent self-model fallback");
    require(opt.blocks<=base.blocks,"one-block verification bounded by base window scan");
    std::cout<<"{\\"phase\\":\\"bm_work\\",\\"queries\\":"<<queries<<",\\"bm_calls\\":"<<opt.bm_calls
        <<",\\"segments\\":"<<index.bm_segment_count()<<",\\"training_certified_pages\\":"<<index.bm_certified_pages()
        <<",\\"base_blocks\\":"<<base.blocks<<",\\"optimized_blocks\\":"<<opt.blocks
        <<",\\"base_directory_steps\\":"<<base.directory_steps<<",\\"optimized_directory_steps\\":"<<opt.directory_steps
        <<",\\"boundary_compares\\":"<<opt.bm_boundary_compares<<",\\"outside_proposals\\":"<<opt.bm_outside<<"}\\n";
}
static void corpus(const char* path)''')
s=s.replace('initial.clear();initial.shrink_to_fit();compare(index,ref);compare(control,ref);','initial.clear();initial.shrink_to_fit();compare(index,ref);compare(control,ref);bm_work(index,ref);')
(ROOT/'check_bm_page.cpp').write_text(s,encoding='utf-8',newline='\n')

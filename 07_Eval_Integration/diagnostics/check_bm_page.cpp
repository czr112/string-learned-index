#include "bm_page_index.hpp"
#include "coupled_page_index.hpp"
#include "dirty_compact_index.hpp"
#include "collision_refined_index.hpp"
#include "static_input.hpp"
#include <iostream>
#include <random>

using bm_page::Index;
using bm_leaf::Page;
using slotted_page::Row;
using Reference=std::map<std::string,std::uint64_t,dirty_compact::ByteLess>;
static std::uint64_t checks=0,operations=0;
static void require(bool condition,const char* message){++checks;if(!condition)throw std::logic_error(message);}
static std::string key(std::uint32_t n) {
    std::string out="k/";for(int shift=24;shift>=0;shift-=8)out.push_back(static_cast<char>((n>>shift)&255));return out;
}
static std::vector<Row> rows(const Reference& ref){std::vector<Row> out;for(const auto& r:ref)out.push_back({r.first,r.second});return out;}
template<class Core> static void scan(const Core& index,const Reference& ref,std::string_view lower,std::size_t limit) {
    const auto found=index.scan(lower,limit);auto it=ref.lower_bound(lower);std::size_t i=0;
    while(it!=ref.end()&&i<limit) {
        require(i<found.size(),"scan too short");require(found[i].key==it->first&&found[i].value==it->second,"scan mismatch");++it;++i;
    }
    require(i==found.size(),"scan too long");
}
template<class Core> static void compare(const Core& index,const Reference& ref) {
    require(index.size()==ref.size(),"size");checks+=index.verify();
    for(const auto& r:ref)require(index.find(r.first)==r.second,"map find");
    scan(index,ref,"",SIZE_MAX);scan(index,ref,"",0);scan(index,ref,key(1000),33);
}
static void op(Index& index,Reference& ref,int kind,const std::string& k,std::uint64_t value,coupled_page::Index* mirror=nullptr) {
    const auto old=ref.find(k);bool expected=false,actual=false;
    if(kind==0){expected=ref.emplace(k,value).second;actual=index.insert(k,value);}
    else if(kind==1){expected=old!=ref.end();if(expected)old->second=value;actual=index.update(k,value);}
    else{expected=old!=ref.end();if(expected)ref.erase(old);actual=index.erase(k);}
    if(mirror) {
        const auto result=kind==0?mirror->insert(k,value):kind==1?mirror->update(k,value):mirror->erase(k);
        require(result==actual&&mirror->find(k)==index.find(k),"F4M write equivalence");
    }
    require(actual==expected,"write result");const auto now=ref.find(k);
    require(index.find(k)==(now==ref.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{now->second}),"write value");++operations;
}
static void stable_fences() {
    Reference ref;for(std::uint32_t i=0;i<1500;++i)ref.emplace(key(i*4),i);
    Page page(rows(ref));const auto fences=page.fence_keys();compare(page,ref);
    require(page.try_insert(key(1),0)==Page::Insert::inserted,"spare slot insert");ref.emplace(key(1),0);
    require(page.try_insert(key(2),999)==Page::Insert::rebuild,"full block requires rebuild");
    require(page.update(key(0),UINT64_MAX),"inplace zero update");ref[key(0)]=UINT64_MAX;
    // Remove a complete block, including the record that originally supplied its fence.
    for(std::uint32_t i=15;i<30;++i){require(page.erase(key(i*4)),"empty block erase");ref.erase(key(i*4));}
    require(page.try_insert(key(61),UINT64_MAX)==Page::Insert::inserted,"deleted fence interval insert");ref.emplace(key(61),UINT64_MAX);
    require(page.fence_keys()==fences,"stable fences after writes");compare(page,ref);
    for(std::uint32_t i=0;i<65536;++i) {
        const auto k=key(i);page.check_window(k);const auto f=ref.find(k);
        require(page.find(k)==(f==ref.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{f->second}),"exhaustive gap query");
    }
    require(page.try_insert("",123)==Page::Insert::rebuild,"prefix expansion rebuild");
    Page empty(std::vector<Row>{});require(empty.try_insert("",0)==Page::Insert::rebuild,"empty page rebuild");
}
static void domain() {
    Index index;Reference ref;
    const std::vector<std::string> keys={"",std::string("\0",1),"a",std::string("a\0",2),"aa",
        std::string(1,char(128)),std::string(1,char(255)),std::string(4096,'p')+"a",std::string(4096,'p')+"b"};
    for(std::size_t i=0;i<keys.size();++i)op(index,ref,0,keys[i],i%2?0:UINT64_MAX);
    compare(index,ref);auto owned=index.scan("",SIZE_MAX);const auto expected=rows(ref);
    for(const auto& k:keys){op(index,ref,0,k,99);op(index,ref,1,k,0);op(index,ref,2,k,0);op(index,ref,2,k,0);op(index,ref,1,k,99);}
    index.compact();compare(index,ref);require(index.page_count()==1,"empty sentinel");
    for(std::size_t i=0;i<owned.size();++i)require(owned[i].key==expected[i].key&&owned[i].value==expected[i].value,"owning scan lifetime");
    op(index,ref,0,std::string(12000,'x'),123);compare(index,ref);
    bool invalid=false;try{Index bad(std::vector<Row>{{"b",1},{"a",0}});}catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"unsorted input");invalid=false;
    try{Index bad(std::vector<Row>{{"a",1},{"a",0}});}catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"duplicate input");
}
static void failure(Index& index,const Reference& ref,const std::vector<bm_page::PageState>& before) {
    compare(index,ref);const auto after=index.page_states();require(after.size()==before.size(),"failure route count");
    for(std::size_t i=0;i<before.size();++i)
        require(before[i].lower==after[i].lower&&before[i].version==after[i].version&&before[i].rows==after[i].rows,"failure state");
}
static void locality() {
    Reference ref;for(std::uint32_t i=0;i<8192;++i)ref.emplace(key(i*4),i);
    Index index(rows(ref));const auto before=index.page_states();index.fail_next_rebuild_for_testing(1);bool failed=false;
    try{index.insert(key(1),777);}catch(const std::bad_alloc&){failed=true;}
    require(failed,"partial split failure");failure(index,ref,before);
    op(index,ref,0,key(1),777);require(index.maintenance().rows_rebuilt==2049,"split bounded work");
    const auto after=index.page_states();
    for(std::size_t i=1;i<before.size();++i) {
        const auto same=std::find_if(after.begin(),after.end(),[&](const auto& p){return p.lower==before[i].lower;});
        require(same!=after.end()&&same->version==before[i].version,"unaffected page version");
    }
    op(index,ref,2,key(4),0);
    const auto states=index.page_states();index.fail_next_rebuild_for_testing();failed=false;
    try{index.compact();}catch(const std::bad_alloc&){failed=true;}
    require(failed,"compact failure");failure(index,ref,states);
    auto erase_keys=rows(ref);auto owned=index.scan(key(1990),3000);const auto expected=owned;
    for(std::size_t i=0;i<erase_keys.size();++i){op(index,ref,2,erase_keys[i].key,0);if(i%997==0)compare(index,ref);}
    index.compact();compare(index,ref);require(index.page_count()==1,"empty pages coalesced");
    require(index.maintenance().coalesces>0&&index.maintenance().max_source_pages==2,"neighbor merge");
    require(index.maintenance().max_rebuild_rows<=2049,"bounded rows");
    for(std::size_t i=0;i<owned.size();++i)require(owned[i].key==expected[i].key&&owned[i].value==expected[i].value,"scan survives all delete");
    // Failure when one spare slot was consumed and a second insertion needs rebuilding.
    Reference small_ref;for(std::uint32_t i=0;i<1000;++i)small_ref.emplace(key(i*4),i);
    Index small(rows(small_ref));op(small,small_ref,0,key(1),0);
    const auto full=small.page_states();small.fail_next_rebuild_for_testing();failed=false;
    try{small.insert(key(2),999);}catch(const std::bad_alloc&){failed=true;}
    require(failed,"full-block failure");failure(small,small_ref,full);op(small,small_ref,0,key(2),999);compare(small,small_ref);
    std::cout<<"{\"phase\":\"locality\",\"splits\":"<<index.maintenance().splits<<",\"coalesces\":"<<index.maintenance().coalesces
        <<",\"failed_rebuilds\":"<<index.maintenance().failed_rebuilds+small.maintenance().failed_rebuilds
        <<",\"max_source_pages\":"<<index.maintenance().max_source_pages<<",\"max_rebuild_rows\":"<<index.maintenance().max_rebuild_rows<<"}\n";
}
static void randomized() {
    Index index;coupled_page::Index mirror;Reference ref;std::mt19937_64 rng(0xf320261018ULL);
    for(std::uint32_t i=0;i<14000;++i) {
        auto k=key(std::uint32_t(rng()%12000));if(i%7==0)k=std::string(64,'p')+k;
        if(i%31==0)k=std::string("\0\x80\xff",3)+k;
        op(index,ref,int(rng()%3),k,i%11?rng():i%2?0:UINT64_MAX,&mirror);
        if(i%197==0)compare(index,ref);if(i%19==0)scan(index,ref,k,77);
    }
    index.compact();mirror.compact();compare(index,ref);compare(mirror,ref);
    Index growth;Reference growing;
    for(std::uint32_t i=0;i<10000;++i)op(growth,growing,0,key(i),i);
    compare(growth,growing);require(growth.maintenance().splits>=7,"continuous splits");
    for(std::uint32_t i=0;i<10000;i+=389)scan(growth,growing,key(i),3000);
}
static void bm_work(Index& index,const Reference& ref) {
    bm_leaf::Work base,opt;std::size_t queries=0;
    for(const auto& r:ref)for(const bool absent:{false,true}) {
        auto k=r.first;if(absent)k+="~bm/miss";
        const auto at=ref.find(k);const auto value=at==ref.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{at->second};
        require(index.find_base(k,&base)==value&&index.find_counted(k,opt)==value&&index.find_exact(k)==value,"BM raw/optimized/directory/map");++queries;
    }
    require(base.bm_calls>0&&opt.bm_calls==base.bm_calls,"original BM used by both arms");
    require(base.bm_calls+base.rejected==queries&&opt.bm_calls+opt.rejected==queries,"no silent self-model fallback");
    require(opt.blocks<=base.blocks,"one-block verification bounded by base window scan");
    std::cout<<"{\"phase\":\"bm_work\",\"queries\":"<<queries<<",\"bm_calls\":"<<opt.bm_calls
        <<",\"segments\":"<<index.bm_segment_count()<<",\"training_certified_pages\":"<<index.bm_certified_pages()
        <<",\"base_blocks\":"<<base.blocks<<",\"optimized_blocks\":"<<opt.blocks
        <<",\"base_directory_steps\":"<<base.directory_steps<<",\"optimized_directory_steps\":"<<opt.directory_steps
        <<",\"boundary_compares\":"<<opt.bm_boundary_compares<<",\"outside_proposals\":"<<opt.bm_outside<<"}\n";
}
static void corpus(const char* path) {
    auto stream=static_blocks::buffered(path);const auto loaded=string_index_bench::unified::read_rows(stream);
    Reference ref;for(const auto& r:loaded)ref.emplace(r.key,r.value);
    require(!loaded.empty()&&ref.size()==loaded.size(),"corpus unique");
    auto initial=rows(ref);Index index(initial),control(initial,16);coupled_page::Index mirror(initial);const collision_refined::Index d1(initial);
    const auto live_bytes=index.memory_bytes(),control_bytes=control.memory_bytes(),d1_bytes=d1.memory().total();
    initial.clear();initial.shrink_to_fit();compare(index,ref);compare(control,ref);bm_work(index,ref);
    const auto fences=index.fence_keys();std::mt19937_64 rng(0xf3c0ffeeULL);
    for(std::uint32_t i=0;i<1024;++i)op(index,ref,1,loaded[std::size_t(rng()%loaded.size())].key,i%2?0:UINT64_MAX,&mirror);
    require(index.fence_keys()==fences&&index.maintenance().rebuilds==0,"value update preserves all fences");
    for(std::uint32_t i=0;i<1600;++i) {
        auto k=loaded[std::size_t(rng()%(i%2?loaded.size():std::min<std::size_t>(2048,loaded.size())))].key;
        if(i%4==3)k+="~f3/"+key(i);
        op(index,ref,i%4==0?1:i%4==1?2:0,k,i%2?0:UINT64_MAX,&mirror);
        index.check_window(k);if(i%79==0)scan(index,ref,k,129);
    }
    compare(index,ref);compare(mirror,ref);const auto dirty_before=index.dirty_page_count();index.compact();mirror.compact();compare(index,ref);compare(mirror,ref);
    require(index.dirty_page_count()==0,"corpus compact drained dirty");
    const auto& m=index.maintenance();
    require(m.compact_targets<=dirty_before,"compact visits at most initially dirty pages");require(m.max_source_pages<=2&&m.max_rebuild_rows<=2049,"corpus locality");
    std::cout<<"{\"phase\":\"corpus\",\"rows\":"<<loaded.size()<<",\"initial_slotted_bytes\":"<<live_bytes
        <<",\"initial_control_bytes\":"<<control_bytes<<",\"initial_d1_bytes\":"<<d1_bytes
        <<",\"dirty_before_compact\":"<<dirty_before<<",\"compact_targets\":"<<m.compact_targets<<",\"inplace_updates\":"<<m.inplace_updates<<",\"inplace_inserts\":"<<m.inplace_inserts
        <<",\"inplace_erases\":"<<m.inplace_erases<<",\"rebuilds\":"<<m.rebuilds<<",\"splits\":"<<m.splits
        <<",\"max_source_pages\":"<<m.max_source_pages<<",\"max_rebuild_rows\":"<<m.max_rebuild_rows
        <<",\"root_publications\":"<<m.root_publications<<",\"root_prepared\":"<<m.root_prepared
        <<",\"root_entries_copied\":"<<m.root_entries_copied<<",\"root_key_bytes_copied\":"<<m.root_key_bytes_copied
        <<",\"root_bytes\":"<<index.root_memory_bytes()<<",\"largest_root_entries\":"<<m.largest_root_entries
        <<",\"accounted_peak_bytes\":"<<m.accounted_peak_bytes<<"}\n";
}

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


static void root_publication() {
    std::size_t failures=0;
    for(std::size_t stage=0;stage<2;++stage)for(int scenario=0;scenario<4;++scenario) {
        Reference ref;const std::uint32_t n=scenario==0?4096:scenario==3?4096:1000;
        for(std::uint32_t i=0;i<n;++i)ref.emplace(key(i*4),i);
        Index index(rows(ref));coupled_page::Index mirror(rows(ref));
        if(scenario==1)op(index,ref,0,key(1),0,&mirror);
        if(scenario==2)op(index,ref,2,key(4),0,&mirror);
        if(scenario==3)for(std::uint32_t i=0;i<3848;++i)op(index,ref,2,key(i*4),0,&mirror);
        const auto states=index.page_states();const auto* root=index.root_identity_for_testing();
        const auto dirty=index.dirty_page_count(),pub=index.maintenance().root_publications;
        const auto prepared=index.maintenance().root_prepared,entries=index.maintenance().root_entries_copied;
        index.fail_next_root_for_testing(stage);bool failed=false;
        try{if(scenario<2)index.insert(key(scenario?2:1),777);else index.compact();}
        catch(const std::bad_alloc&){failed=true;}
        require(failed,"root preparation fault reached");++failures;
        require(index.root_identity_for_testing()==root&&index.maintenance().root_publications==pub,"old root survives failure");
        require(index.dirty_page_count()==dirty,"root failure retains dirty membership");
        require(index.maintenance().root_prepared==prepared+stage,"prepared root counted on aborted publication");
        require((index.maintenance().root_entries_copied>entries)==(stage==1),"aborted root copies accounted");
        failure(index,ref,states);compare(mirror,ref);
        if(scenario<2)op(index,ref,0,key(scenario?2:1),777,&mirror);else{index.compact();mirror.compact();}
        compare(index,ref);compare(mirror,ref);
        require(index.root_identity_for_testing()!=root&&index.maintenance().root_publications==pub+1,"replacement publishes new root");
        require(index.maintenance().root_publications==index.maintenance().rebuilds+1,"every replaced page set publishes root");
    }
    Reference ref;for(std::uint32_t i=0;i<1000;++i)ref.emplace(key(i*4),i);
    Index stable(rows(ref));const auto* root=stable.root_identity_for_testing();
    op(stable,ref,1,key(0),0);op(stable,ref,0,key(1),UINT64_MAX);op(stable,ref,2,key(4),0);
    require(root==stable.root_identity_for_testing()&&stable.maintenance().root_publications==1,"inplace writes preserve root");compare(stable,ref);
    // All two-byte strings probe root boundaries, prefix expansion, NUL/high bytes and digest ties.
    Reference binary;
    for(std::uint32_t i=0;i<65536;i+=8)binary.emplace(std::string{char(i>>8),char(i&255)},i);
    binary.emplace("",0);binary.emplace(std::string(1,char(255)),UINT64_MAX);
    Index index(rows(binary));coupled_page::Index mirror(rows(binary));
    for(std::uint32_t i=0;i<65536;++i) {
        const std::string k{char(i>>8),char(i&255)};const auto at=binary.find(k);
        const auto value=at==binary.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{at->second};
        index.check_window(k);require(index.find(k)==value&&mirror.find(k)==value,"full byte root oracle");
    }
    for(const auto& r:rows(binary))op(index,binary,2,r.key,0,&mirror);
    index.compact();mirror.compact();compare(index,binary);compare(mirror,binary);
    require(index.page_count()==1,"full byte delete leaves single root sentinel");
    std::cout<<"{\"phase\":\"root_publication\",\"failure_scenarios\":"<<failures
        <<",\"binary_queries\":65536,\"inplace_root_publications\":1,\"final_pages\":"<<index.page_count()<<"}\n";
}

int main(int argc,char** argv) {
    try {
        if(argc==1){root_publication();dirty_locality();stable_fences();domain();locality();randomized();}
        else if(argc==2)corpus(argv[1]);else throw std::invalid_argument("optional rows.bin");
        std::cout<<"{\"phase\":\"complete\",\"status\":\"passed\",\"checks\":"<<checks<<",\"operations\":"<<operations<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

"""Carry frozen F4M regressions forward and add root publication checks."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent
s=(ROOT/'check_dirty_compact.cpp').read_text(encoding='utf-8')
s=s.replace('#include "dirty_compact_index.hpp"','#include "coupled_page_index.hpp"\n#include "dirty_compact_index.hpp"')
s=s.replace('using dirty_compact::Index;', 'using coupled_page::Index;').replace('using slotted_page::Page;','using route_audit_leaf::Page;')
s=s.replace('vector<dirty_compact::PageState>','vector<coupled_page::PageState>')
s=s.replace('std::uint64_t value) {\n    const auto old=ref.find(k);','std::uint64_t value,dirty_compact::Index* mirror=nullptr) {\n    const auto old=ref.find(k);')
s=s.replace('require(actual==expected,"write result");', '''if(mirror) {
        const auto result=kind==0?mirror->insert(k,value):kind==1?mirror->update(k,value):mirror->erase(k);
        require(result==actual&&mirror->find(k)==index.find(k),"F4M write equivalence");
    }
    require(actual==expected,"write result");''')
s=s.replace('Index index;Reference ref;std::mt19937_64 rng','Index index;dirty_compact::Index mirror;Reference ref;std::mt19937_64 rng')
s=s.replace('i%11?rng():i%2?0:UINT64_MAX);','i%11?rng():i%2?0:UINT64_MAX,&mirror);')
s=s.replace('index.compact();compare(index,ref);\n    Index growth;', 'index.compact();mirror.compact();compare(index,ref);compare(mirror,ref);\n    Index growth;')
s=s.replace('Index index(initial),control(initial,16);','Index index(initial),control(initial,16);dirty_compact::Index mirror(initial);')
s=s.replace('i%2?0:UINT64_MAX);\n    require(index.fence_keys()', 'i%2?0:UINT64_MAX,&mirror);\n    require(index.fence_keys()')
s=s.replace('op(index,ref,i%4==0?1:i%4==1?2:0,k,i%2?0:UINT64_MAX);','op(index,ref,i%4==0?1:i%4==1?2:0,k,i%2?0:UINT64_MAX,&mirror);')
s=s.replace('compare(index,ref);const auto dirty_before=index.dirty_page_count();index.compact();compare(index,ref);',
            'compare(index,ref);compare(mirror,ref);const auto dirty_before=index.dirty_page_count();index.compact();mirror.compact();compare(index,ref);compare(mirror,ref);')
s=s.replace('<<",\\\"accounted_peak_bytes\\\":"<<m.accounted_peak_bytes', '''<<",\\\"root_publications\\\":"<<m.root_publications<<",\\\"root_prepared\\\":"<<m.root_prepared
        <<",\\\"root_entries_copied\\\":"<<m.root_entries_copied<<",\\\"root_key_bytes_copied\\\":"<<m.root_key_bytes_copied
        <<",\\\"root_bytes\\\":"<<index.root_memory_bytes()<<",\\\"largest_root_entries\\\":"<<m.largest_root_entries
        <<",\\\"accounted_peak_bytes\\\":"<<m.accounted_peak_bytes''')
extra=r'''
static void root_publication() {
    std::size_t failures=0;
    for(std::size_t stage=0;stage<2;++stage)for(int scenario=0;scenario<4;++scenario) {
        Reference ref;const std::uint32_t n=scenario==0?4096:scenario==3?4096:1000;
        for(std::uint32_t i=0;i<n;++i)ref.emplace(key(i*4),i);
        Index index(rows(ref));dirty_compact::Index mirror(rows(ref));
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
    Index index(rows(binary));dirty_compact::Index mirror(rows(binary));
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
'''
s=s.replace('int main(int argc,char** argv)',extra+'\nint main(int argc,char** argv)')
s=s.replace('if(argc==1){dirty_locality();','if(argc==1){root_publication();dirty_locality();')
s=s.replace('\\\\\\"root_', '\\\"root_').replace('\\\\\\"largest_root_entries', '\\\"largest_root_entries')
# Match the existing C++ JSON string escaping on every generated telemetry field.
s=s.replace('\\\\\\"', '\\\"')
(ROOT/'check_coupled_page.cpp').write_text(s,encoding='utf-8',newline='\n')

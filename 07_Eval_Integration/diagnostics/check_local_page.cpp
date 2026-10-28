#include "local_page_index.hpp"
#include "static_input.hpp"
#include <iostream>
#include <random>
#include <sstream>

using local_page::Index;
using local_page::Row;
using Reference=std::map<std::string,std::uint64_t,local_page::ByteLess>;
static std::uint64_t checks=0,operations=0;
static void require(bool condition,const char* message) {
    ++checks;if(!condition)throw std::logic_error(message);
}
static std::string key(std::uint32_t n) {
    std::string out="key/";
    for(int shift=24;shift>=0;shift-=8)out.push_back(static_cast<char>((n>>shift)&255));
    return out;
}
static std::vector<Row> rows(const Reference& ref) {
    std::vector<Row> result;for(const auto& r:ref)result.push_back({r.first,r.second});return result;
}
static void scan(const Index& index,const Reference& ref,std::string_view lower,std::size_t limit) {
    const auto got=index.scan(lower,limit);auto it=ref.lower_bound(lower);
    std::size_t n=0;
    while(it!=ref.end()&&n<limit) {
        require(n<got.size(),"scan short");
        require(got[n].key==it->first&&got[n].value==it->second,"scan result");++n;++it;
    }
    require(n==got.size(),"scan long");
}
static void compare(const Index& index,const Reference& ref) {
    require(index.size()==ref.size(),"size");checks+=index.verify();
    for(const auto& r:ref)require(index.find(r.first)==r.second,"find reference");
    scan(index,ref,"",SIZE_MAX);scan(index,ref,"",0);
    scan(index,ref,key(2000),129);scan(index,ref,std::string(1,char(255)),13);
}
static void op(Index& index,Reference& ref,int kind,const std::string& k,std::uint64_t value) {
    bool expected=false,got=false;auto it=ref.find(k);
    if(kind==0) {
        expected=ref.emplace(k,value).second;got=index.insert(k,value);
    } else if(kind==1) {
        expected=it!=ref.end();if(expected)it->second=value;got=index.update(k,value);
    } else {
        expected=it!=ref.end();if(expected)ref.erase(it);got=index.erase(k);
    }
    require(got==expected,"write result");
    const auto found=ref.find(k);
    require(index.find(k)==(found==ref.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{found->second}),"post-write value");
    ++operations;
}
static void rollback_check(Index& index,const Reference& ref,const std::vector<local_page::PageState>& before) {
    compare(index,ref);const auto after=index.page_states();
    require(after.size()==before.size(),"failed write route count");
    for(std::size_t i=0;i<before.size();++i) {
        require(after[i].lower==before[i].lower&&after[i].version==before[i].version,"failed write route/version");
        require(after[i].delta_entries==before[i].delta_entries&&after[i].delta_bytes==before[i].delta_bytes,"failed write delta");
    }
}
static void domain() {
    Reference ref;Index index;
    const std::vector<std::string> keys={"",std::string("\0",1),std::string("a\0",2),"a","aa","aaa",
        std::string(1,char(128)),std::string(1,char(255)),std::string(4096,'p')+"a",std::string(4096,'p')+"b"};
    for(std::size_t i=0;i<keys.size();++i)op(index,ref,0,keys[i],i%2?UINT64_MAX:0);
    compare(index,ref);
    auto owned=index.scan("",100);const auto expected_owned=rows(ref);
    for(const auto& k:keys) {
        op(index,ref,0,k,123);op(index,ref,1,k,0);op(index,ref,2,k,0);op(index,ref,2,k,0);
        op(index,ref,1,k,123);op(index,ref,0,k,UINT64_MAX);
    }
    compare(index,ref);
    for(const auto& k:keys)op(index,ref,2,k,0);
    compare(index,ref);require(index.page_count()==1,"empty sentinel");
    require(owned.size()==keys.size(),"owning scan size");
    for(std::size_t i=0;i<owned.size();++i)
        require(owned[i].key==expected_owned[i].key&&owned[i].value==expected_owned[i].value,"owning scan accessible");
    // A very long delta key must synchronously rebuild even a small base.
    op(index,ref,0,std::string(12000,'z'),0);compare(index,ref);
    require(index.page_states()[0].delta_entries==0,"oversize delta compacted");
    bool invalid=false;
    try {Index bad(std::vector<Row>{{"b",1},{"a",2}});}catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"reject unsorted");
    invalid=false;
    try {Index bad(std::vector<Row>{{"a",1},{"a",2}});}catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"reject duplicates");
}
static void locality_and_failure() {
    Reference ref;for(std::uint32_t i=0;i<8192;++i)ref.emplace(key(i*2),i);
    Index index(rows(ref));compare(index,ref);
    const auto before=index.page_states();const auto baseline=index.maintenance();
    index.fail_next_rebuild_for_testing(1);bool failed=false;
    try {index.insert(key(1),999);}catch(const std::bad_alloc&){failed=true;}
    require(failed,"partial split failure injected");rollback_check(index,ref,before);
    op(index,ref,0,key(1),999);compare(index,ref);
    require(index.page_count()==5,"split creates one route");
    const auto split=index.page_states();
    for(std::size_t i=1;i<before.size();++i) {
        auto found=std::find_if(split.begin(),split.end(),[&](const auto& p){return p.lower==before[i].lower;});
        require(found!=split.end()&&found->version==before[i].version,"unaffected page preserved");
    }
    require(index.maintenance().rows_rebuilt-baseline.rows_rebuilt==2049,"one-page split work");
    // A failed explicit compaction retains the already-committed delta as well.
    op(index,ref,1,key(4),UINT64_MAX);
    const auto dirty=index.page_states();index.fail_next_rebuild_for_testing();failed=false;
    try {index.compact();}catch(const std::bad_alloc&){failed=true;}
    require(failed,"compaction failure injected");rollback_check(index,ref,dirty);
    index.compact();compare(index,ref);
    // Hit whichever registered budget comes first: bytes or entry count.
    index.fail_next_rebuild_for_testing();failed=false;
    for(std::uint32_t i=0;i<200&&!failed;++i) {
        const auto at_limit=index.page_states();
        try {require(index.update(key(i*2),777),"budget update existing");ref[key(i*2)]=777;}
        catch(const std::bad_alloc&) {
            failed=true;rollback_check(index,ref,at_limit);
            op(index,ref,1,key(i*2),777);
        }
    }
    require(failed,"budget-triggered write failure injected");compare(index,ref);
    // Insert below an existing boundary, remove its minimum, and reinsert it.
    const auto boundary=index.page_states().back().lower;
    op(index,ref,2,boundary,0);op(index,ref,0,boundary,0);compare(index,ref);
    auto owned=index.scan(key(1990),2050);const auto expected_owned=owned;
    // Concentrated erases exercise adjacent coalescing, including the sentinel page.
    std::vector<std::string> erase_keys;for(const auto& r:ref)erase_keys.push_back(r.first);
    for(std::size_t i=0;i<erase_keys.size();++i) {
        op(index,ref,2,erase_keys[i],0);if(i%509==0)compare(index,ref);
    }
    index.compact();compare(index,ref);
    require(index.page_count()==1,"all pages coalesced to sentinel");
    require(index.maintenance().coalesces>0,"coalescing exercised");
    require(index.maintenance().max_source_pages<=2,"bounded source pages");
    require(index.maintenance().max_rebuild_rows<=2049,"bounded rebuild rows");
    require(owned.size()==expected_owned.size(),"owning scan survives rebuild");
    for(std::size_t i=0;i<owned.size();++i)
        require(owned[i].key==expected_owned[i].key&&owned[i].value==expected_owned[i].value,"owning scan survives deletion");
    std::cout<<"{\"phase\":\"locality\",\"rebuild_events\":"<<index.maintenance().rebuild_events
        <<",\"splits\":"<<index.maintenance().splits<<",\"coalesces\":"<<index.maintenance().coalesces
        <<",\"failed_rebuilds\":"<<index.maintenance().failed_rebuilds
        <<",\"rows_rebuilt\":"<<index.maintenance().rows_rebuilt
        <<",\"max_source_pages\":"<<index.maintenance().max_source_pages
        <<",\"max_rebuild_rows\":"<<index.maintenance().max_rebuild_rows
        <<",\"accounted_peak_bytes\":"<<index.maintenance().accounted_peak_bytes<<"}\n";
}
static void randomized() {
    Reference ref;Index index;std::mt19937_64 rng(0xf120261018ULL);
    for(std::uint32_t i=0;i<12000;++i) {
        auto k=key(std::uint32_t(rng()%10000));
        if(i%7==0)k=std::string(64,'p')+k;
        if(i%31==0)k=std::string("\0\x80\xff",3)+k;
        const auto value=i%11? rng() : i%2?UINT64_MAX:0;
        op(index,ref,int(rng()%3),k,value);
        if(i%197==0)compare(index,ref);
        if(i%19==0)scan(index,ref,k,std::size_t(rng()%100));
    }
    index.compact();compare(index,ref);
}
static void repeated_splits() {
    Reference ref;Index index;
    for(std::uint32_t i=0;i<10000;++i)op(index,ref,0,key(i),i);
    compare(index,ref);require(index.maintenance().splits>=7,"continuous growth splits");
    // Verify arbitrary gap lower_bounds and cross-page limits after growth.
    for(std::uint32_t i=0;i<10000;i+=389)scan(index,ref,key(i),3000);
}
static void corpus(const char* path) {
    auto input=static_blocks::buffered(path);
    const auto loaded=string_index_bench::unified::read_rows(input);
    require(!loaded.empty(),"nonempty corpus");
    Reference ref;for(const auto& r:loaded)ref.emplace(r.key,r.value);
    auto owned_rows=rows(ref);Index index(owned_rows);
    owned_rows.clear();owned_rows.shrink_to_fit();compare(index,ref);
    const auto initial_bytes=index.memory().total();
    std::mt19937_64 rng(0xf1c0ffeeULL);
    for(std::uint32_t i=0;i<1600;++i) {
        auto k=loaded[std::size_t(rng()%(i%2?loaded.size():std::min<std::size_t>(2048,loaded.size())))].key;
        const auto value=i%2?UINT64_MAX:0;
        if(i%4==3)k+="~f1/"+key(i);
        op(index,ref,i%4==0?1:i%4==1?2:0,k,value);
        if(i%79==0)scan(index,ref,k,129);
    }
    compare(index,ref);const auto dirty=index.memory();index.compact();compare(index,ref);
    require(index.maintenance().max_source_pages<=2,"corpus source locality");
    require(index.maintenance().max_rebuild_rows<=2049,"corpus rebuild bound");
    std::cout<<"{\"phase\":\"corpus\",\"input_rows\":"<<loaded.size()<<",\"final_rows\":"<<index.size()
        <<",\"pages\":"<<index.page_count()<<",\"initial_accounted_bytes\":"<<initial_bytes
        <<",\"dirty_base_bytes\":"<<dirty.bases<<",\"dirty_delta_bytes\":"<<dirty.deltas
        <<",\"rebuild_events\":"<<index.maintenance().rebuild_events
        <<",\"splits\":"<<index.maintenance().splits<<",\"coalesces\":"<<index.maintenance().coalesces
        <<",\"rows_rebuilt\":"<<index.maintenance().rows_rebuilt
        <<",\"key_value_bytes_rebuilt\":"<<index.maintenance().key_value_bytes_rebuilt
        <<",\"delta_entries_copied\":"<<index.maintenance().delta_entries_copied
        <<",\"max_source_pages\":"<<index.maintenance().max_source_pages
        <<",\"max_rebuild_rows\":"<<index.maintenance().max_rebuild_rows
        <<",\"accounted_peak_bytes\":"<<index.maintenance().accounted_peak_bytes<<"}\n";
}
int main(int argc,char** argv) {
    try {
        if(argc==2)corpus(argv[1]);
        else if(argc==1){domain();locality_and_failure();randomized();repeated_splits();}
        else throw std::invalid_argument("optional rows.bin");
        std::cout<<"{\"phase\":\"complete\",\"checks\":"<<checks<<",\"operations\":"<<operations
            <<",\"status\":\"passed\"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

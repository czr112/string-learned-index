// Isolated one-level refinement by the upstream BM. Counts only, not a timing driver.
#include "bm_local_refine_page.hpp"
#include "bm_page_core.hpp"
#include "static_input.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <vector>

static bool count_new=false;
static std::uint64_t query_news=0;
void* operator new(std::size_t n) {
    if(count_new)++query_news;
    if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t align) {
    if(count_new)++query_news;
    void* p=nullptr;if(posix_memalign(&p,std::size_t(align),n?n:1))throw std::bad_alloc();return p;
}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}

namespace uni=string_index_bench::unified;
using Row=static_physical::Row;
using Page=bm_local_refine::Page;
using Work=bm_local_refine::Work;

static std::uint64_t checks=0;
static std::optional<std::uint64_t> expected(const std::map<std::string,std::uint64_t>& ref,std::string_view k) {
    auto it=ref.find(std::string(k));return it==ref.end()?std::optional<std::uint64_t>{}:it->second;
}
static void check_key(const bm_leaf::Page& frozen,const Page& next,
                      const std::map<std::string,std::uint64_t>& ref,std::string_view k) {
    const auto want=expected(ref,k);
    if(frozen.find_model(k)!=want||frozen.find_base(k)!=want||frozen.find_exact(k)!=want||
       next.find_model(k)!=want||next.find_family_directory(k)!=want)
        throw std::logic_error("baseline/family directory disagrees with std::map");
    count_new=true;
    const auto got=next.find_refined(k);
    count_new=false;
    if(got!=want)throw std::logic_error("local BM disagrees with std::map");
    next.check_refinement(k);++checks;
}
static void scan_check(const Page& page,const std::map<std::string,std::uint64_t>& ref,
                      std::string_view lower,std::size_t limit) {
    auto got=page.scan(lower,limit);auto it=ref.lower_bound(std::string(lower));
    std::size_t n=0;
    for(const auto& row:got) {
        if(it==ref.end()||row.key!=it->first||row.value!=it->second)throw std::logic_error("scan mismatch");
        ++it;++n;
    }
    if(n>limit||(n<limit&&it!=ref.end()))throw std::logic_error("scan length");++checks;
}
static void fixture() {
    std::vector<std::vector<Row>> sets{{},{{"",0}},{{"",UINT64_MAX},{std::string(1,'\0'),0},
        {std::string(1,char(0x80)),17},{std::string(1,char(0xff)),UINT64_MAX}}};
    std::map<std::string,std::uint64_t> generated;
    for(unsigned i=0;i<1600;++i) {
        std::string key;
        key+=char('a'+i/400);
        key+=std::string(9,'p');key+=std::string(80,'x');
        key+=char(i/256);key+=char(i%256);
        generated.emplace(key,i%2?UINT64_MAX:i);
    }
    generated.emplace("",0);generated.emplace(std::string(4096,'z'),UINT64_MAX);
    std::vector<Row> long_rows;for(const auto& [k,v]:generated)long_rows.push_back({k,v});
    sets.push_back(std::move(long_rows));
    std::size_t fixture_families=0;
    for(const auto& rows:sets) {
        bm_leaf::Page frozen(rows,15);Page next(rows,15);
        std::map<std::string,std::uint64_t> ref;for(const auto& r:rows)ref.emplace(r.key,r.value);
        fixture_families+=next.family_count();
        std::vector<std::string> probes{"",std::string(1,'\0'),std::string(4096,'x'),std::string(1,char(0xff))};
        for(const auto& r:rows) {
            check_key(frozen,next,ref,r.key);
            probes.push_back(r.key+std::string(1,'\0'));probes.push_back(r.key+"~");
            if(!r.key.empty()) {
                probes.push_back(r.key.substr(0,r.key.size()-1));
                auto mutated=r.key;mutated[0]=char(static_cast<unsigned char>(mutated[0])^0x80);
                probes.push_back(std::move(mutated));
            }
        }
        for(const auto& k:probes)check_key(frozen,next,ref,k);
        scan_check(next,ref,"",rows.size()+1);scan_check(next,ref,std::string(1,'\0'),7);
        if(next.payload_identity()!=frozen.payload_identity())throw std::logic_error("fixture payload drift");
    }
    if(!fixture_families)throw std::logic_error("fixtures did not exercise local BM");
}
static void work_json(const Work& w) {
    std::cout<<"{\"queries\":"<<w.queries
        <<",\"fence_compares\":"<<w.bm_boundary_compares+w.directory_steps+w.local_boundary_compares
        <<",\"fence_bytes\":"<<w.fence_bytes<<",\"blocks\":"<<w.blocks<<",\"key_compares\":"<<w.exact
        <<",\"parent_bm_calls\":"<<w.bm_calls<<",\"local_bm_calls\":"<<w.local_calls
        <<",\"family_encodes\":"<<w.family_encodes<<",\"family_steps\":"<<w.family_steps
        <<",\"family_hits\":"<<w.family_hits<<",\"local_prefix_bytes\":"<<w.local_prefix_bytes
        <<",\"local_prefix_rejects\":"<<w.local_prefix_rejects
        <<",\"parent_corrections\":"<<w.parent_corrections<<"}";
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin queries.bin");
        fixture();const auto fixture_checks=checks;
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty input");
        std::map<std::string,std::uint64_t> global;
        for(const auto& r:rows)global.emplace(r.key,r.value);
        for(std::size_t i=0;i<queries.size();++i)if(i%10==9) {
            queries[i].key+="~f2/miss";while(global.count(queries[i].key))queries[i].key+="~";
        }
        struct Chunk {
            std::string lower;
            std::unique_ptr<bm_leaf::Page> frozen;
            std::unique_ptr<Page> next;
            std::map<std::string,std::uint64_t> ref;
        };
        std::vector<Chunk> chunks;
        std::size_t baseline_bytes=0,new_bytes=0,added_bytes=0,model_bytes=0,families=0,family_fences=0,useful_pages=0;
        for(std::size_t first=0;first<rows.size();first+=2048) {
            const auto last=std::min(first+2048,rows.size());
            std::vector<Row> part;for(auto i=first;i<last;++i)part.push_back({rows[i].key,rows[i].value});
            Chunk c;c.lower=first?rows[first].key:std::string();
            for(const auto& r:part)c.ref.emplace(r.key,r.value);
            c.frozen=std::make_unique<bm_leaf::Page>(part,15);c.next=std::make_unique<Page>(part,15);
            if(c.next->payload_identity()!=c.frozen->payload_identity())throw std::logic_error("payload drift");
            baseline_bytes+=c.frozen->memory().total();new_bytes+=c.next->refined_memory();
            added_bytes+=c.next->refinement_bytes();model_bytes+=c.next->local_model_bytes();
            families+=c.next->family_count();family_fences+=c.next->family_fences();
            useful_pages+=c.next->family_count()!=0;
            for(const auto& r:part)check_key(*c.frozen,*c.next,c.ref,r.key);
            for(const auto& k:c.frozen->fence_keys()) {
                check_key(*c.frozen,*c.next,c.ref,k);
                check_key(*c.frozen,*c.next,c.ref,k+std::string(1,'\0'));
                check_key(*c.frozen,*c.next,c.ref,k+"~");
                if(!k.empty())check_key(*c.frozen,*c.next,c.ref,k.substr(0,k.size()-1));
            }
            scan_check(*c.next,c.ref,"",part.size()+1);
            scan_check(*c.next,c.ref,part.front().key,7);
            chunks.push_back(std::move(c));
        }
        if(new_bytes-baseline_bytes!=added_bytes)throw std::logic_error("capacity ledger mismatch");
        auto route=[&](std::string_view k) {
            std::size_t lo=0,hi=chunks.size();
            while(lo<hi){auto mid=lo+(hi-lo)/2;if(static_physical::compare(chunks[mid].lower,k)<=0)lo=mid+1;else hi=mid;}
            return lo?lo-1:0;
        };
        Work current,refined,family,base,dir;
        bm_leaf::Work frozen_work;
        for(const auto& q:queries) {
            auto& c=chunks[route(q.key)];check_key(*c.frozen,*c.next,c.ref,q.key);
            c.frozen->find_model(q.key,&frozen_work);
            c.next->find_model(q.key,&current);c.next->find_base(q.key,&base);c.next->find_directory(q.key,&dir);
            c.next->find_refined(q.key,&refined);c.next->find_family_directory(q.key,&family);
        }
        if(current.bm_boundary_compares!=frozen_work.bm_boundary_compares||current.directory_steps!=frozen_work.directory_steps||
           current.blocks!=frozen_work.blocks||current.exact!=frozen_work.exact||current.bm_calls!=frozen_work.bm_calls)
            throw std::logic_error("frozen control work drift");
        if(query_news)throw std::logic_error("uninstrumented find allocated");
        std::cout<<std::setprecision(17)<<"{\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"pages\":"<<chunks.size()<<",\"checks\":"<<checks<<",\"fixture_checks\":"<<fixture_checks
            <<",\"mismatch\":0,\"query_new_calls\":"<<query_news
            <<",\"memory\":{\"baseline\":"<<baseline_bytes<<",\"candidate\":"<<new_bytes
            <<",\"added\":"<<added_bytes<<",\"local_model_segments\":"<<model_bytes<<"}"
            <<",\"families\":"<<families<<",\"family_fences\":"<<family_fences<<",\"useful_pages\":"<<useful_pages;
        std::cout<<",\"current\":";work_json(current);std::cout<<",\"refined\":";work_json(refined);
        std::cout<<",\"family_directory\":";work_json(family);std::cout<<",\"base\":";work_json(base);
        std::cout<<",\"directory\":";work_json(dir);std::cout<<"}\n";
    }catch(const std::exception& e){count_new=false;std::cerr<<e.what()<<'\n';return 1;}
}

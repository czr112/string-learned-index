#include "collision_refinement.hpp"
#include "navigation_entry_index.hpp"
#include "static_input.hpp"
#include <iostream>

namespace cr=collision_refinement;
namespace ne=navigation_entry;
namespace uni=string_index_bench::unified;
using Row=string_index_bench::v1::Row;
static std::uint64_t checks=0;
static void require(bool ok,const char* message){++checks;if(!ok)throw std::logic_error(message);}
static int bytes(std::string_view a,std::string_view b) {
    for(std::size_t i=0;i<std::min(a.size(),b.size());++i) {
        const auto x=static_cast<unsigned char>(a[i]),y=static_cast<unsigned char>(b[i]);if(x!=y)return x<y?-1:1;
    }
    return a.size()<b.size()?-1:a.size()>b.size()?1:0;
}
struct Ledger {
    std::uint64_t queries=0,covered=0,narrow=0,corrections=0,old_blocks=0,new_blocks=0;
    std::uint64_t old_windows=0,new_windows=0,family_steps=0,local_steps=0,prefix_bytes=0,directory_steps=0,directory_bytes=0;
};
static void probe(const ne::Index& core,const cr::Table& table,const std::vector<Row>& rows,std::size_t chunk,
    std::string_view q,Ledger* ledger=nullptr) {
    const auto it=std::lower_bound(rows.begin(),rows.end(),q,[](const Row& r,std::string_view k){return bytes(r.key,k)<0;});
    const bool hit=it!=rows.end()&&bytes(it->key,q)==0;
    const auto want=hit?std::optional<std::uint64_t>(it->value):std::nullopt;
    ne::Work old;require(core.find_model(q,&old)==want,"B row oracle");
    const auto original=core.locate_blocks<ne::Lookup::learned>(q);
    if(ledger)++ledger->queries;
    if(rows.empty()||!original.prefix_verified){require(old.blocks==0,"rejected query entered block");return;}
    const auto after=std::upper_bound(rows.begin(),rows.end(),q,[](std::string_view k,const Row& r){return bytes(k,r.key)<0;});
    const auto predecessor=after==rows.begin()?0:std::size_t(after-rows.begin()-1),ri=predecessor/chunk;
    require(original.region==ri,"region oracle");const auto expected=static_cast<std::uint32_t>((predecessor-ri*chunk)/16);
    const auto suffix=q.substr(core.region(ri).prefix.size());cr::Work work;
    const auto result=table.locate(ri,suffix,static_physical::coordinate(suffix),&work);
    auto begin=original.begin,end=original.end;std::uint64_t dir_steps=0,dir_bytes=0;
    if(result.used) {
        begin=result.range.lo;end=result.range.hi+1;
        if(result.correction) {
            std::uint32_t lo=begin,hi=end;
            while(lo<hi) {
                ++dir_steps;const auto mid=lo+(hi-lo)/2;
                const auto fence=std::string_view(rows[ri*chunk+mid*16].key).substr(core.region(ri).prefix.size());
                std::size_t i=0;while(i<std::min(fence.size(),suffix.size())&&fence[i]==suffix[i])++i;
                dir_bytes+=i+(i<std::min(fence.size(),suffix.size()));
                if(bytes(fence,suffix)<=0)lo=mid+1;else hi=mid;
            }
            begin=lo>begin?lo-1:begin;end=begin+1;
        }
    }
    require(begin<=expected&&expected<end&&end<=core.region(ri).count,"local range excludes byte predecessor");
    const auto old_entered=hit?expected-original.begin+1:original.end-original.begin;
    require(old_entered==old.blocks,"old entered-block formula does not match actual find");
    if(!ledger)return;
    ledger->covered+=work.covered;ledger->narrow+=work.local_narrow;ledger->corrections+=work.parent_corrections;
    ledger->old_blocks+=old.blocks;ledger->new_blocks+=hit?expected-begin+1:end-begin;
    ledger->old_windows+=original.end-original.begin;ledger->new_windows+=end-begin;
    ledger->family_steps+=work.family_steps;ledger->local_steps+=work.local_steps;ledger->prefix_bytes+=work.prefix_bytes;
    ledger->directory_steps+=dir_steps;ledger->directory_bytes+=dir_bytes;
}
static void dataset(const std::vector<Row>& rows,const std::vector<uni::Query>& queries,std::size_t chunk,bool exhaustive,Ledger& ledger) {
    auto input=rows;ne::Index core(input,chunk);
    const auto count=[&](std::size_t r){return core.region(r).count;};
    const auto fence=[&](std::size_t r,std::uint32_t b){return std::string_view(input[r*chunk+b*16].key).substr(core.region(r).prefix.size());};
    cr::Table table(core.region_count(),count,fence);std::vector<Row>().swap(input);
    checks+=table.verify_numeric([&](std::size_t r,std::uint32_t b){return std::string_view(rows[r*chunk+b*16].key).substr(core.region(r).prefix.size());});
    for(std::size_t i=0;i<rows.size();++i) {
        const auto& q=rows[i].key;probe(core,table,rows,chunk,q);
        if(exhaustive||i%16==0) {
            probe(core,table,rows,chunk,q+std::string(1,'\0'));probe(core,table,rows,chunk,q+std::string(1,char(255)));
            if(!q.empty())probe(core,table,rows,chunk,std::string_view(q).substr(0,q.size()-1));
        }
    }
    if(exhaustive)for(unsigned i=0;i<65536;++i) {std::string q;q.push_back(char(i>>8));q.push_back(char(i));probe(core,table,rows,chunk,q);}
    for(const auto& q:queries) {
        probe(core,table,rows,chunk,q.key,&ledger);
        const auto r=core.find_model(q.key);require(r.has_value()==q.expected_present&&(!r||*r==q.expected_value),"frozen query oracle");
    }
}
static void domain() {
    Ledger unused;
    for(const auto& rows:std::vector<std::vector<Row>>{{},{{"",0}},{{std::string(7,char(255)),UINT64_MAX}}})dataset(rows,{},2048,false,unused);
    std::vector<Row> rows{{"",0},{"a",1},{std::string("a\0",2),2},{std::string(1,char(128)),3},
        {std::string(1,char(255)),4},{std::string(4096,'z'),UINT64_MAX}};
    for(unsigned i=0;i<2000;++i) {
        std::string q="collision/"+std::string(64,'x');q.push_back(char(i>>8));q.push_back(char(i));rows.push_back({std::move(q),i+10});
        q="fallback/"+std::string(1,i<1000?'a':'b')+std::string(64,'x');q.push_back(char(i>>8));q.push_back(char(i));rows.push_back({std::move(q),i+3010});
    }
    rows.push_back({"fallback/",UINT64_MAX});rows.push_back({"fallback.",0});
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return bytes(a.key,b.key)<0;});
    for(auto chunk:{17u,64u,2048u})dataset(rows,{},chunk,true,unused);
}
int main(int argc,char** argv) {
    try {
        if(argc==1){domain();std::cout<<"{\"domain_checks\":"<<checks<<"}\n";return 0;}
        if(argc!=3)throw std::invalid_argument("[rows.bin queries.bin]");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);Ledger ledger;
        dataset(rows,queries,2048,false,ledger);ne::Index core(rows);
        cr::Table table(core.region_count(),[&](std::size_t r){return core.region(r).count;},
            [&](std::size_t r,std::uint32_t b){return std::string_view(rows[r*2048+b*16].key).substr(core.region(r).prefix.size());});
        std::size_t families=0,segments=0,covered_fences=0;std::vector<std::size_t> prefixes;
        for(const auto& fs:table.regions())for(const auto& f:fs){++families;segments+=f.segments.size();covered_fences+=f.last-f.first+1;prefixes.push_back(f.prefix.size());}
        std::sort(prefixes.begin(),prefixes.end());
        std::cout<<"{\"checks\":"<<checks<<",\"rows\":"<<rows.size()<<",\"queries\":"<<ledger.queries
            <<",\"core_bytes\":"<<core.memory().total()<<",\"extra_bytes\":"<<table.memory()<<",\"families\":"<<families
            <<",\"local_segments\":"<<segments<<",\"covered_fences\":"<<covered_fences
            <<",\"prefix_p50\":"<<(prefixes.empty()?0:prefixes[(prefixes.size()-1)/2])
            <<",\"covered\":"<<ledger.covered<<",\"local_narrow\":"<<ledger.narrow<<",\"parent_corrections\":"<<ledger.corrections
            <<",\"old_blocks\":"<<ledger.old_blocks<<",\"new_blocks\":"<<ledger.new_blocks
            <<",\"old_windows\":"<<ledger.old_windows<<",\"new_windows\":"<<ledger.new_windows
            <<",\"family_steps\":"<<ledger.family_steps<<",\"local_steps\":"<<ledger.local_steps
            <<",\"prefix_bytes\":"<<ledger.prefix_bytes<<",\"directory_steps\":"<<ledger.directory_steps
            <<",\"directory_bytes\":"<<ledger.directory_bytes<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

#include "static_physical_index.hpp"
#include "static_physical_blocks.hpp"
#include "../adapters/our_static/our_static_adapter.h"
#include "unified/runner.h"
#include "static_input.hpp"
#include <iostream>
namespace sib=string_index_bench::v1;
namespace unified=string_index_bench::unified;
using Row=sib::Row;
static std::size_t checks=0,scans=0;
static void require(bool okay,const char* what) {++checks;if(!okay)throw std::logic_error(what);}
static auto lower(const std::vector<Row>& rows,std::string_view key) {
    return std::lower_bound(rows.begin(),rows.end(),key,[](const Row& r,std::string_view k){return static_physical::compare(r.key,k)<0;});
}
template<class Rows> static void check_scan(const Rows& got,const std::vector<Row>& rows,std::string_view key,std::size_t limit) {
    const auto begin=lower(rows,key);const auto n=std::min<std::size_t>(rows.end()-begin,limit);
    require(got.size()==n,"scan length mismatch");
    for(std::size_t i=0;i<n;++i)require(got[i].key==begin[i].key&&got[i].value==begin[i].value,"scan row mismatch");
    ++scans;
}
static void probe(const static_physical::Index& core,const static_blocks::Index& old,const std::vector<Row>& rows,std::string_view key,bool scan) {
    const auto it=lower(rows,key);const std::optional<std::uint64_t> expected=it!=rows.end()&&it->key==key?std::optional<std::uint64_t>(it->value):std::nullopt;
    core.check_window(key);
    require(core.find_model(key)==expected,"learned lookup mismatch");
    require(core.find_directory(key)==expected,"directory lookup mismatch");
    require(core.find_hybrid(key)==expected,"hybrid lookup mismatch");
    require(old.find_model(key)==expected,"old prototype mismatch");
    if(scan)check_scan(core.scan(key,17),rows,key,17);
}
static void dataset(const std::vector<Row>& rows,const std::vector<unified::Query>& queries,bool exhaustive) {
    for(auto chunk:exhaustive?std::vector<unsigned>{64,2048}:std::vector<unsigned>{2048}) {
        auto input=rows;static_physical::Index core(input,chunk);static_blocks::Index old(input,chunk);std::vector<Row>().swap(input);
        checks+=core.verify_numeric_certificate();
        require(core.memory().total()==old.memory().total(),"integration changed core resident layout");
        for(std::size_t i=0;i<rows.size();++i) {
            probe(core,old,rows,rows[i].key,i%16==0);
            if(exhaustive||i%16==0) {
                const auto& key=rows[i].key;probe(core,old,rows,key+std::string(1,'\0'),true);
                probe(core,old,rows,key+std::string(1,char(255)),true);
                if(!key.empty())probe(core,old,rows,std::string_view(key).substr(0,key.size()-1),true);
            }
        }
        for(const auto& q:queries) {
            probe(core,old,rows,q.key,true);const auto it=lower(rows,q.key);
            require((it!=rows.end()&&it->key==q.key)==q.expected_present,"query presence oracle mismatch");
            if(q.expected_present)require(it->value==q.expected_value,"query value oracle mismatch");
        }
        if(exhaustive)for(unsigned i=0;i<65536;++i) {
            std::string k;k.push_back(char(i>>8));k.push_back(char(i));probe(core,old,rows,k,true);
        }
        for(const auto& key:std::vector<std::string>{"",std::string(1,char(255)),"before",std::string(4096,char(255))})
            for(auto limit:std::vector<std::size_t>{0,1,16,17,64,SIZE_MAX})check_scan(core.scan(key,limit),rows,key,limit);
        check_scan(core.scan("",SIZE_MAX),rows,"",SIZE_MAX);
    }
    // Actual public factories: all policies, input release, lifecycle and result ownership.
    for(const auto& policy:{"learned","directory","wide_directory"}) {
        auto adapter=make_our_static_index({{"lookup",policy}});
        bool threw=false;try{adapter->find("");}catch(const std::logic_error&){threw=true;}
        require(threw,"query before build accepted");
        try{adapter->bulk_load({{"b",1},{"a",2}});require(false,"invalid build accepted");}catch(const std::invalid_argument&){}
        require(adapter->memory_usage().index_owned_bytes==std::optional<std::uint64_t>(0),"failed build published state");
        auto input=rows;adapter->bulk_load(input);std::vector<Row>().swap(input);
        for(const auto& row:rows)require(adapter->find(row.key)==std::optional<std::uint64_t>(row.value),"factory lookup mismatch");
        threw=false;try{adapter->bulk_load({});}catch(const std::logic_error&){threw=true;}require(threw,"second successful build accepted");
        auto survivors=adapter->scan("",SIZE_MAX);adapter.reset();check_scan(survivors,rows,"",SIZE_MAX);
    }
}
int main(int argc,char** argv) {
    try {
        for(const auto& rows:std::vector<std::vector<Row>>{{},{{"",0}},{{"only",UINT64_MAX}}})dataset(rows,{},false);
        std::vector<Row> domain{{"",0},{"a",1},{std::string("a\0",2),2},{std::string(1,char(128)),3},{std::string(1,char(255)),4},{std::string(4096,'z'),UINT64_MAX}};
        for(unsigned i=0;i<2000;++i) {
            std::string k="collision/"+std::string(64,'x');k.push_back(char(i>>8));k.push_back(char(i));domain.push_back({std::move(k),i+10});
        }
        for(unsigned i=0;i<300;++i) {
            std::string k;k.push_back(char((i*193)%256));k.push_back(char((i*71)%256));k.push_back(char(i>>8));domain.push_back({std::move(k),i+3000});
        }
        std::sort(domain.begin(),domain.end(),[](const Row& a,const Row& b){return static_physical::compare(a.key,b.key)<0;});
        domain.erase(std::unique(domain.begin(),domain.end(),[](const Row& a,const Row& b){return a.key==b.key;}),domain.end());
        dataset(domain,{},true);const auto domain_checks=checks;
        if(argc!=3)throw std::invalid_argument("rows.bin queries.bin");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        auto rows=unified::read_rows(rs);const auto queries=unified::read_queries(qs);dataset(rows,queries,false);
        static_physical::Index core(rows);static_physical::Work learned,hybrid;
        for(const auto& q:queries){core.find_model(q.key,&learned);core.find_hybrid(q.key,&hybrid);}
        require(learned.wide==hybrid.fallback,"wide-window fallback coverage mismatch");
        std::cout<<"{\"checks\":"<<checks<<",\"domain_checks\":"<<domain_checks<<",\"scans\":"<<scans<<",\"rows\":"<<rows.size()
            <<",\"queries\":"<<queries.size()<<",\"learned_blocks\":"<<learned.blocks<<",\"hybrid_blocks\":"<<hybrid.blocks
            <<",\"fallback_queries\":"<<hybrid.fallback<<",\"directory_steps\":"<<hybrid.directory_steps<<",\"core_bytes\":"<<core.memory().total()<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

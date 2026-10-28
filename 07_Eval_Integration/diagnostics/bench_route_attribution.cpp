#include "route_attribution.hpp"
#include "slotted_page_index.hpp"
#include "static_input.hpp"
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>

using route_audit::Row;
using Reference=std::map<std::string,std::uint64_t,route_audit::ByteLess>;
static volatile std::uint64_t sink=0;
static std::uint64_t checks=0;
static void require(bool ok,const char* message){++checks;if(!ok)throw std::logic_error(message);}
static std::uint64_t token(std::optional<std::uint64_t> value){return value?*value^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);}
static std::string key(std::uint32_t n){std::string s="r/";for(int i=24;i>=0;i-=8)s.push_back(static_cast<char>((n>>i)&255));return s;}
static std::vector<Row> rows_of(const Reference& map){std::vector<Row> rows;for(const auto& r:map)rows.push_back({r.first,r.second});return rows;}
static const route_audit::Page* oracle(const std::vector<route_audit::Index::Binding>& bindings,std::string_view key) {
    auto it=std::upper_bound(bindings.begin(),bindings.end(),key,[](std::string_view k,const auto& b){return route_audit::compare(k,b.lower)<0;});
    return (it==bindings.begin()?it:std::prev(it))->page;
}
static void check_key(const route_audit::Index& index,const route_audit::FlatRoute& flat,
                      const std::vector<route_audit::Index::Binding>& bindings,const slotted_page::Index& frozen,
                      std::string_view key,std::optional<std::uint64_t> expected) {
    const auto* tree=index.tree_page(key);const auto* target=oracle(bindings,key);
    require(tree==target&&flat.page(key)==target,"root selection");
    const auto a=tree->locate_blocks<route_audit_leaf::Lookup::learned>(key);
    const auto b=tree->locate_blocks_single<route_audit_leaf::Lookup::learned>(key);
    require(a.state==b.state&&a.region==b.region&&a.begin==b.begin&&a.end==b.end&&a.prefix_verified==b.prefix_verified,"single-page certificate");
    require(route_audit::find<false,false>(index,flat,key)==expected,"tree wrapped answer");
    require(route_audit::find<false,true>(index,flat,key)==expected,"tree single answer");
    require(route_audit::find<true,false>(index,flat,key)==expected,"flat wrapped answer");
    require(route_audit::find<true,true>(index,flat,key)==expected,"flat single answer");
    require(frozen.find(key)==expected,"frozen F3 answer");
    tree->check_window(key);
}
static void domain_case(const Reference& reference) {
    auto rows=rows_of(reference);route_audit::Index index(rows);slotted_page::Index frozen(rows);
    const auto bindings=index.bindings();const route_audit::FlatRoute flat(bindings);
    require(sizeof(route_audit::Index)==sizeof(slotted_page::Index),"Index object shape");
    require(sizeof(route_audit::Page)==sizeof(slotted_page::Page),"Page object shape");
    checks+=index.verify();
    auto probe=[&](std::string_view key){const auto it=reference.find(key);check_key(index,flat,bindings,frozen,key,it==reference.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{it->second});};
    for(const auto& row:rows)probe(row.key);
    const std::vector<std::string> special={"",std::string("\0",1),"a",std::string("a\0",2),std::string(1,char(128)),
        std::string(1,char(255)),std::string(4096,'p'),"shared","shared/","before","zzzz"};
    for(const auto& k:special)probe(k);
    for(std::uint32_t i=0;i<65536;++i){std::string k;k.push_back(char(i>>8));k.push_back(char(i&255));probe(k);}
    for(const auto& boundary:bindings) {
        probe(boundary.lower);probe(boundary.lower+std::string(1,'\0'));
        if(!boundary.lower.empty())probe(std::string_view(boundary.lower).substr(0,boundary.lower.size()-1));
    }
    const auto got=index.scan("",SIZE_MAX);require(got.size()==rows.size(),"scan count");
    for(std::size_t i=0;i<rows.size();++i)require(got[i].key==rows[i].key&&got[i].value==rows[i].value,"scan rows");
}
static void domain() {
    domain_case({});domain_case({{"",0}});
    Reference ordinary;for(std::uint32_t i=0;i<7000;++i)ordinary.emplace(key(i*4),i%2?0:UINT64_MAX);domain_case(ordinary);
    Reference shared;for(std::uint32_t i=0;i<7000;++i)shared.emplace("shared/"+std::string(80,'p')+key(i*4),i);domain_case(shared);
    Reference collisions;
    for(std::uint32_t i=0;i<8192;++i)collisions.emplace((i<4096?"aa/":"zz/")+std::string(80,'p')+key(i*4),i);
    domain_case(collisions);
    std::cout<<"{\"phase\":\"domain\",\"status\":\"passed\",\"checks\":"<<checks<<"}\n";
}
struct Query {std::string key;std::optional<std::uint64_t> expected;};
struct Read {virtual ~Read()=default;virtual std::optional<std::uint64_t> find(std::string_view)const=0;};
template<bool Flat,bool Single> struct Reader:Read {
    const route_audit::Index& index;const route_audit::FlatRoute& flat;
    Reader(const route_audit::Index& i,const route_audit::FlatRoute& f):index(i),flat(f){}
    std::optional<std::uint64_t> find(std::string_view key)const override{return route_audit::find<Flat,Single>(index,flat,key);}
};
__attribute__((noinline)) static std::uint64_t pass(const Read& index,const std::vector<Query>& queries,const std::vector<std::size_t>& order) {
    std::uint64_t sum=0;for(const auto i:order)sum=sum*131+token(index.find(queries[i].key));return sum;
}
static void stats(const char* name,std::vector<std::uint64_t> values) {
    std::uint64_t total=0;for(auto x:values)total+=x;std::sort(values.begin(),values.end());
    std::cout<<'"'<<name<<"\":{\"total\":"<<total<<",\"mean\":"<<double(total)/values.size()
        <<",\"median\":"<<values[values.size()/2]<<",\"p95\":"<<values[(values.size()-1)*95/100]<<"}";
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="domain"){domain();return 0;}
        if(argc!=6)throw std::invalid_argument("rows queries clean|dirty process gate|timing");
        const std::string state=argv[3],mode=argv[5];const int process=std::stoi(argv[4]);
        if((state!="clean"&&state!="dirty")||(mode!="gate"&&mode!="timing")||process<0||process>=5)throw std::invalid_argument("state/mode/process");
        auto row_input=static_blocks::buffered(argv[1]),query_input=static_blocks::buffered(argv[2]);
        const auto input=string_index_bench::unified::read_rows(row_input);
        const auto frozen_queries=string_index_bench::unified::read_queries(query_input);
        Reference reference;for(const auto& r:input)reference.emplace(r.key,r.value);auto rows=rows_of(reference);
        require(!rows.empty()&&rows.size()==input.size()&&frozen_queries.size()==rows.size(),"input");
        route_audit::Index index(rows);slotted_page::Index frozen(rows);
        const std::size_t writes=state=="dirty"?1024:0;std::mt19937_64 writer(0xf2a20261018ULL);
        for(std::size_t i=0;i<writes;++i) {
            const auto& key=input[std::size_t(writer()%input.size())].key;const auto value=writer();
            require(index.update(key,value)&&frozen.update(key,value),"value update");reference[key]=value;
        }
        const auto bindings=index.bindings();const route_audit::FlatRoute flat(bindings);
        route_audit::CountedTree counted(bindings);std::vector<route_audit::RouteWork> work;work.reserve(frozen_queries.size());
        for(const auto& r:reference)check_key(index,flat,bindings,frozen,r.first,r.second);
        std::vector<Query> queries;queries.reserve(frozen_queries.size());
        for(std::size_t i=0;i<frozen_queries.size();++i) {
            auto key=frozen_queries[i].key;if(i%10==0){key+="~f2/miss";require(reference.find(key)==reference.end(),"absent query");}
            const auto it=reference.find(key);const auto expected=it==reference.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{it->second};
            check_key(index,flat,bindings,frozen,key,expected);queries.push_back({std::move(key),expected});
            route_audit::RouteWork w;const auto* p=counted.page(queries.back().key,w);const auto* q=flat.counted_page(queries.back().key,w);
            require(p==index.tree_page(queries.back().key)&&p==q,"counted selector");work.push_back(w);
        }
        checks+=index.verify();const auto scanned=index.scan("",SIZE_MAX);auto expected_row=reference.begin();
        require(scanned.size()==reference.size(),"scan count");
        for(const auto& r:scanned){require(r.key==expected_row->first&&r.value==expected_row->second,"scan value");++expected_row;}
        std::cout<<"{\"phase\":\"gate\",\"checks\":"<<checks<<",\"queries\":"<<queries.size()<<",\"rows\":"<<reference.size()
            <<",\"writes\":"<<writes<<",\"pages\":"<<bindings.size()<<",\"leaf_index_bytes\":"<<index.memory_bytes()
            <<",\"flat_extra_bytes\":"<<flat.memory_bytes()<<",\"rebuilds\":"<<index.maintenance().rebuilds<<",\"counter\":{";
        auto field=[&](auto member){std::vector<std::uint64_t> values;values.reserve(work.size());for(const auto& w:work)values.push_back(w.*member);return values;};
        stats("tree_compares",field(&route_audit::RouteWork::tree_compares));std::cout<<',';
        stats("tree_bytes",field(&route_audit::RouteWork::tree_bytes));std::cout<<',';
        stats("digest_steps",field(&route_audit::RouteWork::digest_steps));std::cout<<',';
        stats("flat_compares",field(&route_audit::RouteWork::flat_compares));std::cout<<',';
        stats("flat_bytes",field(&route_audit::RouteWork::flat_bytes));std::cout<<',';
        stats("prefix_bytes",field(&route_audit::RouteWork::prefix_bytes));std::cout<<"}}\n";
        if(mode=="gate")return 0;
        Reader<false,false> tw(index,flat);Reader<false,true> ts(index,flat);Reader<true,false> fw(index,flat);Reader<true,true> fs(index,flat);
        const Read* arms[]={&tw,&tw,&ts,&ts,&fw,&fw,&fs,&fs};
        const char* names[]={"tree_wrapped","tree_wrapped_AA","tree_single","tree_single_AA","flat_wrapped","flat_wrapped_AA","flat_single","flat_single_AA"};
        constexpr int williams[]={0,1,7,2,6,3,5,4};
        std::vector<std::size_t> order(queries.size());std::iota(order.begin(),order.end(),0);
        for(const auto* arm:arms)sink=pass(*arm,queries,order);
        for(int repeat=0;repeat<8;++repeat) {
            std::iota(order.begin(),order.end(),0);std::mt19937_64 random(UINT64_C(0x12345f2)+std::uint64_t(repeat));std::shuffle(order.begin(),order.end(),random);
            std::uint64_t expected=0;for(const auto i:order)expected=expected*131+token(queries[i].expected);
            for(int position=0;position<8;++position) {
                const int arm=(williams[position]+repeat+process)%8;const auto start=std::chrono::steady_clock::now();
                const auto checksum=pass(*arms[arm],queries,order);
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                sink=checksum;require(checksum==expected,"timed checksum");
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position<<",\"arm\":\""<<names[arm]
                    <<"\",\"operations\":"<<queries.size()<<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

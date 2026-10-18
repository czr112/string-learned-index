#include "navigation_entry_index.hpp"
#include "segment_navigation.hpp"
#include "static_input.hpp"
#include <iostream>
#include <tuple>

namespace ne=navigation_entry;
namespace sn=segment_navigation;
namespace uni=string_index_bench::unified;
using Row=string_index_bench::v1::Row;
static std::uint64_t checks=0;
static void require(bool ok,const char* message){++checks;if(!ok)throw std::logic_error(message);}
static int bytes(std::string_view a,std::string_view b) {
    for(std::size_t i=0;i<std::min(a.size(),b.size());++i) {
        const auto x=static_cast<unsigned char>(a[i]),y=static_cast<unsigned char>(b[i]);
        if(x!=y)return x<y?-1:1;
    }
    return a.size()<b.size()?-1:a.size()>b.size()?1:0;
}
template<class Work> static auto recorded(const Work& w) {
    return std::make_tuple(w.queries,w.rejected,w.model_steps,w.window_blocks,w.blocks,w.exact,w.wide,w.directory_steps,w.fallback,w.windows,w.entered);
}
struct Ledger {
    ne::Work old;
    sn::Work flat;
    std::uint64_t queries=0,model_search_queries=0;
    std::vector<std::size_t> route_steps,model_steps,flat_steps;
};
static void probe(const ne::Index& core,const physical_locator::Index& control,const sn::Directory& nav,
    const std::vector<Row>& rows,std::size_t chunk,std::string_view q,Ledger* ledger=nullptr) {
    const auto it=std::lower_bound(rows.begin(),rows.end(),q,[](const Row& r,std::string_view k){return bytes(r.key,k)<0;});
    const auto want=it!=rows.end()&&bytes(it->key,q)==0?std::optional<std::uint64_t>(it->value):std::nullopt;
    ne::Work work;static_physical::Work original;sn::Work flat;
    require(core.find_model(q,&work)==want&&control.find_model(q,&original)==want,"lookup oracle");
    require(recorded(work)==recorded(original),"instrumentation changed recorded work");
    if(!rows.empty()) {
        const auto selected=nav.select(q,&flat);
        const auto after=std::upper_bound(rows.begin(),rows.end(),q,[](std::string_view k,const Row& r){return bytes(k,r.key)<0;});
        const auto ri=(after==rows.begin()?0:std::size_t(after-rows.begin()-1))/chunk;
        require(selected.region==ri&&core.locate(q)==ri,"region selection");
        const auto& r=core.region(ri);
        if(q.size()>=r.prefix.size()&&q.substr(0,r.prefix.size())==r.prefix) {
            const auto x=static_physical::coordinate(q.substr(r.prefix.size()));
            auto seg=std::lower_bound(r.segments.begin(),r.segments.end(),x,[](const auto& s,std::uint64_t v){return s.xmax<v;});
            const auto si=seg==r.segments.end()?r.segments.size()-1:std::size_t(seg-r.segments.begin());
            require(selected.segment==si,"numeric segment selection");
        }
    }
    if(!ledger)return;
    ++ledger->queries;ledger->model_search_queries+=work.model_steps>0;
#define ADD_FIELD(name) ledger->old.name+=work.name
    ADD_FIELD(queries);ADD_FIELD(rejected);ADD_FIELD(model_steps);ADD_FIELD(window_blocks);
    ADD_FIELD(blocks);ADD_FIELD(exact);ADD_FIELD(wide);ADD_FIELD(directory_steps);ADD_FIELD(fallback);
    ADD_FIELD(route_steps);ADD_FIELD(route_full_compares);ADD_FIELD(route_full_bytes);
    ADD_FIELD(route_prefix_compares);ADD_FIELD(route_prefix_bytes);ADD_FIELD(digest_bytes);
    ADD_FIELD(region_prefix_compares);ADD_FIELD(region_prefix_bytes);ADD_FIELD(coordinate_bytes);
    ADD_FIELD(crc_bytes);ADD_FIELD(exact_bytes);
#undef ADD_FIELD
    ledger->flat.steps+=flat.steps;ledger->flat.full_compares+=flat.full_compares;
    ledger->flat.full_bytes+=flat.full_bytes;ledger->flat.prefix_bytes+=flat.prefix_bytes;
    ledger->flat.digest_bytes+=flat.digest_bytes;
    ledger->route_steps.push_back(work.route_steps);ledger->model_steps.push_back(work.model_steps);ledger->flat_steps.push_back(flat.steps);
}
static void domain() {
    for(unsigned i=0;i<65536;++i) {
        std::string k;k.push_back(char(i>>8));k.push_back(char(i));
        const auto x=static_physical::coordinate(k);const auto next=sn::successor(x);
        require(next.has_value()&&static_physical::coordinate(*next)>x,"short coordinate successor");
    }
    require(!sn::successor(static_physical::coordinate(std::string(7,char(255)))) ,"maximum coordinate successor");
    const auto carry=sn::successor(static_physical::coordinate(std::string("a")+std::string(6,char(255))));
    require(carry==std::optional<std::string>("b"),"fixed coordinate carry");
    std::vector<Row> rows{{"",0},{"a",1},{std::string("a\0",2),2},{std::string(1,char(128)),3},
        {std::string(1,char(255)),4},{std::string(4096,'z'),UINT64_MAX}};
    for(unsigned i=0;i<2000;++i) {
        std::string k="collision/"+std::string(64,'x');k.push_back(char(i>>8));k.push_back(char(i));rows.push_back({std::move(k),i+10});
    }
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return bytes(a.key,b.key)<0;});
    for(auto chunk:{17u,64u,2048u}) {
        ne::Index core(rows,chunk);physical_locator::Index control(rows,chunk);sn::Directory nav(core);
        for(const auto& row:rows) {
            probe(core,control,nav,rows,chunk,row.key);
            probe(core,control,nav,rows,chunk,row.key+std::string(1,'\0'));
            if(!row.key.empty())probe(core,control,nav,rows,chunk,std::string_view(row.key).substr(0,row.key.size()-1));
        }
        for(unsigned i=0;i<65536;++i) {
            std::string q;q.push_back(char(i>>8));q.push_back(char(i));probe(core,control,nav,rows,chunk,q);
        }
    }
    for(const auto& rows:std::vector<std::vector<Row>>{{},{{"",0}},{{std::string(7,char(255)),UINT64_MAX}}}) {
        ne::Index core(rows);physical_locator::Index control(rows);sn::Directory nav(core);
        for(const auto& q:std::vector<std::string>{"",std::string(1,'\0'),"a",std::string(8,char(255))})probe(core,control,nav,rows,2048,q);
        require(nav.size()==(rows.empty()?0:1),"empty/singleton cut geometry");
    }
}
static std::size_t percentile(std::vector<std::size_t> values,double p) {
    if(values.empty())return 0;std::sort(values.begin(),values.end());return values[std::size_t((values.size()-1)*p)];
}
int main(int argc,char** argv) {
    try {
        if(argc==1){domain();std::cout<<"{\"domain_checks\":"<<checks<<"}\n";return 0;}
        if(argc!=3)throw std::invalid_argument("[rows.bin queries.bin]");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        ne::Index core(rows);physical_locator::Index control(rows);sn::Directory nav(core);std::vector<std::size_t> sizes;
        require(core.memory().total()==control.memory().total(),"instrumentation changed resident geometry");
        for(std::size_t r=0;r<core.region_count();++r)sizes.push_back(core.region(r).segments.size());
        for(const auto& row:rows)probe(core,control,nav,rows,2048,row.key);
        for(const auto& cut:nav.cuts()) {
            probe(core,control,nav,rows,2048,cut.key);probe(core,control,nav,rows,2048,cut.key+std::string(1,'\0'));
            if(!cut.key.empty())probe(core,control,nav,rows,2048,std::string_view(cut.key).substr(0,cut.key.size()-1));
        }
        Ledger ledger;
        for(const auto& q:queries) {
            probe(core,control,nav,rows,2048,q.key,&ledger);
            const auto r=control.find_model(q.key);require(r.has_value()==q.expected_present&&(!r||*r==q.expected_value),"frozen query oracle");
        }
        const auto& w=ledger.old;const auto& f=ledger.flat;
        std::cout<<"{\"checks\":"<<checks<<",\"rows\":"<<rows.size()<<",\"queries\":"<<ledger.queries
            <<",\"regions\":"<<core.region_count()<<",\"segments\":"<<nav.size()<<",\"core_bytes\":"<<control.memory().total()
            <<",\"navigation_bytes\":"<<nav.memory()<<",\"model_search_queries\":"<<ledger.model_search_queries
            <<",\"old\":{\"route_steps\":"<<w.route_steps<<",\"route_full_compares\":"<<w.route_full_compares
            <<",\"route_full_bytes\":"<<w.route_full_bytes<<",\"route_prefix_compares\":"<<w.route_prefix_compares
            <<",\"route_prefix_bytes\":"<<w.route_prefix_bytes<<",\"digest_bytes\":"<<w.digest_bytes
            <<",\"region_prefix_compares\":"<<w.region_prefix_compares<<",\"region_prefix_bytes\":"<<w.region_prefix_bytes
            <<",\"coordinate_bytes\":"<<w.coordinate_bytes<<",\"model_steps\":"<<w.model_steps
            <<",\"directory_steps\":"<<w.directory_steps<<",\"crc_bytes\":"<<w.crc_bytes<<",\"blocks\":"<<w.blocks
            <<",\"exact\":"<<w.exact<<",\"exact_bytes\":"<<w.exact_bytes<<",\"rejected\":"<<w.rejected<<"}"
            <<",\"flat\":{\"steps\":"<<f.steps<<",\"full_compares\":"<<f.full_compares<<",\"full_bytes\":"<<f.full_bytes
            <<",\"prefix_bytes\":"<<f.prefix_bytes<<",\"digest_bytes\":"<<f.digest_bytes<<"}"
            <<",\"distribution\":{\"route_p50\":"<<percentile(ledger.route_steps,.5)<<",\"model_p50\":"<<percentile(ledger.model_steps,.5)
            <<",\"flat_p50\":"<<percentile(ledger.flat_steps,.5)<<",\"segments_per_region_p50\":"<<percentile(sizes,.5)
            <<",\"segments_per_region_max\":"<<percentile(sizes,1)<<"}}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

#include "locator_seam_adapter.hpp"
#include "static_input.hpp"
#include <iostream>
#include <tuple>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;
namespace pl=physical_locator;
using Row=sib::Row;
static std::uint64_t checks=0,scans=0,probes=0,rejections=0,work_checks=0;
static void require(bool ok,const char* message) {++checks;if(!ok)throw std::logic_error(message);}
// Deliberately independent of the core's memcmp, suffix and fence helpers.
static int bytes(std::string_view a,std::string_view b) {
    for(std::size_t i=0;i<std::min(a.size(),b.size());++i) {
        const auto x=static_cast<unsigned char>(a[i]),y=static_cast<unsigned char>(b[i]);
        if(x!=y)return x<y?-1:1;
    }
    return a.size()<b.size()?-1:a.size()>b.size()?1:0;
}
static auto lower(const std::vector<Row>& rows,std::string_view q) {
    return std::lower_bound(rows.begin(),rows.end(),q,[](const Row& r,std::string_view k){return bytes(r.key,k)<0;});
}
template<class Rows> static void scan_check(const Rows& got,const std::vector<Row>& rows,std::string_view q,std::size_t limit) {
    const auto it=lower(rows,q);const auto n=std::min<std::size_t>(rows.end()-it,limit);
    require(got.size()==n,"scan size");
    for(std::size_t i=0;i<n;++i)require(got[i].key==it[i].key&&got[i].value==it[i].value,"scan row");
    ++scans;
}
static void location_check(const pl::Index& core,const std::vector<Row>& rows,std::size_t chunk,
    std::string_view q,const pl::Location& p,bool exact) {
    core.validate_location(p);
    if(rows.empty()) {require(p.state==pl::LocationState::empty,"empty location");return;}
    const auto after=std::upper_bound(rows.begin(),rows.end(),q,
        [](std::string_view k,const Row& r){return bytes(k,r.key)<0;});
    const auto predecessor=after==rows.begin()?0:std::size_t(after-rows.begin()-1);
    const auto region=predecessor/chunk,first=region*chunk,end=std::min(first+chunk,rows.size());
    require(p.region==region,"region differs from independent full-key predecessor");
    std::size_t prefix=0;
    while(prefix<std::min(rows[first].key.size(),rows[end-1].key.size())&&
          rows[first].key[prefix]==rows[end-1].key[prefix])++prefix;
    const bool matches=q.size()>=prefix&&bytes(q.substr(0,prefix),std::string_view(rows[first].key).substr(0,prefix))==0;
    if(!matches) {
        require(p.state==pl::LocationState::prefix_rejected&&!p.prefix_verified,"prefix rejection");++rejections;return;
    }
    // Row predecessor identifies the physical block without the implementation's directory.
    const auto expected=(predecessor-first)/16;
    require(p.prefix_verified&&p.begin<=expected&&expected<p.end,"range excludes physical predecessor");
    if(exact)require(p.state==pl::LocationState::exact_predecessor&&p.begin==expected&&p.end==expected+1,"directory range");
}
static auto fields(const pl::Work& w) {
    return std::make_tuple(w.queries,w.rejected,w.model_steps,w.window_blocks,w.blocks,w.exact,w.wide,
        w.directory_steps,w.fallback,w.windows,w.entered);
}
static void probe(const pl::Index& core,const static_physical::Index& old,const std::vector<Row>& rows,
    std::size_t chunk,std::string_view q,bool scan,bool counted) {
    ++probes;
    const auto it=lower(rows,q);const std::optional<std::uint64_t> want=
        it!=rows.end()&&bytes(it->key,q)==0?std::optional<std::uint64_t>(it->value):std::nullopt;
    location_check(core,rows,chunk,q,core.locate_blocks<pl::Lookup::learned>(q),false);
    location_check(core,rows,chunk,q,core.locate_blocks<pl::Lookup::directory>(q),true);
    location_check(core,rows,chunk,q,core.locate_blocks<pl::Lookup::wide_directory>(q),false);
    require(core.find_model(q)==want,"model answer");
    require(core.find_directory(q)==want,"directory answer");
    require(core.find_hybrid(q)==want,"hybrid answer");
    require(old.find_model(q)==want&&old.find_directory(q)==want&&old.find_hybrid(q)==want,"old answer");
    if(counted) {
        for(unsigned policy=0;policy<3;++policy) {
            pl::Work a,b;
            if(policy==0){old.find_model(q,&a);core.find_model(q,&b);}
            else if(policy==1){old.find_directory(q,&a);core.find_directory(q,&b);}
            else{old.find_hybrid(q,&a);core.find_hybrid(q,&b);}
            require(fields(a)==fields(b),"recorded work differs");++work_checks;
        }
    }
    if(scan)scan_check(core.scan(q,17),rows,q,17);
}
static void malformed(const pl::Index& core,const std::vector<Row>& rows) {
    const auto bad=[&](pl::Location p) {
        bool threw=false;try{core.validate_location(p);}catch(const std::logic_error&){threw=true;}
        require(threw,"malformed location accepted");
    };
    if(rows.empty()) {
        bad({pl::LocationState::candidates,0,0,1,true});
        bad({pl::LocationState::empty,0,0,0,false});return;
    }
    bad({});bad({pl::LocationState::candidates,SIZE_MAX,0,1,true});
    bad({pl::LocationState::candidates,0,0,0,true});
    bad({pl::LocationState::candidates,0,1,0,true});
    bad({pl::LocationState::candidates,0,0,UINT32_MAX,true});
    bad({pl::LocationState::candidates,0,0,1,false});
    bad({pl::LocationState::exact_predecessor,0,0,2,true});
    bad({pl::LocationState::prefix_rejected,0,0,1,false});
    bad({pl::LocationState::prefix_rejected,0,0,0,true});
    bad({static_cast<pl::LocationState>(99),0,0,1,true});
}
static void lifecycle(const std::vector<Row>& rows) {
    for(const auto& policy:{"learned","directory","wide_directory"}) {
        auto adapter=make_locator_seam_index({{"lookup",policy}});
        bool threw=false;try{adapter->find("");}catch(const std::logic_error&){threw=true;}require(threw,"find before build");
        threw=false;try{adapter->scan("",0);}catch(const std::logic_error&){threw=true;}require(threw,"scan before build");
        for(const std::vector<Row>& invalid:std::vector<std::vector<Row>>{{{"b",1},{"a",2}},{{"a",1},{"a",2}}}) {
            threw=false;try{adapter->bulk_load(invalid);}catch(const std::invalid_argument&){threw=true;}
            require(threw,"invalid build accepted");
            require(adapter->memory_usage().index_owned_bytes==std::optional<std::uint64_t>(0),"failed build published state");
            threw=false;try{adapter->find("");}catch(const std::logic_error&){threw=true;}require(threw,"failed build query accepted");
        }
        auto input=rows;adapter->bulk_load(input);std::vector<Row>().swap(input);
        for(const auto& row:rows)require(adapter->find(row.key)==std::optional<std::uint64_t>(row.value),"owned input lookup");
        threw=false;try{adapter->bulk_load({});}catch(const std::logic_error&){threw=true;}require(threw,"second successful build");
        auto result=adapter->scan("",SIZE_MAX);adapter.reset();scan_check(result,rows,"",SIZE_MAX);
    }
}
static void dataset(const std::vector<Row>& rows,const std::vector<uni::Query>& queries,
    const std::vector<std::size_t>& chunks,bool exhaustive) {
    for(auto chunk:chunks) {
        auto input=rows;pl::Index core(input,chunk);static_physical::Index old(input,chunk);std::vector<Row>().swap(input);
        const auto a=core.memory(),b=old.memory();
        require(a.total()-a.object==b.total()-b.object,"storage capacities changed");
        require(a.object==b.object+sizeof(std::size_t),"unexpected metadata cost");
        const auto id=core.build_identity();require(id.region_keys==chunk&&id.block_entries==16&&
            id.coordinate_symbols==7&&id.certificate_target==3,"build metadata");
        require(core.size()==rows.size()&&core.block_count()==old.block_count()&&core.segment_count()==old.segment_count(),"layout/model geometry");
        checks+=core.verify_numeric_certificate();malformed(core,rows);
        for(std::size_t i=0;i<rows.size();++i) {
            const auto& k=rows[i].key;probe(core,old,rows,chunk,k,i%257==0,false);
            if(exhaustive||i%16==0) {
                probe(core,old,rows,chunk,k+std::string(1,'\0'),true,false);
                probe(core,old,rows,chunk,k+std::string(1,char(255)),true,false);
                if(!k.empty())probe(core,old,rows,chunk,std::string_view(k).substr(0,k.size()-1),true,false);
            }
        }
        for(std::size_t i=0;i<queries.size();++i) {
            const auto& q=queries[i];probe(core,old,rows,chunk,q.key,i%257==0,true);
            const auto it=lower(rows,q.key);
            require((it!=rows.end()&&bytes(it->key,q.key)==0)==q.expected_present,"frozen query presence");
            if(q.expected_present)require(it->value==q.expected_value,"frozen query value");
        }
        if(exhaustive)for(unsigned i=0;i<65536;++i) {
            std::string k;k.push_back(char(i>>8));k.push_back(char(i));probe(core,old,rows,chunk,k,i%257==0,false);
        }
        for(const auto& q:std::vector<std::string>{"","a",std::string(1,char(255)),std::string(4096,char(255))}) {
            probe(core,old,rows,chunk,q,true,false);
            for(auto limit:{std::size_t(0),std::size_t(1),std::size_t(16),std::size_t(17),std::size_t(64),SIZE_MAX})
                scan_check(core.scan(q,limit),rows,q,limit);
        }
        scan_check(core.scan("",SIZE_MAX),rows,"",SIZE_MAX);
    }
    lifecycle(rows);
}
static void domain() {
    for(const auto& rows:std::vector<std::vector<Row>>{{},{{"",0}},{{"only",UINT64_MAX}}})dataset(rows,{}, {1,17,2048},false);
    std::vector<Row> rows{{"",0},{"a",1},{std::string("a\0",2),2},{std::string(1,char(128)),3},
        {std::string(1,char(255)),4},{std::string(4096,'z'),UINT64_MAX}};
    for(unsigned i=0;i<2000;++i) {
        std::string k="collision/"+std::string(64,'x');k.push_back(char(i>>8));k.push_back(char(i));rows.push_back({std::move(k),i+10});
    }
    for(unsigned i=0;i<300;++i) {
        std::string k;k.push_back(char((i*193)%256));k.push_back(char((i*71)%256));k.push_back(char(i>>8));rows.push_back({std::move(k),i+3000});
    }
    std::sort(rows.begin(),rows.end(),[](const Row& a,const Row& b){return bytes(a.key,b.key)<0;});
    rows.erase(std::unique(rows.begin(),rows.end(),[](const Row& a,const Row& b){return a.key==b.key;}),rows.end());
    dataset(rows,{}, {17,64,2048},true);
    // An exact predecessor range need not hold scan's first result: key lies
    // strictly between the last key of block 0 and the first key of block 1.
    std::vector<Row> gaps;
    for(unsigned i=0;i<65;++i)gaps.push_back({"gap/"+std::to_string(1000+i),i});
    pl::Index core(gaps,32);const auto q=gaps[15].key+std::string(1,'\0');
    const auto p=core.locate_blocks<pl::Lookup::directory>(q);
    require(p.begin==0&&p.end==1,"explicit gap predecessor");scan_check(core.scan(q,33),gaps,q,33);
    const std::string before="g";const auto reject=core.locate_blocks<pl::Lookup::learned>(before);
    require(reject.state==pl::LocationState::prefix_rejected,"explicit scan prefix rejection");
    scan_check(core.scan(before,33),gaps,before,33);
    dataset(gaps,{}, {1,15,16,17,32,65536},false);
    for(auto chunk:{0u,65537u}) {
        bool threw=false;try{pl::Index invalid(gaps,chunk);}catch(const std::invalid_argument&){threw=true;}require(threw,"invalid region size");
    }
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin queries.bin");
        domain();const auto domain_checks=checks;
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        dataset(rows,queries,{2048},false);
        pl::Index core(rows);const auto m=core.memory();
        std::cout<<"{\"checks\":"<<checks<<",\"domain_checks\":"<<domain_checks<<",\"probes\":"<<probes
            <<",\"scans\":"<<scans<<",\"rejections\":"<<rejections<<",\"work_checks\":"<<work_checks
            <<",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()<<",\"core_bytes\":"<<m.total()
            <<",\"object_bytes\":"<<m.object<<",\"storage_bytes\":"<<m.total()-m.object<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

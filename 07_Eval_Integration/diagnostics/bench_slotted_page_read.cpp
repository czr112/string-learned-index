#include "slotted_page_index.hpp"
#include "collision_refined_index.hpp"
#include "static_input.hpp"
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>

using slotted_page::Row;
static volatile std::uint64_t sink=0;
static std::uint64_t token(std::optional<std::uint64_t> value) {
    return value?*value^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
struct Query {std::string key;std::optional<std::uint64_t> expected;};
struct Read {virtual ~Read()=default;virtual std::optional<std::uint64_t> find(std::string_view)const=0;};
struct PageRead:Read {
    const slotted_page::Index& core;
    explicit PageRead(const slotted_page::Index& index):core(index){}
    std::optional<std::uint64_t> find(std::string_view key)const override{return core.find(key);}
};
struct D1Read:Read {
    const collision_refined::Index& core;
    explicit D1Read(const collision_refined::Index& index):core(index){}
    std::optional<std::uint64_t> find(std::string_view key)const override{return core.find_model(key);}
};
__attribute__((noinline)) static std::uint64_t pass(const Read& index,const std::vector<Query>& queries,
                                                   const std::vector<std::size_t>& order) {
    std::uint64_t checksum=0;for(const auto i:order)checksum=checksum*131+token(index.find(queries[i].key));return checksum;
}
int main(int argc,char** argv) {
    try {
        if(argc!=6)throw std::invalid_argument("rows queries clean|dirty process gate|timing");
        const std::string state=argv[3],mode=argv[5];const auto process=std::stoi(argv[4]);
        if((state!="clean"&&state!="dirty")||(mode!="gate"&&mode!="timing")||process<0||process>=5)
            throw std::invalid_argument("mode/state/process");
        auto row_input=static_blocks::buffered(argv[1]),query_input=static_blocks::buffered(argv[2]);
        const auto loaded=string_index_bench::unified::read_rows(row_input);
        const auto frozen=string_index_bench::unified::read_queries(query_input);
        std::map<std::string,std::uint64_t,slotted_page::ByteLess> reference;
        std::vector<Row> rows;
        for(const auto& r:loaded){rows.push_back({r.key,r.value});reference.emplace(r.key,r.value);}
        if(rows.empty()||rows.size()!=reference.size()||frozen.size()!=rows.size())throw std::logic_error("input rows");
        slotted_page::Index live(rows);
        std::mt19937_64 writer(0xf2a20261018ULL);
        const std::size_t writes=state=="dirty"?1024:0;
        for(std::size_t i=0;i<writes;++i) {
            const auto& key=loaded[std::size_t(writer()%loaded.size())].key;const auto value=writer();
            if(!live.update(key,value))throw std::logic_error("update rejected");reference[key]=value;
        }
        rows.clear();for(const auto& r:reference)rows.push_back({r.first,r.second});
        const slotted_page::Index control(rows,16);
        const collision_refined::Index d1(rows);
        std::vector<Query> queries;queries.reserve(frozen.size());
        for(std::size_t i=0;i<frozen.size();++i) {
            std::string key=frozen[i].key;
            if(i%10==0){key+="~f2/miss";if(reference.find(key)!=reference.end())throw std::logic_error("missing key collision");}
            const auto found=reference.find(key);
            queries.push_back({std::move(key),found==reference.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{found->second}});
        }
        std::size_t checks=live.verify()+control.verify()+d1.verify_numeric_certificate();
        for(const auto& q:queries) {
            if(live.find(q.key)!=q.expected||control.find(q.key)!=q.expected||d1.find_model(q.key)!=q.expected)
                throw std::logic_error("full query mismatch");
            live.check_window(q.key);control.check_window(q.key);d1.check_window(q.key);++checks;
        }
        const auto scanned=live.scan("",SIZE_MAX),controlled=control.scan("",SIZE_MAX);
        if(scanned.size()!=rows.size()||controlled.size()!=rows.size())throw std::logic_error("scan count");
        for(std::size_t i=0;i<rows.size();++i)
            if(scanned[i].key!=rows[i].key||scanned[i].value!=rows[i].value||controlled[i].key!=rows[i].key||controlled[i].value!=rows[i].value)
                throw std::logic_error("scan mismatch");
        std::cout<<"{\"phase\":\"gate\",\"checks\":"<<checks<<",\"queries\":"<<queries.size()
            <<",\"rows\":"<<rows.size()<<",\"writes\":"<<writes<<",\"pages\":"<<live.page_count()
            <<",\"live_bytes\":"<<live.memory_bytes()<<",\"control_bytes\":"<<control.memory_bytes()<<",\"d1_bytes\":"<<d1.memory().total()
            <<",\"inplace_updates\":"<<live.maintenance().inplace_updates<<",\"rebuilds\":"<<live.maintenance().rebuilds<<"}\n";
        if(mode=="gate")return 0;
        PageRead live_read(live),control_read(control);D1Read d1_read(d1);
        const Read* arms[]={&live_read,&live_read,&control_read,&control_read,&d1_read,&d1_read};
        const char* names[]={"live","live_AA","control","control_AA","D1","D1_AA"};
        constexpr int williams[]={0,1,5,2,4,3};
        std::vector<std::size_t> order(queries.size());std::iota(order.begin(),order.end(),0);
        for(const auto* arm:arms)sink=pass(*arm,queries,order);
        for(int repeat=0;repeat<6;++repeat) {
            std::iota(order.begin(),order.end(),0);std::mt19937_64 random(UINT64_C(0x12345f2)+std::uint64_t(repeat));
            std::shuffle(order.begin(),order.end(),random);std::uint64_t expected=0;
            for(const auto i:order)expected=expected*131+token(queries[i].expected);
            for(int position=0;position<6;++position) {
                const auto arm=(williams[position]+repeat+process)%6;
                const auto start=std::chrono::steady_clock::now();const auto checksum=pass(*arms[arm],queries,order);
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                sink=checksum;if(checksum!=expected)throw std::logic_error("timed checksum");
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position<<",\"arm\":\""<<names[arm]
                    <<"\",\"operations\":"<<queries.size()<<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

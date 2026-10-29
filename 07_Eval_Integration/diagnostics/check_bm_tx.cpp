// Public mutation/scan seam and the already registered structural verify() seam.
#include "bm_followup_memory.hpp"
#include "bm_tx_adapter.hpp"
#include "bm_split_adapter.hpp"
#include "bm_tx_trace.hpp"
#include <iostream>
#include <map>
#include <random>

using Index=bm_tx_page::Index;
using Row=static_physical::Row;
using Oracle=std::map<std::string,std::uint64_t,bm_tx_page::ByteLess>;
namespace sib=string_index_bench::v1;
namespace mem=bm_followup_memory;
static std::uint64_t checks=0;
static void require(bool ok,const char* why) {
    ++checks;if(!ok)throw std::logic_error(why);
}
static std::vector<Row> fixture(std::size_t n) {
    std::vector<Row> rows;
    for(std::size_t i=0;i<n;++i)rows.push_back({"p/"+std::to_string(10000000+i*4),i+1});
    return rows;
}
static void full_check(const Index& index,const Oracle& oracle) {
    index.verify();
    require(index.size()==oracle.size(),"cardinality");
    const auto rows=index.scan("",SIZE_MAX);
    require(rows.size()==oracle.size(),"scan cardinality");
    std::size_t at=0;
    for(const auto& [key,value]:oracle) {
        require(rows[at].key==key&&rows[at].value==value,"scan content");
        require(index.find_base(key)==std::optional<std::uint64_t>(value),"full find");
        ++at;
    }
}
static void regression() {
        auto rows=fixture(6144);
        Index index(rows);Oracle oracle;
        for(const auto& r:rows)oracle.emplace(r.key,r.value);
        full_check(index,oracle);
        // Split a full page, then fill the SAME block again. The extracted vector's
        // capacity can shrink at the next block split; the ledger must follow it.
        for(unsigned i=0;i<12;++i) {
            const auto key=rows[100].key+"~tx/"+std::to_string(i);
            require(index.insert(key,90000+i),"insert");oracle.emplace(key,90000+i);
            full_check(index,oracle);
        }
        require(index.maintenance().tx_splits>0,"transaction path exercised");
        for(const auto& key:std::vector<std::string>{"",std::string(1,'\0'),"o/outside","p/00000000","z/last"}) {
            require(index.insert(key,99),"boundary insert");oracle.emplace(key,99);
            full_check(index,oracle);
        }
        // The stable fence can outlive its deleted row, and even an entire empty block.
        auto small=fixture(2048);bm_n1_leaf::Page page(small);
        Oracle live;for(const auto& row:small)live.emplace(row.key,row.value);
        for(unsigned i=1020;i<1035;++i){require(page.erase(small[i].key),"erase fence block");live.erase(small[i].key);}
        auto halves=bm_n1_leaf::Page::extract_split(page,68);
        halves.lower->verify();halves.upper->verify();
        for(const auto& [key,value]:live) {
            auto a=halves.lower->find_base(key),b=halves.upper->find_base(key);
            require(bool(a)!=bool(b),"split exactly one owner");require((a?a:b)==value,"split value");
        }
        // Delete/compact across pages, then continue writing.
        for(unsigned i=0;i<1600;++i){require(index.erase(rows[i].key),"erase");oracle.erase(rows[i].key);}
        index.compact();full_check(index,oracle);
        std::cout<<"{\"suite\":\"regression\",\"checks\":"<<checks<<",\"mismatches\":0}\n";
}

static std::uint64_t factory_check(sib::Index& index,const Oracle& oracle) {
    const auto out=index.scan("",SIZE_MAX);require(out.size()==oracle.size(),"factory size");
    std::size_t i=0;
    for(const auto& [key,value]:oracle) {
        require(out[i].key==key&&out[i].value==value,"factory scan");
        require(index.find(key)==std::optional<std::uint64_t>(value),"factory read");++i;
    }
    if(auto* concrete=dynamic_cast<bm_tx_experiment::Adapter*>(&index))concrete->verify();
    return i;
}
static void differential(const char* rows_path,const char* ops_path) {
    const auto rows=bm_tx_trace::rows(rows_path);const auto ops=bm_tx_trace::operations(ops_path);
    auto index=make_bm_tx_experiment_index({});index->bulk_load(rows);
    Oracle oracle;for(const auto& row:rows)oracle.emplace(row.key,row.value);
    std::uint64_t seen=0;factory_check(*index,oracle);
    for(const auto& op:ops) {
        const auto result=bm_tx_trace::apply(*index,op);
        if(op.kind==1){auto it=oracle.find(op.key);require(result==(it==oracle.end()?UINT64_MAX:it->second),"trace find");}
        else if(op.kind==2){bool inserted=oracle.emplace(op.key,op.value).second;require(result==(inserted?1u:2u),"trace insert");}
        else if(op.kind==3){auto it=oracle.find(op.key);require(result==(it==oracle.end()?4u:3u),"trace update");if(it!=oracle.end())it->second=op.value;}
        else if(op.kind==4){bool erased=oracle.erase(op.key);require(result==(erased?5u:6u),"trace erase");}
        else {
            const auto out=index->scan(op.key,op.limit);auto it=oracle.lower_bound(op.key);std::size_t i=0;
            for(;it!=oracle.end()&&i<op.limit;++it,++i)require(i<out.size()&&out[i].key==it->first&&out[i].value==it->second,"trace scan");
            require(out.size()==i,"trace scan limit");
        }
        if(++seen%2000==0)factory_check(*index,oracle);
    }
    factory_check(*index,oracle);
    auto& concrete=dynamic_cast<bm_tx_experiment::Adapter&>(*index);
    const auto splits=concrete.maintenance().tx_splits;
    std::cout<<"{\"suite\":\"differential\",\"checks\":"<<checks<<",\"mismatches\":0,\"tx_splits\":"<<splits<<",\"final_rows\":"<<oracle.size()<<"}\n";
}

static void fault(const char* rows_path) {
    std::vector<sib::Row> rows;
    if(std::string(rows_path)=="--bytes") {
        for(auto& row:fixture(6144))rows.push_back({std::string("\0\xff",2)+row.key,row.value});
    }else{rows=bm_tx_trace::rows(rows_path);if(rows.size()>6144)rows.resize(6144);}
    require(rows.size()>=4096,"fault fixture size");
    for(const auto position:std::vector<std::size_t>{100,2200,rows.size()-100}) {
        const std::string key=rows[position].key+"~tx/fault";
        const std::string follow=rows[position].key+"~tx/continue";
        Oracle oracle;for(const auto& row:rows)oracle.emplace(row.key,row.value);
        std::uint64_t allocations=0,failures=0;
        for(std::uint64_t ordinal=0;ordinal<=allocations+1;++ordinal) {
            mem::begin();
            auto index=make_bm_tx_experiment_index({});index->bulk_load(rows);
            const auto before=mem::live;
            mem::arm(ordinal);bool caught=false;
            try {require(index->insert(key,999)==sib::InsertResult::inserted,"fault insert");}
            catch(const std::bad_alloc&){caught=true;}
            const auto calls=mem::disarm();
            if(ordinal==0)allocations=calls;
            if(caught) {
                ++failures;require(mem::live==before,"failure exact live bytes");
                require(!index->find(key),"failed key absent");factory_check(*index,oracle);
                require(index->insert(key,999)==sib::InsertResult::inserted,"retry insert");
            }
            require(index->find(key)==std::optional<sib::Value>(999),"insert readable");
            // Exactly 1,000 continued insert/delete operations after every ordinal.
            for(unsigned n=0;n<500;++n) {
                require(index->insert(follow,n)==sib::InsertResult::inserted,"continue insert");
                require(index->erase(follow)==sib::EraseResult::erased,"continue erase");
            }
            require(index->erase(key)==sib::EraseResult::erased,"remove fault key");
            factory_check(*index,oracle);
            auto& concrete=dynamic_cast<bm_tx_experiment::Adapter&>(*index);
            require(concrete.maintenance().tx_splits>0,"fault split path");
            index.reset();mem::tracking=false;require(mem::live==0,"fault destruction leak");
        }
        require(failures==allocations,"all allocation ordinals caught");
        std::cout<<"{\"suite\":\"fault\",\"position\":"<<position<<",\"injections\":"<<failures
                 <<",\"continuation_writes\":1000,\"checks\":"<<checks<<",\"mismatches\":0}\n";
    }
}
static void space(const char* rows_path,const char* ops_path) {
    const auto rows=bm_tx_trace::rows(rows_path);const auto ops=bm_tx_trace::operations(ops_path);
    for(bool candidate:{false,true}) {
        mem::begin();auto index=candidate?make_bm_tx_experiment_index({}):make_bm_split_experiment_index({});
        index->bulk_load(rows);const auto build_peak=mem::peak;mem::peak=mem::live;
        for(const auto& op:ops)(void)bm_tx_trace::apply(*index,op);
        const auto terminal=mem::live,maintenance_peak=mem::peak;
        if(candidate)dynamic_cast<bm_tx_experiment::Adapter&>(*index).verify();
        index.reset();mem::tracking=false;require(mem::live==0,"space destruction leak");
        std::cout<<"{\"suite\":\"space\",\"arm\":\""<<(candidate?"tx":"bm1")<<"\",\"terminal\":"<<terminal
                 <<",\"build_peak\":"<<build_peak<<",\"maintenance_peak\":"<<maintenance_peak<<"}\n";
    }
}
int main(int argc,char** argv) {
    try {
        if(argc==1)regression();
        else if(argc==3&&std::string(argv[1])=="fault")fault(argv[2]);
        else if(argc==4&&std::string(argv[1])=="differential")differential(argv[2],argv[3]);
        else if(argc==4&&std::string(argv[1])=="space")space(argv[2],argv[3]);
        else throw std::invalid_argument("fault corpus|--bytes OR differential|space rows ops");
        return 0;
    }catch(const std::exception& e){mem::disarm();std::cerr<<e.what()<<'\n';return 1;}
}

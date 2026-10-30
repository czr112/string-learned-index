// Read-only factory, owning scan, actual allocation ordinals and existing verify seam.
#define main bm_snapshot_legacy_entry
#include "bm_tx_query.cpp"
#undef main
#include "bm_model16_snapshot_adapter.hpp"
#include "bm_followup_memory.hpp"
#include "bm_tx_trace.hpp"

namespace mem=bm_followup_memory;
static std::uint64_t snapshot_checks=0;
static void check(bool ok,const char* why) {
    ++snapshot_checks;if(!ok)throw std::logic_error(why);
}
static void all_rows(const sib::Index& index,const std::vector<sib::Row>& expected) {
    const auto actual=index.scan("",SIZE_MAX);check(actual.size()==expected.size(),"snapshot cardinality");
    for(std::size_t i=0;i<expected.size();++i) {
        check(actual[i].key==expected[i].key&&actual[i].value==expected[i].value,"snapshot ordered rows");
        check(index.find(expected[i].key)==std::optional<sib::Value>(expected[i].value),"snapshot all finds");
    }
    if(auto* p=dynamic_cast<const bm_model16_snapshot::Adapter*>(&index))p->verify();
}
static void rejection(sib::Index& index) {
    for(const auto key:{std::string_view(""),std::string_view("~absent/snapshot")}) {
        unsigned throws=0;
        try{index.insert(key,7);}catch(const sib::UnsupportedOperation&){++throws;}
        try{index.update(key,7);}catch(const sib::UnsupportedOperation&){++throws;}
        try{index.erase(key);}catch(const sib::UnsupportedOperation&){++throws;}
        try{index.range_sum(key,key);}catch(const sib::UnsupportedOperation&){++throws;}
        check(throws==4,"snapshot mutation rejection");
    }
}
struct Space {std::size_t terminal,peak;std::uint64_t queries;};
static Space profile(sib::Factory factory,const Case& test) {
    mem::begin();auto index=factory({});index->bulk_load(test.terminal);
    Space result{mem::live,mem::peak,0};mem::tracking=false;
    all_rows(*index,test.terminal);
    if(dynamic_cast<bm_model16_snapshot::Adapter*>(index.get()))rejection(*index);
    all_rows(*index,test.terminal);
    mem::arm(0);
    for(const auto& group:test.query.groups)for(const auto& query:group.queries) {
        check(index->find(query.key)==(query.hit?std::optional<sib::Value>(query.value):std::nullopt),"snapshot historical answer");
        ++result.queries;
    }
    check(mem::disarm()==0,"snapshot find allocation");
    index.reset();check(mem::live==0,"snapshot profile leak");return result;
}
static void emit_profile(const char* arm,const Space& p) {
    std::cout<<"{\"arm\":\""<<arm<<"\",\"terminal\":"<<p.terminal<<",\"build_peak\":"<<p.peak
             <<",\"queries\":"<<p.queries<<",\"find_allocations\":0,\"mismatches\":0}\n";
}
static std::vector<sib::Row> binary_rows(std::size_t n) {
    std::vector<sib::Row> rows;
    for(std::size_t i=0;i<n;++i) {
        std::string key;
        if(i){key=std::string("\0\x80",2);key.push_back(char((i-1)>>8));key.push_back(char((i-1)&255));
            if(i%7==0)key+=std::string(80,'z');}
        rows.push_back({std::move(key),i%2?UINT64_MAX-i:i});
    }
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return static_physical::compare(a.key,b.key)<0;});
    return rows;
}
static void continuation(const sib::Index& index,const std::vector<sib::Row>& rows) {
    mem::arm(0);
    for(unsigned i=0;i<1000;++i) {
        if(rows.empty())check(!index.find("absent"),"empty retry read");
        else {const auto& row=rows[(std::size_t(i)*7919)%rows.size()];
            check(index.find(row.key)==std::optional<sib::Value>(row.value),"retry continuation read");}
    }
    check(mem::disarm()==0,"retry read allocated");
}
static std::uint64_t build_allocations(const std::vector<sib::Row>& rows) {
    mem::begin();auto index=make_bm_model16_snapshot_index({});mem::arm(0);index->bulk_load(rows);
    const auto count=mem::disarm();mem::tracking=false;index.reset();check(mem::live==0,"build count leak");return count;
}
static void build_faults(const std::vector<sib::Row>& rows) {
    const auto count=build_allocations(rows);
    for(std::uint64_t ordinal=1;ordinal<=count+1;++ordinal) {
        mem::begin();auto index=make_bm_model16_snapshot_index({});const auto before=mem::live;
        mem::arm(ordinal);bool failed=false;
        try{index->bulk_load(rows);}catch(const std::bad_alloc&){failed=true;}
        const auto calls=mem::disarm();mem::tracking=false;
        check(failed==(ordinal<=count),"bulk fault ordinal");
        if(failed) {
            check(calls==ordinal&&mem::live==before,"bulk failure byte restoration");
            bool unbuilt=false;try{index->find("");}catch(const std::logic_error&){unbuilt=true;}
            check(unbuilt,"bulk failure published partial index");rejection(*index);
            mem::tracking=true;index->bulk_load(rows);mem::tracking=false;
        }
        all_rows(*index,rows);continuation(*index,rows);index.reset();check(mem::live==0,"bulk fault destruction leak");
        std::cout<<"{\"path\":\"bulk_load\",\"ordinal\":"<<ordinal<<",\"allocation_count\":"<<count
                 <<",\"injected\":"<<(failed?"true":"false")<<",\"continuation_reads\":1000,\"mismatches\":0}\n";
    }
}
static void scan_faults(const std::vector<sib::Row>& rows) {
    mem::begin();auto index=make_bm_model16_snapshot_index({});index->bulk_load(rows);mem::tracking=false;
    mem::arm(0);{const auto output=index->scan("",SIZE_MAX);check(output.size()==rows.size(),"scan count rows");}
    const auto count=mem::disarm();
    for(std::uint64_t ordinal=1;ordinal<=count+1;++ordinal) {
        const auto before=mem::live;mem::tracking=true;mem::arm(ordinal);bool failed=false;
        try{const auto output=index->scan("",SIZE_MAX);check(output.size()==rows.size(),"scan positive control");}
        catch(const std::bad_alloc&){failed=true;}
        const auto calls=mem::disarm();mem::tracking=false;
        check(failed==(ordinal<=count),"scan fault ordinal");
        check(mem::live==before,"scan failure byte restoration");if(failed)check(calls==ordinal,"scan allocation order");
        all_rows(*index,rows);continuation(*index,rows);
        std::cout<<"{\"path\":\"scan\",\"ordinal\":"<<ordinal<<",\"allocation_count\":"<<count
                 <<",\"injected\":"<<(failed?"true":"false")<<",\"continuation_reads\":1000,\"mismatches\":0}\n";
    }
    index.reset();check(mem::live==0,"scan fault destruction leak");
}
int main(int argc,char** argv) {
    try {
        if(argc==7&&std::string(argv[1])=="profile") {
            Case test(argv,0);
            emit_profile("tx8_snapshot",profile(make_bm_tx_experiment_index,test));
            emit_profile("m16_snapshot",profile(make_bm_model16_snapshot_index,test));
        } else if(argc==4&&std::string(argv[1])=="fault") {
            std::vector<sib::Row> rows;
            if(std::string(argv[2])=="--bytes")rows=binary_rows(std::stoul(argv[3]));
            else {rows=bm_tx_trace::rows(argv[2]);check(rows.size()>=2049,"fault corpus size");rows.resize(2049);}
            build_faults(rows);scan_faults(rows);
            std::cout<<"{\"suite\":\"fault_summary\",\"rows\":"<<rows.size()<<",\"checks\":"<<snapshot_checks<<",\"mismatches\":0}\n";
        } else throw std::invalid_argument("profile rows ops pool final queries | fault --bytes count | fault corpus ignored");
    }catch(const std::exception& error){mem::disarm();mem::tracking=false;std::cerr<<error.what()<<'\n';return 1;}
}

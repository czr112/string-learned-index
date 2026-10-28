#include "bm_space_adapter.hpp"
#include "benchmark/factories.h"
#include "../adapters/art/art_adapter.h"
static string_index_bench::v1::Factory space_selected=nullptr;
static std::unique_ptr<string_index_bench::v1::Index> make_space_selected(
    const string_index_bench::v1::Parameters& p){return space_selected(p);}
#define ACCEPTANCE_FACTORY_HEADER "benchmark/factories.h"
#define ACCEPTANCE_FACTORY_SYMBOL make_space_selected
#define main space_common_main
#include "borrowed_contract_acceptance.cpp"
#undef main
#include "allocation_trace.hpp"
#include "static_input.hpp"
#include <chrono>
#include <fstream>
#include <malloc.h>
#include <sstream>

namespace uni=string_index_bench::unified;
namespace at=allocation_trace;
static void require(bool condition,const char* message) {if(!condition)throw std::logic_error(message);}
static std::uint64_t heap(const std::vector<sib::Row>& rows) {
    std::uint64_t bytes=rows.capacity()*sizeof(sib::Row);
    for(const auto& r:rows) {
        const auto p=reinterpret_cast<std::uintptr_t>(r.key.data());
        const auto b=reinterpret_cast<std::uintptr_t>(&r.key);
        if(p<b||p>=b+sizeof(r.key))bytes+=r.key.capacity()+1;
    }
    return bytes;
}
static void totals(std::ostream& o,const at::Totals& t) {
    o<<"{\"requested\":"<<t.requested<<",\"usable\":"<<t.usable<<",\"objects\":"<<t.objects
     <<",\"allocations\":"<<t.allocations<<",\"frees\":"<<t.frees
     <<",\"total_requested\":"<<t.total_requested<<",\"total_freed\":"<<t.total_freed
     <<",\"total_usable\":"<<t.total_usable<<",\"freed_usable\":"<<t.freed_usable
     <<",\"peak_requested\":"<<t.peak_requested<<",\"peak_usable\":"<<t.peak_usable
     <<",\"failed\":"<<t.failed<<",\"kinds\":[";
    for(unsigned i=0;i<at::kind_count;++i){if(i)o<<',';o<<t.kinds[i];}o<<"]}";
}
static void snapshot(std::ostream& o,const at::Snapshot& s) {
    if(!BM_ALLOCATION_TRACE){o<<"null";return;}
    const char* names[]={"off","input","index","query","scan","test"};
    o<<"{\"peak_requested\":"<<s.phase_peak_requested<<",\"peak_usable\":"<<s.phase_peak_usable
     <<",\"registry_bytes\":"<<s.registry_bytes<<",\"registry_live\":"<<s.registry_live;
    for(unsigned i=1;i<at::domain_count;++i){o<<",\""<<names[i]<<"\":";totals(o,s.domains[i]);}o<<'}';
}
struct SystemMemory {std::uint64_t allocated,rss_kib,hwm_kib;};
static SystemMemory system_memory() {
    SystemMemory m{mallinfo2().uordblks,0,0};std::ifstream f("/proc/self/status");std::string line;
    while(std::getline(f,line)) {
        if(line.rfind("VmRSS:",0)==0)m.rss_kib=std::stoull(line.substr(6));
        if(line.rfind("VmHWM:",0)==0)m.hwm_kib=std::stoull(line.substr(6));
    }return m;
}
static void system_json(std::ostream& o,const SystemMemory& s) {
    o<<"{\"mallinfo2_uordblks\":"<<s.allocated<<",\"rss_kib\":"<<s.rss_kib<<",\"hwm_kib\":"<<s.hwm_kib<<'}';
}
static void option(std::ostream& o,std::optional<std::uint64_t> x){if(x)o<<*x;else o<<"null";}
int main(int argc,char** argv) {
    try {
        require(argc>=3,"arm rows queries | arm --contract");
        const std::string arm=argv[1];
        if(arm=="bm_base")space_selected=bm_space::make_base;
        else if(arm=="native")space_selected=make_block_residual_delta_shortdir_first;
        else if(arm=="ART")space_selected=make_art_index;
        else throw std::invalid_argument("unknown arm");
        if(std::string(argv[2])=="--contract") {
            const auto initial=checks;const auto rc=space_common_main();
            std::cout<<"{\"arm\":\""<<arm<<"\",\"acceptance_checks\":"<<checks-initial<<",\"rc\":"<<rc<<"}\n";
            return rc;
        }
        require(argc==4,"arm rows queries");
        // Prewarm process-global streams and locale outside index phases.
        {std::ostringstream s;s<<42;std::cout<<std::flush;}
        std::vector<sib::Row> rows;
        at::begin(at::input);
        {auto stream=static_blocks::buffered(argv[2]);rows=uni::read_rows(stream);}
        at::end();const auto input_snapshot=at::snapshot();const auto input_bytes=heap(rows);
        if(BM_ALLOCATION_TRACE)require(input_snapshot.domains[at::input].requested==input_bytes,"input heap coverage mismatch");
        require(!rows.empty(),"empty input");const auto row_count=rows.size();
        std::vector<uni::Query> queries;
        {auto stream=static_blocks::buffered(argv[3]);queries=uni::read_queries(stream);}
        std::map<std::string,std::uint64_t> oracle;
        for(const auto& r:rows)require(oracle.emplace(r.key,r.value).second,"duplicate row");
        std::size_t hits=0;
        for(std::size_t i=0;i<queries.size();++i) {
            auto& q=queries[i];if(i%10==9){q.key+="~f2/miss";while(oracle.count(q.key))q.key+="~";}
            const auto it=oracle.find(q.key);q.expected_present=it!=oracle.end();
            q.expected_value=q.expected_present?it->second:0;hits+=q.expected_present;
        }
        const bool retain=arm=="native";const auto before=system_memory();
        at::begin(at::index,retain);
        const auto started=std::chrono::steady_clock::now();
        auto index=space_selected({});index->bulk_load(rows);
        const auto build_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
        at::end();const auto built=at::snapshot();const auto after_build=system_memory();
        const auto reported=index->memory_usage();
        std::uint64_t semantic=0,checksum=0;
        at::begin(at::query,retain);
        for(const auto& r:rows){require(index->find(r.key)==std::optional<sib::Value>(r.value),"row find mismatch");++semantic;}
        for(const auto& q:queries) {
            const auto expected=q.expected_present?std::optional<sib::Value>(q.expected_value):std::nullopt;
            const auto found=index->find(q.key);require(found==expected,"query find mismatch");++semantic;
            checksum=checksum*UINT64_C(1099511628211)+(found?(*found^UINT64_C(0x9e3779b97f4a7c15)):UINT64_C(0xd1b54a32d192ed03));
        }
        at::end();const auto found=at::snapshot();
        if(BM_ALLOCATION_TRACE) {
            require(found.domains[at::query].objects==0,"query allocations retained");
            if(arm=="bm_base")require(found.domains[at::query].allocations==0,"BM find allocated");
            require(found.domains[at::index].requested==built.domains[at::index].requested,"find changed index storage");
        }
        at::begin(at::scan,retain);auto output=index->scan("",row_count+1);at::end();
        const auto scanned=at::snapshot();const auto output_bytes=heap(output);
        if(BM_ALLOCATION_TRACE)require(scanned.domains[at::scan].requested==output_bytes,"scan output coverage mismatch");
        auto validate_output=[&] {
            require(output.size()==oracle.size(),"scan length");auto it=oracle.begin();
            for(const auto& r:output){require(r.key==it->first&&r.value==it->second,"scan content");++it;++semantic;}
        };
        validate_output();const auto after_scan=system_memory();
        index.reset();const auto destroyed=at::snapshot();
        if(BM_ALLOCATION_TRACE)require(destroyed.domains[at::index].objects==0,"index destruction retained allocations");
        std::vector<sib::Row>().swap(rows);const auto no_input=at::snapshot();
        if(BM_ALLOCATION_TRACE)require(no_input.domains[at::input].objects==0,"input destruction retained allocations");
        validate_output();std::vector<sib::Row>().swap(output);const auto released=at::snapshot();
        if(BM_ALLOCATION_TRACE)require(released.registry_live==0,"tracked allocation leak");
        std::ostringstream out;
        out<<"{\"arm\":\""<<arm<<"\",\"tracking_enabled\":"<<(BM_ALLOCATION_TRACE?"true":"false")
           <<",\"rows\":"<<row_count<<",\"queries\":"<<queries.size()<<",\"hits\":"<<hits
           <<",\"semantic_checks\":"<<semantic<<",\"checksum\":"<<checksum<<",\"input_heap_bytes\":"<<input_bytes
           <<",\"output_heap_bytes\":"<<output_bytes<<",\"retains_input\":"<<(retain?"true":"false")
           <<",\"build_ns\":"<<build_ns<<",\"self_report\":{\"owned\":";option(out,reported.index_owned_bytes);
        out<<",\"adapter\":";option(out,reported.adapter_owned_bytes);out<<",\"retained\":";option(out,reported.retained_input_bytes);out<<'}';
        const char* labels[]={"input_loaded","built","found","scanned","index_destroyed","input_destroyed","released"};
        const at::Snapshot states[]={input_snapshot,built,found,scanned,destroyed,no_input,released};
        for(unsigned i=0;i<7;++i){out<<",\""<<labels[i]<<"\":";snapshot(out,states[i]);}
        out<<",\"system_before\":";system_json(out,before);out<<",\"system_built\":";system_json(out,after_build);
        out<<",\"system_scanned\":";system_json(out,after_scan);out<<"}\n";
        std::cout<<out.str();
    }catch(const std::exception& e){at::end();std::cerr<<e.what()<<'\n';return 1;}
}

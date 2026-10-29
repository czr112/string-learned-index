// Native Windows measurement adapter. All index/replay/group validation is reused.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define main bm_query_native_legacy_entry
#include "bm_tx_query.cpp"
#undef main

static std::uint64_t cpu_time_ns() {
    FILETIME created{},exited{},kernel{},user{};
    need(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user)!=0,"thread CPU times");
    const auto value=[](const FILETIME& t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    return (value(kernel)+value(user))*100;
}
static std::uint64_t thread_cycles() {
    ULONG64 cycles=0;need(QueryThreadCycleTime(GetCurrentThread(),&cycles)!=0,"thread cycles");return cycles;
}
struct NativeBatch {Measurement measurement;std::uint64_t cpu_ns,cycles;DWORD before,after;};
static NativeBatch batch(const sib::Index& index,const Group& group,std::size_t loops,DWORD cpu) {
    const auto before=GetCurrentProcessorNumber();const auto cpu_start=cpu_time_ns(),cycles_start=thread_cycles();
    auto measured=timed(index,group,loops);
    const auto cycles_end=thread_cycles(),cpu_end=cpu_time_ns();const auto after=GetCurrentProcessorNumber();
    need(before==cpu&&after==cpu,"native processor changed");
    need(cycles_end>=cycles_start&&cpu_end>=cpu_start,"native counters monotonic");
    return {measured,cpu_end-cpu_start,cycles_end-cycles_start,before,after};
}
static void emit_batch(const NativeBatch& b) {
    const auto& m=b.measurement;
    std::cout<<"\"ns\":"<<m.ns<<",\"lookups\":"<<m.lookups<<",\"checksum\":"<<m.checksum
             <<",\"cpu_ns\":"<<b.cpu_ns<<",\"cycles\":"<<b.cycles
             <<",\"cpu_before\":"<<b.before<<",\"cpu_after\":"<<b.after;
}
int main(int argc,char** argv) {
    try {
        need(argc==9,"check|run rows ops pool final queries process min_ms");
        const std::string mode=argv[1];need(mode=="check"||mode=="run","native mode");
        const int process=std::stoi(argv[7]);const auto minimum_ms=std::stoull(argv[8]);
        need(process>=0&&process<16,"process range");need(minimum_ms==250||minimum_ms==1000,"batch floor");
        need(GetActiveProcessorGroupCount()==1,"single processor group required");
        DWORD_PTR allowed=0,system=0;
        need(GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system)!=0&&allowed,"native affinity query");
        const auto selected=allowed&(~allowed+1);
        need(SetThreadAffinityMask(GetCurrentThread(),selected)!=0,"native affinity set");
        DWORD cpu=0;while(!(selected&(DWORD_PTR(1)<<cpu)))++cpu;
        Case test(argv,process);
        if(mode=="check") {
            std::cout<<"{\"passed\":true,\"cpu\":"<<cpu<<",\"affinity\":"<<selected<<',';
            metadata(test);std::cout<<"}\n";return 0;
        }
        const auto found=std::find_if(test.query.groups.begin(),test.query.groups.end(),[](const auto& g){return g.name=="mixed90";});
        need(found!=test.query.groups.end()&&!found->queries.empty(),"native primary group");const auto& group=*found;
        struct Trial {int state;std::size_t loops;NativeBatch measurement;};
        std::vector<Trial> calibration;std::size_t loops=1;
        const auto cap=std::size_t(32768000)/group.queries.size();
        while(true) {
            bool sufficient=true;
            for(int state=0;state<4;++state) {
                const auto value=batch(*test.states[state],group,loops,cpu);calibration.push_back({state,loops,value});
                sufficient=sufficient&&value.measurement.ns>=minimum_ms*1000000;
            }
            if(sufficient)break;need(loops<=cap/2,"native calibration cap");loops*=2;
        }
        struct Warmup {std::uint64_t ns,lookups,checksum;};std::array<Warmup,4> warmups{};
        for(int state=0;state<4;++state) {
            const auto start=Clock::now();std::uint64_t passes=0,sum=0;
            do {const auto value=lookup(*test.states[state],group,loops);need(value==expected(group,loops),"native warm checksum");sum+=value;++passes;}
            while(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count()<250);
            warmups[state]={std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()),passes*loops*group.queries.size(),sum};
        }
        struct Record {int state,round,position;NativeBatch measurement;};std::vector<Record> records;records.reserve(32);
        for(int round=0;round<8;++round)for(int position=0;position<4;++position) {
            const auto state=order[(process+round)%4][position];
            records.push_back({state,round,position,batch(*test.states[state],group,loops,cpu)});
        }
        std::cout<<"{\"process\":"<<process<<",\"minimum_ms\":"<<minimum_ms<<",\"loops\":"<<loops
                 <<",\"cpu\":"<<cpu<<",\"affinity\":"<<selected<<",\"group\":\"mixed90\",";metadata(test);
        std::cout<<",\"calibration\":[";bool first=true;
        for(const auto& t:calibration) {
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"state\":\""<<names[t.state]<<"\",\"loops\":"<<t.loops<<',';emit_batch(t.measurement);std::cout<<'}';
        }
        std::cout<<"],\"warmups\":[";first=true;
        for(int state=0;state<4;++state) {
            if(!first)std::cout<<',';first=false;const auto& w=warmups[state];
            std::cout<<"{\"state\":\""<<names[state]<<"\",\"ns\":"<<w.ns<<",\"lookups\":"<<w.lookups<<",\"checksum\":"<<w.checksum<<'}';
        }
        std::cout<<"],\"measurements\":[";first=true;
        for(const auto& r:records) {
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"state\":\""<<names[r.state]<<"\",\"round\":"<<r.round<<",\"position\":"<<r.position<<',';
            emit_batch(r.measurement);std::cout<<'}';
        }
        std::cout<<"]}\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

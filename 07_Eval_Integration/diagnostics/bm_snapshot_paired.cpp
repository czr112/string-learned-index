// Fixed ABBA/BAAB pairs. Calibration peers are identical TX8 factories.
#include BM_PAIR_NATIVE_HEADER
#include "bm_model16_snapshot_adapter.hpp"

static const char* pair_names[4]={"A0","A1","B0","B1"};
int main(int argc,char** argv) {
    try {
        need(argc==10,"check|run rows ops pool final queries process min_ms calibrate|confirm");
        const std::string mode=argv[1],phase=argv[9];
        need(mode=="check"||mode=="run","paired mode");need(phase=="calibrate"||phase=="confirm","paired phase");
        const int process=std::stoi(argv[7]);need(process>=0&&process<16,"paired process");
        need(std::stoull(argv[8])==1000,"paired fixed floor");
        need(GetPriorityClass(GetCurrentProcess())==NORMAL_PRIORITY_CLASS&&
             GetThreadPriority(GetCurrentThread())==THREAD_PRIORITY_NORMAL,"paired normal priority");
        need(GetActiveProcessorGroupCount()==1,"paired one processor group");
        DWORD_PTR allowed=0,system=0;need(GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system)!=0&&allowed,"paired affinity query");
        const auto selected=allowed&(~allowed+1);need(SetThreadAffinityMask(GetCurrentThread(),selected)!=0,"paired affinity set");
        DWORD cpu=0;while(!(selected&(DWORD_PTR(1)<<cpu)))++cpu;
        Case test(argv,process);for(auto& index:test.states)index.reset();
        std::array<std::unique_ptr<sib::Index>,4> indexes;
        std::uint64_t structural_checks=0;
        for(int pos=0;pos<4;++pos) {
            const auto state=order[process%4][pos];
            const bool model16=phase=="confirm"&&state>=2;
            indexes[state]=model16?make_bm_model16_snapshot_index({}):make_bm_tx_experiment_index({});
            indexes[state]->bulk_load(test.terminal);
            const auto descriptor=indexes[state]->descriptor();
            need(descriptor.implementation_version==BM_TX_CORE_SHA256,"paired compiled closure identity");
            need(descriptor.adapter_version==(model16?BM_MODEL16_ADAPTER_SHA256:BM_TX_ADAPTER_SHA256),"paired adapter identity");
            need((descriptor.adaptation_notes.find("segment_budget=16")!=std::string::npos)==model16,"paired role budget");
            if(auto* concrete=dynamic_cast<bm_tx_experiment::Adapter*>(indexes[state].get()))structural_checks+=concrete->verify();
            if(auto* concrete=dynamic_cast<bm_model16_snapshot::Adapter*>(indexes[state].get()))structural_checks+=concrete->verify();
            const auto rows=indexes[state]->scan("",SIZE_MAX);need(rows.size()==test.terminal.size(),"paired terminal size");
            for(std::size_t i=0;i<rows.size();++i)
                need(rows[i].key==test.terminal[i].key&&rows[i].value==test.terminal[i].value,"paired terminal rows");
            for(const auto& group:test.query.groups)for(const auto& q:group.queries)
                need(indexes[state]->find(q.key)==(q.hit?std::optional<sib::Value>(q.value):std::nullopt),"paired semantic query");
        }
        const auto found=std::find_if(test.query.groups.begin(),test.query.groups.end(),[](const auto& g){return g.name=="mixed90";});
        need(found!=test.query.groups.end()&&!found->queries.empty(),"paired primary group");const auto& group=*found;
        const auto common_metadata=[&] {
            std::cout<<"\"process\":"<<process<<",\"phase\":\""<<phase<<"\",\"cpu\":"<<cpu<<",\"affinity\":"<<selected
                     <<",\"source_pool_rows\":"<<test.pool.size()<<",\"initial_rows\":"<<test.initial.size()
                     <<",\"final_rows\":"<<test.terminal.size()<<",\"group\":\"mixed90\",\"model16_measured\":"
                     <<(phase=="confirm"?"true":"false")<<",\"structural_checks\":"<<structural_checks
                     <<",\"runtime_modules\":";emit_modules();
        };
        if(mode=="check"){std::cout<<"{\"passed\":true,";common_metadata();std::cout<<"}\n";return 0;}
        struct Trial {int state;std::size_t loops;NativeBatch value;};std::vector<Trial> trials;
        std::size_t loops=1;const auto cap=std::size_t(32768000)/group.queries.size();
        while(true) {
            bool sufficient=true;
            for(int state=0;state<4;++state) {
                const auto value=batch(*indexes[state],group,loops,cpu);trials.push_back({state,loops,value});
                sufficient=sufficient&&value.measurement.ns>=1000000000ULL;
            }
            if(sufficient)break;need(loops<=cap/2,"paired calibration cap");loops*=2;
        }
        struct Warmup {std::uint64_t ns,lookups,checksum;};std::array<Warmup,4> warmups{};
        for(int state=0;state<4;++state) {
            const auto start=Clock::now();std::uint64_t passes=0,sum=0;
            do {const auto value=lookup(*indexes[state],group,loops);need(value==expected(group,loops),"paired warm checksum");sum+=value;++passes;}
            while(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count()<250);
            warmups[state]={std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()),passes*loops*group.queries.size(),sum};
        }
        struct Record {int quad,position,state;std::uint64_t start,end;NativeBatch value;};
        std::vector<Record> records;records.reserve(32);const auto origin=Clock::now();
        for(int quad=0;quad<8;++quad) {
            const int a=quad%2,b=2+(quad/2)%2;std::array<int,4> sequence{a,b,b,a};
            if((quad/4+process)%2)sequence={b,a,a,b};
            for(int pos=0;pos<4;++pos) {
                const auto state=sequence[pos];const auto start=Clock::now();const auto value=batch(*indexes[state],group,loops,cpu);
                const auto end=Clock::now();records.push_back({quad,pos,state,
                    std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(start-origin).count()),
                    std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(end-origin).count()),value});
            }
        }
        std::cout<<'{';common_metadata();std::cout<<",\"loops\":"<<loops<<",\"calibration\":[";bool first=true;
        for(const auto& trial:trials) {
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"state\":\""<<pair_names[trial.state]<<"\",\"loops\":"<<trial.loops<<',';emit_batch(trial.value);std::cout<<'}';
        }
        std::cout<<"],\"warmups\":[";first=true;
        for(int state=0;state<4;++state) {
            if(!first)std::cout<<',';first=false;const auto& w=warmups[state];
            std::cout<<"{\"state\":\""<<pair_names[state]<<"\",\"ns\":"<<w.ns<<",\"lookups\":"<<w.lookups<<",\"checksum\":"<<w.checksum<<'}';
        }
        std::cout<<"],\"measurements\":[";first=true;
        for(const auto& record:records) {
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"quad\":"<<record.quad<<",\"position\":"<<record.position<<",\"state\":\""<<pair_names[record.state]
                     <<"\",\"envelope_start_ns\":"<<record.start<<",\"envelope_end_ns\":"<<record.end<<',';
            emit_batch(record.value);std::cout<<'}';
        }
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

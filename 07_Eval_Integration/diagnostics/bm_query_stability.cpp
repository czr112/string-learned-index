// Reuse the accepted public factory, replay and historical grouping validation.
#define main bm_query_legacy_entry
#include "bm_tx_query.cpp"
#undef main
#include <ctime>
#include <sched.h>
#include <sys/resource.h>

struct StableBatch {
    Measurement measurement;
    std::uint64_t cpu_ns;
    long voluntary,involuntary,minor_faults,major_faults;
    int cpu_before,cpu_after;
};
static std::uint64_t thread_ns() {
    timespec value{};need(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)==0,"thread clock");
    return std::uint64_t(value.tv_sec)*1000000000+std::uint64_t(value.tv_nsec);
}
static StableBatch stable_batch(const sib::Index& index,const Group& group,std::size_t loops) {
    rusage before{},after{};
    need(getrusage(RUSAGE_THREAD,&before)==0,"usage before");
    const int cpu_before=sched_getcpu();const auto cpu_start=thread_ns();
    auto measurement=timed(index,group,loops);
    const auto cpu_end=thread_ns();const int cpu_after=sched_getcpu();
    need(getrusage(RUSAGE_THREAD,&after)==0,"usage after");
    return {measurement,cpu_end-cpu_start,after.ru_nvcsw-before.ru_nvcsw,
            after.ru_nivcsw-before.ru_nivcsw,after.ru_minflt-before.ru_minflt,
            after.ru_majflt-before.ru_majflt,cpu_before,cpu_after};
}
int main(int argc,char** argv) {
    try {
        need(argc==9,"run rows ops pool final queries process min_ms");
        need(std::string(argv[1])=="run","unknown stability mode");
        const int process=std::stoi(argv[7]);const auto minimum_ms=std::stoull(argv[8]);
        need(process>=0&&process<16,"process range");
        need(minimum_ms==250||minimum_ms==1000,"registered batch floors only");
        Case test(argv,process);
        const auto found=std::find_if(test.query.groups.begin(),test.query.groups.end(),
                                     [](const auto& group){return group.name=="mixed90";});
        need(found!=test.query.groups.end()&&!found->queries.empty(),"primary queries");
        const auto& group=*found;
        const auto limit=std::size_t(32768000)/group.queries.size();
        std::size_t loops=1;
        std::vector<std::array<std::uint64_t,5>> calibration;
        while(true) {
            bool enough=true;
            for(int state=0;state<4;++state) {
                const auto sample=stable_batch(*test.states[state],group,loops);
                calibration.push_back({std::uint64_t(state),loops,sample.measurement.ns,
                                       sample.cpu_ns,sample.measurement.checksum});
                enough=enough&&sample.measurement.ns>=minimum_ms*1000000;
            }
            if(enough)break;
            need(loops<=limit/2,"calibration lookup cap");loops*=2;
        }
        for(int state=0;state<4;++state) {
            const auto start=Clock::now();
            do {need(lookup(*test.states[state],group,loops)==expected(group,loops),"warm checksum");}
            while(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count()<250);
        }
        struct Record {int state,round,position;StableBatch batch;};
        std::vector<Record> records;records.reserve(32);
        for(int round=0;round<8;++round)for(int position=0;position<4;++position) {
            const auto state=order[(process+round)%4][position];
            records.push_back({state,round,position,stable_batch(*test.states[state],group,loops)});
        }
        std::cout<<"{\"process\":"<<process<<",\"minimum_ms\":"<<minimum_ms
                 <<",\"loops\":"<<loops<<",\"group\":\"mixed90\",";metadata(test);
        std::cout<<",\"calibration\":[";bool first=true;
        for(const auto& c:calibration) {
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"state\":\""<<names[c[0]]<<"\",\"loops\":"<<c[1]
                     <<",\"ns\":"<<c[2]<<",\"cpu_ns\":"<<c[3]<<",\"checksum\":"<<c[4]<<'}';
        }
        std::cout<<"],\"measurements\":[";first=true;
        for(const auto& r:records) {
            if(!first)std::cout<<',';first=false;const auto& b=r.batch;const auto& m=b.measurement;
            std::cout<<"{\"state\":\""<<names[r.state]<<"\",\"round\":"<<r.round
                     <<",\"position\":"<<r.position<<",\"ns\":"<<m.ns<<",\"cpu_ns\":"<<b.cpu_ns
                     <<",\"lookups\":"<<m.lookups<<",\"checksum\":"<<m.checksum
                     <<",\"voluntary\":"<<b.voluntary<<",\"involuntary\":"<<b.involuntary
                     <<",\"minor_faults\":"<<b.minor_faults<<",\"major_faults\":"<<b.major_faults
                     <<",\"cpu_before\":"<<b.cpu_before<<",\"cpu_after\":"<<b.cpu_after<<'}';
        }
        std::cout<<"]}\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

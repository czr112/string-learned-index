// E6 benchmark: outer-route variants (baseline binary search / A prefix-integer /
// B Eytzinger layout) over one fixed leaf (context16, B=1024, S=16), same query
// protocol as E5. Final acceptance is end-to-end; locate_only is explanatory.
#include "data.hpp"
#include "route_aux.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace validation;
using Clock=std::chrono::steady_clock;
static volatile std::uint64_t sink=0;
static double ns(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::nano>(b-a).count();}
static std::uint64_t checksum(Result r){return r.found?r.value+0x9e3779b97f4a7c15ULL:0;}

struct Q {std::string owned;std::string key;std::uint64_t expected=0;bool hit=false;std::size_t region=0;};

static std::vector<Q> make_q(const std::vector<Pair>&keys,std::size_t count,std::uint64_t seed){
    std::mt19937_64 rng(seed);std::vector<Q> out;out.reserve(count);
    for(std::size_t i=0;i<count;++i){
        const auto&kv=keys[rng()%keys.size()];Q q;q.hit=rng()%2==0;q.owned=kv.first;
        if(!q.hit){q.owned+="~missing~";
            auto it=std::lower_bound(keys.begin(),keys.end(),q.owned,[](const Pair&a,const std::string&b){return compare(a.first,b)<0;});
            if(it!=keys.end()&&it->first==q.owned)throw std::runtime_error("negative query exists");}
        q.key=q.owned;if(q.hit)q.expected=kv.second;
        out.push_back(std::move(q));
    }
    std::shuffle(out.begin(),out.end(),rng);return out;
}

int main(int argc,char**argv){try{
#ifdef _WIN32
    SetProcessAffinityMask(GetCurrentProcess(),1);
    SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
#endif
    if(argc<4)throw std::invalid_argument("bench_e6 KIND N OUTDIR [TRANCOCVS] [QUERIES] [SEEDS] [ROUNDS]");
    const std::string kind=argv[1];const std::size_t n=std::stoull(argv[2]);const std::string dir=argv[3];
    const std::string csv=argc>4?argv[4]:"";
    const std::size_t nq=argc>5?std::stoull(argv[5]):131072;
    const int seeds=argc>6?std::stoi(argv[6]):3,rounds=argc>7?std::stoi(argv[7]):5;
    if(n<4096||nq<1000||seeds<1||rounds<1)throw std::invalid_argument("size");

    const auto keys=make_data(kind,n,csv);
    Config c;c.block_bytes=1024;c.slots=16;c.model_kind=ModelKind::Context16;c.region_keys=2048;
    Index idx(c);
    const auto b0=Clock::now();idx.bulk_load(keys);const double build_ms=ns(b0,Clock::now())/1e6;

    RouterA ra;RouterB rb;
    const auto r0=Clock::now();ra.sync(idx);rb.sync(idx);const double router_build_ms=ns(r0,Clock::now())/1e6;

    const auto da=ra.diag();
    std::cerr<<"fences="<<da.fences<<" distinct_u="<<da.distinct_u<<" equal_share="<<da.equal_share
             <<" max_equal_run="<<da.max_equal_run<<" build_ms="<<build_ms
             <<" router_meta_bytes A="<<ra.metadata_bytes()<<" B="<<rb.metadata_bytes()
             <<" router_build_ms="<<router_build_ms<<"\n";

    const Mode mode{Route::Interval,Prefetch::Off,true,true};
    struct Method {std::string name;bool locate_only;std::function<std::uint64_t(const Q&)> fn;};
    std::vector<Method> M;
    M.push_back({"locate_baseline",true,[&](const Q&q){return static_cast<std::uint64_t>(idx.locate(q.key));}});
    M.push_back({"locate_A",true,[&](const Q&q){return static_cast<std::uint64_t>(ra.locate(idx,q.key));}});
    M.push_back({"locate_B",true,[&](const Q&q){return static_cast<std::uint64_t>(rb.locate(idx,q.key));}});
    M.push_back({"end_baseline",false,[&](const Q&q){return checksum(idx.find(q.key,mode));}});
    M.push_back({"end_A",false,[&](const Q&q){return checksum(idx.region(ra.locate(idx,q.key)).find(q.key,mode));}});
    M.push_back({"end_B",false,[&](const Q&q){return checksum(idx.region(rb.locate(idx,q.key)).find(q.key,mode));}});

    std::ofstream out(dir+"/route_"+kind+".csv");
    out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);
    out<<"kind,n,method,seed,round,ns_per_op,checksum\n";

    for(int seed=0;seed<seeds;++seed){
        auto q=make_q(keys,nq,2026091310+static_cast<std::uint64_t>(seed));
        // correctness first: every variant must reproduce the baseline route and result
        for(const auto&v:q){
            const std::size_t base=idx.locate(v.key);
            if(ra.locate(idx,v.key)!=base)throw std::runtime_error("route A mismatch");
            if(rb.locate(idx,v.key)!=base)throw std::runtime_error("route B mismatch");
            const auto r=idx.region(base).find(v.key,mode);
            if(checksum(r)!=checksum({v.hit,v.expected}))throw std::runtime_error("find mismatch");
        }
        std::uint64_t locate_sum=0;for(const auto&v:q)locate_sum+=static_cast<std::uint64_t>(idx.locate(v.key));
        std::uint64_t find_want=0;for(const auto&v:q)find_want+=checksum({v.hit,v.expected});
        std::vector<std::size_t> order(M.size());std::iota(order.begin(),order.end(),0);
        std::mt19937_64 rng(2026091377+seed);
        for(int r=0;r<rounds;++r){
            std::shuffle(order.begin(),order.end(),rng);
            for(auto which:order){
                const auto&m=M[which];
                std::uint64_t warm=0;const auto nw=std::min<std::size_t>(20000,q.size());
                for(std::size_t i=0;i<nw;++i)warm+=m.fn(q[i]);
                sink=warm;
                std::uint64_t sum=0;const auto t0=Clock::now();for(const auto&v:q)sum+=m.fn(v);const auto t1=Clock::now();
                if(m.locate_only){if(sum!=locate_sum)throw std::runtime_error("locate checksum "+m.name);}
                else if(sum!=find_want)throw std::runtime_error("timed checksum "+m.name);
                out<<kind<<','<<keys.size()<<','<<m.name<<','<<seed<<','<<r<<','<<ns(t0,t1)/q.size()<<','<<sum<<'\n';
            }
            out.flush();
        }
        std::cerr<<"seed "<<seed<<" done\n";
    }
    std::cerr<<"COMPLETE "<<kind<<"\n";
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<"\n";return 1;}}

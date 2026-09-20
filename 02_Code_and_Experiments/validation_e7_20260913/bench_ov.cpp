// E7 benchmark: same overflow records, three lookup policies (Linear/Binary/Hybrid).
// Identity separation: tuning queries from rank%4==0 keys pick the hybrid threshold,
// the choice is frozen to frozen_ov_KIND.json, and only then are test queries
// (rank%4!=0 keys) generated and timed. Hit-class and comparison counters are
// sampled on test queries outside the timed batches.
#include "data.hpp"
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

struct Q {std::string owned;std::string key;std::uint64_t expected=0;bool hit=false;};
static std::vector<Q> make_q_on(const std::vector<Pair>&keys,const std::vector<std::size_t>&eligible,
                                std::size_t count,std::uint64_t seed){
    std::mt19937_64 rng(seed);std::vector<Q> out;out.reserve(count);
    for(std::size_t i=0;i<count;++i){
        const auto&kv=keys[eligible[rng()%eligible.size()]];Q q;q.hit=rng()%2==0;q.owned=kv.first;
        if(!q.hit){q.owned+="~missing~";
            auto it=std::lower_bound(keys.begin(),keys.end(),q.owned,[](const Pair&a,const std::string&b){return compare(a.first,b)<0;});
            if(it!=keys.end()&&it->first==q.owned)throw std::runtime_error("negative query exists");}
        q.key=q.owned;if(q.hit)q.expected=kv.second;
        out.push_back(std::move(q));
    }
    std::shuffle(out.begin(),out.end(),rng);return out;
}
static void eligible_split(const std::vector<Pair>&keys,std::vector<std::size_t>&tune,std::vector<std::size_t>&test){
    for(std::size_t i=0;i<keys.size();++i)((i%4==0)?tune:test).push_back(i);
}
int main(int argc,char**argv){try{
#ifdef _WIN32
    SetProcessAffinityMask(GetCurrentProcess(),1);
    SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
#endif
    if(argc<4)throw std::invalid_argument("bench_ov KIND N OUTDIR [TRANCOCVS]");
    const std::string kind=argv[1];const std::size_t n=std::stoull(argv[2]);const std::string dir=argv[3];
    const std::string csv=argc>4?argv[4]:"";
    if(n<8192)throw std::invalid_argument("size");
    const auto keys=make_data(kind,n,csv);
    Config c;c.block_bytes=1024;c.slots=16;c.model_kind=ModelKind::Context16;c.region_keys=2048;
    Index idx(c);
    const auto b0=Clock::now();idx.bulk_load(keys);
    std::cerr<<"built in "<<ns(b0,Clock::now())/1e6<<"ms overflow_keys="<<idx.overflow_count()<<"\n";

    std::vector<std::size_t> tune_e,test_e;eligible_split(keys,tune_e,test_e);
    // ---------- tuning phase: pick the hybrid threshold ----------
    const auto tq=make_q_on(keys,tune_e,32768,2026091310);
    const std::uint64_t tune_want=[&]{std::uint64_t s=0;for(const auto&v:tq)s+=checksum({v.hit,v.expected});return s;}();
    const std::vector<std::uint32_t> THRESHOLDS{4,8,16,32,64,128,256};
    std::vector<std::pair<std::string,Mode>> variants;
    variants.push_back({"ov_linear",Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Linear,32}});
    variants.push_back({"ov_binary",Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Binary,32}});
    for(auto t:THRESHOLDS)variants.push_back({"ov_hybrid_"+std::to_string(t),
        Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Hybrid,t}});
    std::ofstream tune_out(dir+"/tune_"+kind+".csv");
    tune_out.exceptions(std::ios::badbit|std::ios::failbit);tune_out<<std::setprecision(12);
    tune_out<<"variant,round,ns_per_op\n";
    std::vector<std::pair<std::string,double>> tune_med;   // variant, median ns
    for(const auto&[name,mode]:variants){
        std::vector<double> times;
        for(int r=0;r<3;++r){
            std::uint64_t warm=0;for(std::size_t i=0;i<std::min<std::size_t>(20000,tq.size());++i)warm+=checksum(idx.find(tq[i].key,mode));
            sink=warm;
            std::uint64_t sum=0;const auto t0=Clock::now();for(const auto&v:tq)sum+=checksum(idx.find(v.key,mode));const auto t1=Clock::now();
            if(sum!=tune_want)throw std::runtime_error("tuning checksum "+name);
            times.push_back(ns(t0,t1)/tq.size());
            tune_out<<name<<','<<r<<','<<times.back()<<'\n';
        }
        std::sort(times.begin(),times.end());
        tune_med.push_back({name,times[times.size()/2]});
        std::cerr<<"tuned "<<name<<" median="<<times[times.size()/2]<<"\n";
    }
    tune_out.close();
    // pick best hybrid threshold (context: linear/binary medians reported too)
    std::uint32_t best_t=32;double best_ns=1e300;
    double lin_ns=0,bin_ns=0;
    for(const auto&[name,nsv]:tune_med){
        if(name=="ov_linear")lin_ns=nsv;
        if(name=="ov_binary")bin_ns=nsv;
        if(name.rfind("ov_hybrid_",0)==0&&nsv<best_ns){
            best_ns=nsv;best_t=static_cast<std::uint32_t>(std::stoul(name.substr(10)));}
    }
    {std::ofstream fz(dir+"/frozen_ov_"+kind+".json");
     fz<<"{\"kind\":\""<<kind<<"\",\"n\":"<<keys.size()
       <<",\"frozen_hybrid_threshold\":"<<best_t
       <<",\"tuning_median_ns\":{\"linear\":"<<lin_ns<<",\"binary\":"<<bin_ns
       <<",\"hybrid\":"<<best_ns<<"}}\n";}
    std::cerr<<"FROZEN hybrid threshold="<<best_t<<"\n";
    // ---------- test phase: frozen policy set only ----------
    const Mode m_lin{Route::Interval,Prefetch::Off,true,true,OvSearch::Linear,32};
    const Mode m_bin{Route::Interval,Prefetch::Off,true,true,OvSearch::Binary,32};
    const Mode m_hyb{Route::Interval,Prefetch::Off,true,true,OvSearch::Hybrid,best_t};
    struct Method {std::string name;Mode mode;};
    const std::vector<Method> M{{"ov_linear",m_lin},{"ov_binary",m_bin},{"ov_hybrid",m_hyb}};
    std::ofstream out(dir+"/ov_"+kind+".csv");
    out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);
    out<<"kind,n,method,seed,round,ns_per_op,checksum\n";
    std::ofstream diag(dir+"/ovdiag_"+kind+".csv");
    diag.exceptions(std::ios::badbit|std::ios::failbit);diag<<std::setprecision(12);
    diag<<"kind,method,primary_hits,overflow_hits,misses,mean_str_cmps,mean_overflow_checks,mean_steps,sampled\n";
    const std::size_t nq=131072;
    for(int seed=0;seed<3;++seed){
        auto q=make_q_on(keys,test_e,nq,2026091320+static_cast<std::uint64_t>(seed));
        const std::uint64_t want=[&]{std::uint64_t s=0;for(const auto&v:q)s+=checksum({v.hit,v.expected});return s;}();
        for(const auto&v:q){const auto r=idx.find(v.key,m_hyb);
            if(checksum(r)!=checksum({v.hit,v.expected}))throw std::runtime_error("verify mismatch");}
        std::vector<std::size_t> order(M.size());std::iota(order.begin(),order.end(),0);
        std::mt19937_64 rng(2026091377+seed);
        for(int r=0;r<5;++r){
            std::shuffle(order.begin(),order.end(),rng);
            for(auto which:order){
                const auto&m=M[which];
                std::uint64_t warm=0;for(std::size_t i=0;i<std::min<std::size_t>(20000,q.size());++i)warm+=checksum(idx.find(q[i].key,m.mode));
                sink=warm;
                std::uint64_t sum=0;const auto t0=Clock::now();for(const auto&v:q)sum+=checksum(idx.find(v.key,m.mode));const auto t1=Clock::now();
                if(sum!=want)throw std::runtime_error("timed checksum "+m.name);
                out<<kind<<','<<keys.size()<<','<<m.name<<','<<seed<<','<<r<<','<<ns(t0,t1)/q.size()<<','<<sum<<'\n';
            }
            out.flush();
        }
        // class statistics on a fresh sample of test queries (outside timing)
        const auto sq=make_q_on(keys,test_e,4096,2026091390+static_cast<std::uint64_t>(seed));
        for(const auto&m:M){
            std::size_t ph=0,oh=0,mi=0;double sc=0,oc=0,st=0;
            for(const auto&v:sq){const auto r=idx.find(v.key,m.mode);
                if(r.source==1)++ph;else if(r.source==2)++oh;else ++mi;
                sc+=r.str_cmps;oc+=r.overflow_checks;st+=r.steps;}
            diag<<kind<<','<<m.name<<','<<ph<<','<<oh<<','<<mi<<','
                <<sc/sq.size()<<','<<oc/sq.size()<<','<<st/sq.size()<<','<<sq.size()<<'\n';
        }
        diag.flush();
        std::cerr<<"seed "<<seed<<" done\n";
    }
    std::cerr<<"COMPLETE "<<kind<<"\n";
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<"\n";return 1;}}

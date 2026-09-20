// E5 benchmark: (1) slot-count x block-size sweep with SIMD A/B on identical indexes,
// (2) cost decomposition (locate-only / region-local / end-to-end) at the default layout.
// Discipline inherited from rounds 1-4: shuffled method order, warm-up, batch checksums,
// independent query copies, ASCII ~missing~ negatives verified absent before timing.
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
static std::uint64_t expected_sum(const std::vector<Q>&q){std::uint64_t s=0;for(const auto&v:q)s+=checksum({v.hit,v.expected});return s;}

struct LayoutRec {
    std::uint32_t s,b;std::unique_ptr<Index> idx;double build_ms=0;
    std::size_t bytes=0,overflow=0,groups=0;
    std::size_t blocked_slots=0,blocked_bytes=0,blocked_both=0,fits=0,oversized=0;
};

int main(int argc,char**argv){try{
#ifdef _WIN32
    // Replaces Linux `taskset -c 0` from the original rounds.
    SetProcessAffinityMask(GetCurrentProcess(),1);
    SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
#endif
    if(argc<4)throw std::invalid_argument("bench_e5 KIND N OUTDIR [TRANCOCSV] [QUERIES] [SEEDS] [ROUNDS]");
    const std::string kind=argv[1];const std::size_t n=std::stoull(argv[2]);const std::string dir=argv[3];
    const std::string csv=argc>4?argv[4]:"";
    const std::size_t nq=argc>5?std::stoull(argv[5]):131072;
    const int seeds=argc>6?std::stoi(argv[6]):3,rounds=argc>7?std::stoi(argv[7]):5;
    if(n<4096||nq<1000||seeds<1||rounds<1)throw std::invalid_argument("size");

    const auto keys=make_data(kind,n,csv);
    std::cerr<<"loaded "<<keys.size()<<" keys kind="<<kind<<"\n";

    // ---- build all layouts (context16 fixed: the sweep question is S x B, not the model) ----
    const std::vector<std::pair<std::uint32_t,std::uint32_t>> LAYOUTS{{8,512},{8,1024},{8,2048},{16,512},{16,1024},{16,2048},{32,1024},{32,2048}};
    std::vector<LayoutRec> L;L.reserve(LAYOUTS.size());
    for(const auto&[sl,bb]:LAYOUTS){
        Config c;c.block_bytes=bb;c.slots=sl;c.model_kind=ModelKind::Context16;c.region_keys=2048;
        LayoutRec r;r.s=sl;r.b=bb;
        const auto t0=Clock::now();r.idx=std::make_unique<Index>(c);r.idx->bulk_load(keys);r.build_ms=ns(t0,Clock::now())/1e6;
        r.bytes=r.idx->memory();r.overflow=r.idx->overflow_count();
        for(std::size_t i=0;i<r.idx->region_count();++i)for(const auto&g:r.idx->region(i).group_stats()){
            r.groups++;r.blocked_slots+=g.blocked_slots_only;r.blocked_bytes+=g.blocked_bytes_only;
            r.blocked_both+=g.blocked_both;r.fits+=g.fits_now;r.oversized+=g.oversized;}
        std::cerr<<"built s="<<sl<<" b="<<bb<<" build_ms="<<r.build_ms<<" bytes="<<r.bytes<<" overflow="<<r.overflow<<"\n";
        L.push_back(std::move(r));
    }
    const std::size_t regions=(keys.size()+2047)/2048;
    SelectedIndex leaf;double leaf_ms;
    {const auto t0=Clock::now();leaf.bulk_load(keys,std::vector<int>(regions,SORTED_LEAF),2048);leaf_ms=ns(t0,Clock::now())/1e6;}
    std::size_t sorted_bytes=keys.capacity()*sizeof(Pair);for(const auto&kv:keys)sorted_bytes+=kv.first.capacity()+1;

    struct Method {std::string name;std::uint32_t s=0,b=0;int simd=-1;std::function<Result(const std::string&)> fn;};
    std::vector<Method> M;
    for(const auto&r:L){
        const Index*p=r.idx.get();
        M.push_back({"s"+std::to_string(r.s)+"b"+std::to_string(r.b)+"_simd",r.s,r.b,1,[p](const std::string&k){return p->find(k,Mode{Route::Interval,Prefetch::Off,true,true});}});
        M.push_back({"s"+std::to_string(r.s)+"b"+std::to_string(r.b)+"_scalar",r.s,r.b,0,[p](const std::string&k){return p->find(k,Mode{Route::Interval,Prefetch::Off,true,false});}});
    }
    M.push_back({"sorted_leaf",0,0,-1,[&](const std::string&k){return leaf.find(k);}});
    M.push_back({"sorted_array",0,0,-1,[&](const std::string&k){
        auto it=std::lower_bound(keys.begin(),keys.end(),k,[](const Pair&a,const std::string&b){return compare(a.first,b)<0;});
        return it!=keys.end()&&it->first==k?Result{true,it->second}:Result{};}});

    // mean model steps / overflow checks sampled once per layout method (seed 0 queries).
    std::vector<double> mean_steps(M.size(),0),mean_ovc(M.size(),0);
    {
        const auto sq=make_q(keys,4096,2026091300);
        for(std::size_t m=0;m<M.size();++m){double st=0,ov=0;
            for(const auto&q:sq){const auto r=M[m].fn(q.key);st+=r.steps;ov+=r.overflow_checks;
                if(r.found!=q.hit||(r.found&&r.value!=q.expected))throw std::runtime_error("sample verify "+M[m].name);}
            mean_steps[m]=st/sq.size();mean_ovc[m]=ov/sq.size();}
    }

    std::ofstream out(dir+"/sweep_"+kind+".csv");
    out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);
    out<<"kind,n,method,S,B,simd,seed,round,ns_per_op,bytes,overflow_keys,mean_steps,mean_overflow_checks,blocked_slots_only,blocked_bytes_only,blocked_both,fits_now,oversized,checksum\n";
    std::ofstream dout(dir+"/decomp_"+kind+".csv");
    dout.exceptions(std::ios::badbit|std::ios::failbit);dout<<std::setprecision(12);
    dout<<"kind,n,method,seed,round,ns_per_op,checksum\n";

    const Index*pdef=L[4].idx.get(); // (16,1024) reference layout for decomposition
    for(int seed=0;seed<seeds;++seed){
        auto q=make_q(keys,nq,2026091310+static_cast<std::uint64_t>(seed));
        const auto want=expected_sum(q);
        for(auto&v:q)v.region=pdef->locate(v.key);
        {const Index*p=pdef;
         // decomposition methods for this seed (locate-only / region-local / end-to-end)
         std::vector<std::pair<std::string,std::function<void(const Q&)>>> D{
             {"locate_only",[&](const Q&v){sink=static_cast<std::uint64_t>(p->locate(v.key));}},
             {"local_simd",[&](const Q&v){auto r=p->find_known(v.region,v.key,Mode{Route::Interval,Prefetch::Off,true,true});if(checksum(r)!=checksum({v.hit,v.expected}))throw std::runtime_error("local mismatch");}},
             {"local_scalar",[&](const Q&v){auto r=p->find_known(v.region,v.key,Mode{Route::Interval,Prefetch::Off,true,false});if(checksum(r)!=checksum({v.hit,v.expected}))throw std::runtime_error("local mismatch");}},
             {"end_simd",[&](const Q&v){auto r=p->find(v.key,Mode{Route::Interval,Prefetch::Off,true,true});if(checksum(r)!=checksum({v.hit,v.expected}))throw std::runtime_error("end mismatch");}}};
         std::vector<std::size_t> order(D.size());std::iota(order.begin(),order.end(),0);
         std::mt19937_64 rng(2026091399+seed);
         for(int r=0;r<rounds;++r){std::shuffle(order.begin(),order.end(),rng);
             for(auto which:order){const auto t0=Clock::now();for(const auto&v:q)D[which].second(v);const auto t1=Clock::now();
                 dout<<kind<<','<<keys.size()<<','<<D[which].first<<','<<seed<<','<<r<<','<<ns(t0,t1)/q.size()<<','<<want<<'\n';}
             dout.flush();}
        }
        std::vector<std::size_t> order(M.size());std::iota(order.begin(),order.end(),0);
        std::mt19937_64 rng(2026091377+seed);
        for(int r=0;r<rounds;++r){
            std::shuffle(order.begin(),order.end(),rng);
            for(auto which:order){
                const auto&m=M[which];
                std::uint64_t warm=0;const auto nw=std::min<std::size_t>(20000,q.size());
                for(std::size_t i=0;i<nw;++i)warm+=checksum(m.fn(q[i].key));
                sink=warm;
                std::uint64_t sum=0;const auto t0=Clock::now();for(const auto&v:q)sum+=checksum(m.fn(v.key));const auto t1=Clock::now();
                if(sum!=want)throw std::runtime_error("timed checksum "+m.name);
                const bool lay=m.s!=0;const LayoutRec*rec=nullptr;
                for(const auto&x:L)if(x.s==m.s&&x.b==m.b)rec=&x;
                double st=lay?mean_steps[which]:0,ov=lay?mean_ovc[which]:0;
                std::size_t bytes=lay?rec->bytes:(m.name=="sorted_leaf"?leaf.memory():sorted_bytes);
                std::size_t ovk=lay?rec->overflow:(m.name=="sorted_leaf"?leaf.overflow_count():0);
                out<<kind<<','<<keys.size()<<','<<m.name<<','<<m.s<<','<<m.b<<','<<m.simd<<','<<seed<<','<<r<<','
                   <<ns(t0,t1)/q.size()<<','<<bytes<<','<<ovk<<','<<st<<','<<ov<<','
                   <<(rec?rec->blocked_slots:0)<<','<<(rec?rec->blocked_bytes:0)<<','<<(rec?rec->blocked_both:0)<<','
                   <<(rec?rec->fits:0)<<','<<(rec?rec->oversized:0)<<','<<sum<<'\n';
            }
            out.flush();
        }
        std::cerr<<"seed "<<seed<<" done\n";
    }
    std::cerr<<"COMPLETE "<<kind<<"\n";
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<"\n";return 1;}}

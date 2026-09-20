#include "index.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
using namespace validation;
using Clock=std::chrono::steady_clock;
static volatile std::uint64_t sink=0;
static double elapsed(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::nano>(b-a).count();}
static std::string hex(std::uint64_t x){char buf[17];static const char*d="0123456789abcdef";for(int i=15;i>=0;--i){buf[i]=d[x&15];x>>=4;}return {buf,16};}
static std::vector<Pair> data(const std::string&kind,std::size_t n,const std::string&file){
    std::vector<Pair> out;out.reserve(n);std::mt19937_64 rng(20260906);
    if(kind=="tranco"){
        std::ifstream in(file);if(!in)throw std::runtime_error("cannot open Tranco CSV");std::string line;
        while(out.size()<n&&std::getline(in,line)){auto p=line.find(',');if(p==std::string::npos)continue;auto s=line.substr(p+1);if(!s.empty()&&s.back()=='\r')s.pop_back();out.emplace_back(std::move(s),out.size()+1);}
    }else for(std::size_t i=0;i<n;++i){
        std::string s;
        if(kind=="short")s="user/"+hex(rng())+hex(i);
        else if(kind=="long"){
            s="https://example.test/catalog/"+hex(rng())+hex(i);
            while(s.size()<156)s.push_back(static_cast<char>('a'+rng()%26));
        }else if(kind=="skew"){
            s="https://example.test/shared/"+std::string(1+(rng()%96),'a')+"b"+hex(i);
            while(s.size()<156)s.push_back(static_cast<char>('a'+rng()%26));
        }else throw std::invalid_argument("unknown dataset");
        out.emplace_back(std::move(s),i+1);
    }
    std::sort(out.begin(),out.end(),pair_less);
    out.erase(std::unique(out.begin(),out.end(),[](const Pair&a,const Pair&b){return a.first==b.first;}),out.end());return out;
}
struct Query {std::string owned;std::string_view key;std::size_t region=0;std::uint64_t expected=0;bool hit=false;};
struct Variant {const char*name;Mode mode;bool residual=false;bool sorted=false;};
static std::uint64_t checksum(const Result&r){return r.found?r.value+0x9e3779b97f4a7c15ULL:0;}
int main(int argc,char**argv){try{
    if(argc<8)throw std::invalid_argument("bench DATASET N BLOCK QUERIES REPS REGION_KEYS SCOPE [CSV] [MISS_PERCENT]");
    const std::string kind=argv[1];const auto requested=std::stoull(argv[2]);Config cfg;cfg.block_bytes=std::stoul(argv[3]);
    const auto nq=std::stoull(argv[4]);const int reps=std::stoi(argv[5]);cfg.region_keys=std::stoull(argv[6]);const std::string scope=argv[7];
    const std::string file=argc>8?argv[8]:"";const unsigned miss=argc>9?std::stoul(argv[9]):10;
    auto keys=data(kind,requested,file);if(keys.empty())throw std::runtime_error("no keys");
    const auto build0=Clock::now();Index index(cfg);index.bulk_load(keys);const auto build1=Clock::now();
    cfg.canonical=Route::Residual;Index residual(cfg);residual.bulk_load(keys);const auto build2=Clock::now();
    std::cerr<<"build "<<kind<<" n="<<keys.size()<<" B="<<cfg.block_bytes<<" full_ms="<<elapsed(build0,build1)/1e6<<" residual_ms="<<elapsed(build1,build2)/1e6<<" regions="<<index.region_count()<<" bytes="<<index.memory()<<" overflow="<<index.overflow_count()<<"\n";
    std::mt19937_64 rng(981273);std::vector<Query> queries(nq);std::uint64_t expected_sum=0;double mean_key_bytes=0;
    for(auto&q:queries){const auto &kv=keys[rng()%keys.size()];q.hit=rng()%100>=miss;
        if(q.hit){q.key=kv.first;q.expected=kv.second;expected_sum+=kv.second+0x9e3779b97f4a7c15ULL;}
        else {q.owned=kv.first;q.owned.push_back('\0');q.key=q.owned;}
        q.region=index.locate(q.key);mean_key_bytes+=q.key.size();
    }
    mean_key_bytes/=nq;
    std::vector<Variant> variants={
        {"full",{Route::Full,Prefetch::Off,true}},
        {"full_pf",{Route::Full,Prefetch::Early,true}},
        {"interval",{Route::Interval,Prefetch::Off,true}},
        {"interval_early_pf",{Route::Interval,Prefetch::Early,true}},
        {"interval_late_pf",{Route::Interval,Prefetch::Late,true}},
        {"interval_direct",{Route::Interval,Prefetch::Off,false}},
        {"residual",{Route::Residual,Prefetch::Off,true},true},
        {"residual_pf",{Route::Residual,Prefetch::Early,true},true}
    };
    if(scope=="end")variants.push_back({"sorted_array",{Route::Full,Prefetch::Off,false},false,true});
    auto lookup=[&](const Variant&v,const Query&q){
        if(v.sorted){auto it=std::lower_bound(keys.begin(),keys.end(),q.key,[](const Pair&a,std::string_view b){return compare(a.first,b)<0;});Result r;if(it!=keys.end()&&compare(it->first,q.key)==0){r.found=true;r.value=it->second;}return r;}
        const Index&owner=v.residual?residual:index;
        return scope=="local"?owner.find_known(q.region,q.key,v.mode):owner.find(q.key,v.mode);
    };
    std::size_t sorted_bytes=keys.capacity()*sizeof(Pair);for(const auto&kv:keys)sorted_bytes+=kv.first.capacity()+1;
    std::cout<<"dataset,n,block,region_keys,scope,miss_percent,queries,repeat,mode,ns_per_op,mops,index_bytes,overflow_keys,mean_model_bytes,mean_overflow_checks,query_key_bytes,p50_sample_ns,p99_sample_ns,checksum\n"<<std::setprecision(12);
    std::vector<std::size_t> order(variants.size());std::iota(order.begin(),order.end(),0);
    for(int rep=0;rep<reps;++rep){
        std::shuffle(order.begin(),order.end(),rng);
        for(auto which:order){const auto&v=variants[which];std::uint64_t warm=0;
            const auto nw=std::min<std::size_t>(20000,queries.size());
            for(std::size_t i=0;i<nw;++i){auto r=lookup(v,queries[i]);if(r.found!=queries[i].hit||(r.found&&r.value!=queries[i].expected))throw std::runtime_error(std::string("warm validation: ")+v.name);warm+=checksum(r);}sink=warm;
            std::uint64_t sum=0;const auto t0=Clock::now();for(const auto&q:queries)sum+=checksum(lookup(v,q));const auto t1=Clock::now();sink=sum;
            if(sum!=expected_sum)throw std::runtime_error(std::string("timed checksum: ")+v.name);
            const double ns=elapsed(t0,t1)/queries.size();
            double steps=0,ov=0,p50=0,p99=0;const auto sample=std::min<std::size_t>(4096,queries.size());
            for(std::size_t i=0;i<sample;++i){auto r=lookup(v,queries[i]);steps+=r.steps;ov+=r.overflow_checks;}
            if(rep==0){std::vector<double> lat;lat.reserve(sample);for(std::size_t i=0;i<sample;++i){auto a=Clock::now();auto r=lookup(v,queries[i]);auto b=Clock::now();sink=checksum(r);lat.push_back(elapsed(a,b));}std::sort(lat.begin(),lat.end());p50=lat[lat.size()/2];p99=lat[(lat.size()-1)*99/100];}
            const auto &owner=v.residual?residual:index;
            std::cout<<kind<<','<<keys.size()<<','<<cfg.block_bytes<<','<<cfg.region_keys<<','<<scope<<','<<miss<<','<<queries.size()<<','<<rep<<','<<v.name<<','<<ns<<','<<1000/ns<<','<<(v.sorted?sorted_bytes:owner.memory())<<','<<(v.sorted?0:owner.overflow_count())<<','<<steps/sample<<','<<ov/sample<<','<<mean_key_bytes<<','<<p50<<','<<p99<<','<<sum<<'\n';
        }
        std::cout.flush();
    }
}catch(const std::exception&e){std::cerr<<"FAILED: "<<e.what()<<std::endl;return 1;}}

#pragma once
#include "../validation_e3_20260906/data.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <set>

namespace e4 {
using namespace validation;
using Clock=std::chrono::steady_clock;
inline volatile std::uint64_t sink=0;
inline double ns(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::nano>(b-a).count();}
inline double median(std::vector<double> a){if(a.empty())throw std::runtime_error("empty median");std::sort(a.begin(),a.end());return a[a.size()/2];}
inline std::uint64_t checksum(Result r){return r.found?r.value+0x9e3779b97f4a7c15ULL:0;}
inline void verify(Result r,const Query&q){if(r.found!=q.hit||(r.found&&r.value!=q.expected))throw std::runtime_error("query mismatch: "+q.key);}
inline std::uint64_t expected(const std::vector<Query>&q){std::uint64_t s=0;for(const auto&v:q)s+=checksum({v.hit,v.expected});return s;}
inline int partition(std::size_t rank){const auto r=rank%8;return r<2?0:r<4?1:2;}
inline std::vector<Query> queries(const std::vector<Pair>&keys,std::size_t count,std::uint64_t seed,int split,std::size_t begin=0,std::size_t end=SIZE_MAX){
    std::vector<std::size_t> eligible;end=std::min(end,keys.size());
    for(std::size_t i=begin;i<end;++i)if(partition(i)==split)eligible.push_back(i);
    if(eligible.empty())throw std::runtime_error("empty split");
    std::mt19937_64 rng(seed);std::vector<Query> out;out.reserve(count);
    for(std::size_t i=0;i<count;++i){const auto&kv=keys[eligible[rng()%eligible.size()]];Query q{kv.first,0,i%2==0};
        if(q.hit)q.expected=kv.second;else{q.key+="~missing~";auto it=std::lower_bound(keys.begin(),keys.end(),q.key,[](const Pair&a,const std::string&b){return compare(a.first,b)<0;});if(it!=keys.end()&&it->first==q.key)throw std::runtime_error("negative query exists");}
        out.push_back(std::move(q));
    }
    std::shuffle(out.begin(),out.end(),rng);return out;
}
using Lookup=std::function<Result(const std::string&)>;
inline double batch(const Lookup&find,const std::vector<Query>&q,int passes=1){
    std::uint64_t sum=0;const auto a=Clock::now();for(int p=0;p<passes;++p)for(const auto&v:q)sum+=checksum(find(v.key));const auto b=Clock::now();sink=sum;
    if(sum!=expected(q)*passes)throw std::runtime_error("batch checksum mismatch");return ns(a,b)/(q.size()*passes);
}
struct ProxyMatrices {Matrix warm,cross;std::vector<double> weights;double clock_ns=0,setup_ms=0;};
inline ProxyMatrices measure(const std::vector<Pair>&keys,std::ostream&out,std::size_t region=2048){
    const auto start=Clock::now();const auto nr=(keys.size()+region-1)/region;
    ProxyMatrices m{Matrix(nr),Matrix(nr),std::vector<double>(nr)};
    std::vector<Query> tune;for(std::size_t i=0;i<nr;++i){auto q=queries(keys,128,2026091100+i,0,i*region,std::min(keys.size(),(i+1)*region));for(auto&v:q)tune.push_back(std::move(v));}
    std::vector<std::vector<std::size_t>> local(nr);std::vector<std::size_t> owners(tune.size());
    {SelectedIndex route;route.bulk_load(keys,std::vector<int>(nr,9),region);for(std::size_t j=0;j<tune.size();++j){owners[j]=route.locate(tune[j].key);local[owners[j]].push_back(j);}}
    for(std::size_t i=0;i<nr;++i){if(local[i].empty())throw std::runtime_error("empty routed region");m.weights[i]=double(local[i].size())/tune.size();}
    std::vector<double> clocks;for(int i=0;i<10000;++i){auto a=Clock::now();auto b=Clock::now();clocks.push_back(ns(a,b));}m.clock_ns=median(clocks);
    std::vector<int> candidates(10);std::iota(candidates.begin(),candidates.end(),0);std::mt19937_64 rng(2026091111);std::shuffle(candidates.begin(),candidates.end(),rng);
    out<<"candidate,region,bytes,overflow,warm_ns,cross_ns,routed_queries\n";
    for(int c:candidates){SelectedIndex idx;idx.bulk_load(keys,std::vector<int>(nr,c),region);
        for(const auto&q:tune)verify(idx.find(q.key),q);
        for(std::size_t i=0;i<nr;++i){const auto&leaf=idx.leaf(i);std::vector<Query> q;for(auto j:local[i])q.push_back(tune[j]);std::vector<double> times;
            for(int r=0;r<3;++r)times.push_back(batch([&](const std::string&k){return leaf.find(k);},q,4));
            m.warm[i][c]={leaf.memory(),leaf.overflow_count(),median(times),0};
            m.cross[i][c]=m.warm[i][c];
        }
        std::vector<std::vector<double>> times(nr);std::vector<std::size_t> order(tune.size());std::iota(order.begin(),order.end(),0);
        for(int r=0;r<3;++r){std::shuffle(order.begin(),order.end(),rng);std::vector<double> sum(nr);std::uint64_t check=0;
            for(auto j:order){const auto&q=tune[j];const auto a=Clock::now();auto result=idx.find(q.key);const auto b=Clock::now();check+=checksum(result);sum[owners[j]]+=ns(a,b);}
            sink=check;if(check!=expected(tune))throw std::runtime_error("cross checksum mismatch");
            for(std::size_t i=0;i<nr;++i)times[i].push_back(sum[i]/local[i].size());
        }
        for(std::size_t i=0;i<nr;++i){m.cross[i][c].tune_ns=median(times[i]);out<<c<<','<<i<<','<<m.warm[i][c].bytes<<','<<m.warm[i][c].overflow<<','<<m.warm[i][c].tune_ns<<','<<m.cross[i][c].tune_ns<<','<<local[i].size()<<'\n';}
        out.flush();std::cerr<<"MEASURED candidate "<<c<<std::endl;
    }
    m.setup_ms=ns(start,Clock::now())/1e6;return m;
}
inline std::vector<int> uniform(const Matrix&m,const std::vector<double>&w,std::size_t budget){
    std::vector<int> best(m.size(),REFERENCE);for(int c=0;c<9;++c){std::vector<int> v(m.size(),c);if(selected_bytes(m,v)<=budget&&selected_cost(m,w,v)<selected_cost(m,w,best))best=v;}return best;
}
inline std::vector<int> joint(const Matrix&m,const std::vector<double>&w,std::size_t budget,bool fallback){
    Allowed allowed(m.size());for(auto&v:allowed)for(int c=0;c<(fallback?10:9);++c)v.push_back(c);
    return select(m,w,allowed,budget,uniform(m,w,budget));
}
struct Policy {std::string name;std::vector<int> ids;std::unique_ptr<SelectedIndex> index;double build_ms=0,validation_ns=0;};
class Registry {
public:
    std::vector<Policy> policies;std::map<std::string,std::size_t> aliases;
    std::size_t add(const std::string&name,const std::vector<int>&ids){
        for(std::size_t i=0;i<policies.size();++i)if(policies[i].ids==ids){aliases[name]=i;return i;}
        aliases[name]=policies.size();policies.push_back({name,ids,{}});return policies.size()-1;
    }
    void build(const std::vector<Pair>&keys){for(auto&p:policies){auto a=Clock::now();p.index=std::make_unique<SelectedIndex>();p.index->bulk_load(keys,p.ids);p.build_ms=ns(a,Clock::now())/1e6;}}
    // Validation samples are supplied explicitly; no test data is accepted here.
    std::size_t validate(const std::vector<Query>&validation,std::ostream&out,std::size_t budget){
        std::vector<std::vector<double>> samples(policies.size());std::vector<std::size_t> order(policies.size());std::iota(order.begin(),order.end(),0);std::mt19937_64 rng(2026091131);
        for(auto&p:policies)for(const auto&q:validation)verify(p.index->find(q.key),q);
        out<<"method,repeat,ns_per_op,eligible\n";
        for(int r=0;r<5;++r){std::shuffle(order.begin(),order.end(),rng);for(auto i:order){auto&p=policies[i];auto t=batch([&](const std::string&k){return p.index->find(k);},validation);samples[i].push_back(t);out<<p.name<<','<<r<<','<<t<<','<<(p.index->payload_memory()<=budget)<<'\n';}out.flush();}
        std::size_t best=aliases.at("default");for(std::size_t i=0;i<policies.size();++i){policies[i].validation_ns=median(samples[i]);if(policies[i].index->payload_memory()<=budget&&policies[i].validation_ns<policies[best].validation_ns)best=i;}
        aliases["validated_policy"]=best;std::size_t u=aliases.at("default");for(int c=0;c<9;++c){auto it=aliases.find("uniform_"+std::to_string(c));if(it!=aliases.end()&&policies[it->second].validation_ns<policies[u].validation_ns)u=it->second;}
        aliases["validated_uniform"]=u;return best;
    }
};
}

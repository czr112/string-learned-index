#include "selector.hpp"
#include "native.hpp"
#include <filesystem>
#include <iomanip>
using namespace e4;
static std::ofstream file(const std::filesystem::path&p){std::ofstream f;f.exceptions(std::ios::badbit|std::ios::failbit);f.open(p);f<<std::setprecision(12);return f;}
int main(int argc,char**argv){try{
    if(argc<5)throw std::invalid_argument("bench KIND N OUTPUT_DIR TRANC0_CSV [QUERIES] [ROUNDS] [SEEDS]");
    const std::string kind=argv[1];const std::size_t n=std::stoull(argv[2]),nq=argc>5?std::stoull(argv[5]):262144;const int rounds=argc>6?std::stoi(argv[6]):5,seeds=argc>7?std::stoi(argv[7]):3;
    if(n<2048||nq<2||rounds<1||seeds<1)throw std::invalid_argument("invalid benchmark size");
    const std::filesystem::path dir=argv[3];std::filesystem::create_directories(dir);
    const auto keys=make_data(kind,n,argv[4]);for(const auto&kv:keys)ascii_key(kv.first);
    const auto nr=(n+2047)/2048;auto candidate_file=file(dir/"candidates.csv");auto m=measure(keys,candidate_file);candidate_file.close();
    const std::vector<int> ref(nr,REFERENCE);const auto budget=selected_bytes(m.warm,ref);
    Registry reg;reg.add("default",ref);
    for(int c=0;c<9;++c){std::vector<int> v(nr,c);if(selected_bytes(m.warm,v)<=budget)reg.add("uniform_"+std::to_string(c),v);}
    reg.add("warm_uniform",uniform(m.warm,m.weights,budget));reg.add("cross_uniform",uniform(m.cross,m.weights,budget));
    reg.add("warm_joint",joint(m.warm,m.weights,budget,false));reg.add("cross_joint",joint(m.cross,m.weights,budget,false));
    reg.add("warm_hybrid",joint(m.warm,m.weights,budget,true));reg.add("cross_hybrid",joint(m.cross,m.weights,budget,true));
    reg.add("sorted_leaves",std::vector<int>(nr,9));reg.build(keys);
    for(auto&p:reg.policies)if(p.index->payload_memory()!=selected_bytes(m.warm,p.ids))throw std::runtime_error("selection allocation changed");
    const auto va=Clock::now();const auto validation=queries(keys,std::min<std::size_t>(131072,nq),2026091121,1);auto vf=file(dir/"validation.csv");auto winner=reg.validate(validation,vf,budget);vf.close();
    const auto validation_ms=ns(va,Clock::now())/1e6;
    // Freeze all aliases and choices before creating or inspecting test queries.
    auto frozen=file(dir/"frozen.json");frozen<<"{\"dataset\":\""<<kind<<"\",\"n\":"<<n<<",\"budget_leaf_bytes\":"<<budget<<",\"clock_ns\":"<<m.clock_ns<<",\"proxy_setup_ms\":"<<m.setup_ms<<",\"validation_ms\":"<<validation_ms<<",\"validation_winner\":\""<<reg.policies[winner].name<<"\",\"aliases\":{";
    bool comma=false;for(const auto&[a,i]:reg.aliases){if(comma)frozen<<',';comma=true;frozen<<'"'<<a<<"\":\""<<reg.policies[i].name<<'"';}frozen<<"},\"policies\":[";
    for(std::size_t i=0;i<reg.policies.size();++i){auto&p=reg.policies[i];if(i)frozen<<',';frozen<<"{\"name\":\""<<p.name<<"\",\"bytes\":"<<p.index->memory()<<",\"leaf_bytes\":"<<p.index->payload_memory()<<",\"validation_ns\":"<<p.validation_ns<<",\"build_ms\":"<<p.build_ms<<",\"choices\":[";for(std::size_t j=0;j<nr;++j){if(j)frozen<<',';frozen<<p.ids[j];}frozen<<"]}";}frozen<<"]}\n";frozen.close();
    std::cerr<<"FROZEN winner="<<reg.policies[winner].name<<std::endl;
    std::set<std::size_t> keep;for(auto a:{"default","warm_uniform","cross_uniform","warm_joint","cross_joint","warm_hybrid","cross_hybrid","sorted_leaves","validated_uniform","validated_policy"})keep.insert(reg.aliases.at(a));
    for(std::size_t i=0;i<reg.policies.size();++i)if(!keep.count(i))reg.policies[i].index.reset();
    auto start=Clock::now();NativeArt art(keys);auto art_ms=ns(start,Clock::now())/1e6;
    struct Method{std::string name;Lookup find;std::size_t bytes;double build_ms;};std::vector<Method> methods;
    for(auto i:keep){auto*p=reg.policies[i].index.get();methods.push_back({reg.policies[i].name,[p](const std::string&k){return p->find(k);},p->memory(),reg.policies[i].build_ms});}
    std::size_t sorted_bytes=keys.capacity()*sizeof(Pair);for(const auto&kv:keys)sorted_bytes+=kv.first.capacity()+1;
    methods.push_back({"sorted_array",[&](const std::string&k){auto it=std::lower_bound(keys.begin(),keys.end(),k,[](const Pair&a,const std::string&b){return compare(a.first,b)<0;});return it!=keys.end()&&it->first==k?Result{true,it->second}:Result{};},sorted_bytes,0});
    methods.push_back({"ART_libart",[&](const std::string&k){return art.find(k);},0,art_ms});
    auto out=file(dir/"timings.csv");out<<"dataset,n,queries,query_seed,repeat,method,ns_per_op,bytes_estimate,bytes_known,build_ms,checksum\n";
    for(int seed=0;seed<seeds;++seed){const std::uint64_t qseed=2026091200+seed;const auto q=queries(keys,nq,qseed,2);const auto want=expected(q);
        for(auto&method:methods)for(const auto&v:q)verify(method.find(v.key),v);
        std::vector<std::size_t> order(methods.size());std::iota(order.begin(),order.end(),0);std::mt19937_64 rng(qseed+31);
        for(int r=0;r<rounds;++r){std::shuffle(order.begin(),order.end(),rng);for(auto i:order){auto&method=methods[i];std::uint64_t warm=0;for(std::size_t j=0;j<std::min<std::size_t>(20000,q.size());++j)warm+=checksum(method.find(q[j].key));sink=warm;
                auto elapsed=batch(method.find,q);out<<kind<<','<<n<<','<<nq<<','<<qseed<<','<<r<<','<<method.name<<','<<elapsed<<','<<method.bytes<<','<<(method.bytes!=0)<<','<<method.build_ms<<','<<want<<'\n';}out.flush();}
    }
    out.close();auto done=file(dir/"complete.json");done<<"{\"methods\":"<<methods.size()<<",\"rows\":"<<methods.size()*rounds*seeds<<",\"queries\":"<<nq<<",\"rounds\":"<<rounds<<",\"seeds\":"<<seeds<<",\"status\":\"complete\"}\n";done.close();
    std::cerr<<"COMPLETE "<<kind<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<"ERROR "<<e.what()<<std::endl;return 1;}}

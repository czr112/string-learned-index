#include "selector.hpp"
#include <iostream>
#include <sstream>
using namespace e4;
int main(){try{
    auto keys=make_data("mixed",8192,"");std::array<std::set<std::string>,3> identities;std::size_t checks=0;
    for(std::size_t i=0;i<keys.size();++i){auto p=partition(i);identities[p].insert(keys[i].first);identities[p].insert(keys[i].first+"~missing~");}
    for(int a=0;a<3;++a)for(int b=a+1;b<3;++b)for(const auto&k:identities[a])if(identities[b].count(k))throw std::runtime_error("query identity overlap");
    for(int p=0;p<3;++p){auto q=queries(keys,16384,20260911+p,p);std::size_t hits=0;for(const auto&v:q){if(!identities[p].count(v.key))throw std::runtime_error("query outside partition");hits+=v.hit;++checks;}if(hits!=q.size()/2)throw std::runtime_error("wrong hit ratio");}
    Registry reg;std::vector<int> ids(4,1);auto a=reg.add("default",ids);auto b=reg.add("uniform_1",ids);if(a!=b||reg.policies.size()!=1)throw std::runtime_error("duplicate policy not shared");
    reg.add("uniform_9",std::vector<int>(4,9));reg.build(keys);
    // Infeasible candidates must never be selected, even if they are faster.
    const auto budget=reg.policies[0].index->payload_memory();std::ostringstream log;auto val=queries(keys,4096,2026091109,1);auto w=reg.validate(val,log,budget);
    if(reg.policies[w].index->payload_memory()>budget)throw std::runtime_error("validation budget violation");
    double best=1e100;for(const auto&p:reg.policies)if(p.index->payload_memory()<=budget)best=std::min(best,p.validation_ns);if(reg.policies[w].validation_ns!=best)throw std::runtime_error("wrong validation minimum");
    std::cout<<"PASS partition_queries="<<checks<<" exact_half_hits=1 disjoint_identities=1 deduplicated=1 budget_and_minimum=1\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

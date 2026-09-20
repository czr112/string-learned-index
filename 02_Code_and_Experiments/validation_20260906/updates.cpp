#include "index.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
using namespace validation;
static std::string number(std::uint64_t x){std::string s=std::to_string(x);return std::string(12-s.size(),'0')+s;}
int main(){try{
    Config cfg;cfg.block_bytes=1024;cfg.region_keys=256;
    std::vector<Pair> initial;for(std::uint64_t i=0;i<2048;++i)initial.emplace_back("https://example.test/base/"+number(i),i+1);
    Index index(cfg);index.bulk_load(initial);std::map<std::string,std::uint64_t,Less> reference;
    for(const auto&kv:initial)reference.insert(kv);
    std::vector<std::string> available;for(const auto&kv:initial)available.push_back(kv.first);
    available.reserve(7000);std::mt19937_64 rng(31337);const Mode mode{Route::Interval,Prefetch::Early,true};
    std::cout<<"step,operation,ns,rebuilds,splits,size\n";
    for(std::size_t step=0;step<12000;++step){
        const auto opcode=step%10;std::string key=opcode<3?initial[0].first+"/new/"+number(step):available[rng()%available.size()];
        const auto before_r=index.rebuilds,before_s=index.splits;
        const auto a=std::chrono::steady_clock::now();bool success=false;Result result;std::vector<Pair> scanned;const char*op;
        if(opcode<3){op="insert";success=index.insert(key,step);}
        else if(opcode==3){op="erase";success=index.erase(key);}
        else if(opcode==4){op="update";success=index.update(key,step);}
        else if(opcode==5){op="scan10";scanned=index.scan(key,10);}
        else {op="find";result=index.find(key,mode);}
        const auto b=std::chrono::steady_clock::now();
        if(opcode<3){if(success!=reference.emplace(key,step).second)throw std::runtime_error("insert");available.push_back(key);}
        else if(opcode==3){if(success!=(reference.erase(key)!=0))throw std::runtime_error("erase");}
        else if(opcode==4){auto it=reference.find(key);if(success!=(it!=reference.end()))throw std::runtime_error("update");if(it!=reference.end())it->second=step;}
        else if(opcode==5){std::vector<Pair>want;for(auto it=reference.lower_bound(key);it!=reference.end()&&want.size()<10;++it)want.push_back(*it);if(scanned!=want)throw std::runtime_error("scan");}
        else {auto it=reference.find(key);if(result.found!=(it!=reference.end())||(result.found&&result.value!=it->second))throw std::runtime_error("find");}
        std::cout<<step<<','<<op<<','<<std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count()<<','<<index.rebuilds-before_r<<','<<index.splits-before_s<<','<<index.size()<<'\n';
    }
    const std::vector<Pair> expected(reference.begin(),reference.end());if(index.scan({},index.size()+1)!=expected)throw std::runtime_error("final state");
    std::cerr<<"PASSED operations=12000 rebuilds="<<index.rebuilds<<" splits="<<index.splits<<" final_keys="<<index.size()<<" index_bytes="<<index.memory()<<"\n";
}catch(const std::exception&e){std::cerr<<"FAILED: "<<e.what()<<std::endl;return 1;}}

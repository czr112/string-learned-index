#include "native.hpp"
#include <map>
#include <iostream>
using namespace e4;
template<class Index> void check(const char* name) {
    auto data=make_data("mixed",8192,"");
    for(auto k:{"a","aa","aaa","aab","ab","b","ba","z"})data.emplace_back(k,0);
    std::sort(data.begin(),data.end(),pair_less);
    Index idx(data);std::map<std::string,std::uint64_t> ref(data.begin(),data.end());
    std::size_t checks=0;
    auto verify=[&](const std::string&k){auto r=idx.find(k);auto it=ref.find(k);if(r.found!=(it!=ref.end())||(r.found&&r.value!=it->second))throw std::runtime_error(std::string(name)+" lookup mismatch: "+k);++checks;};
    for(const auto&kv:data){verify(kv.first);verify(kv.first+"~missing~");}
    std::mt19937_64 rng(2026091101);
    for(int t=0;t<10000;++t){std::string k=t%3?data[rng()%data.size()].first:"new/"+hex_key(rng()%1024);auto op=rng()%3;
        if(op==0){auto v=rng()%100000;idx.upsert(k,v);ref[k]=v;}
        else if(op==1){bool a=idx.erase(k),b=ref.erase(k);if(a!=b)throw std::runtime_error(std::string(name)+" erase mismatch");}
        verify(k);
    }
    for(const auto&kv:ref)verify(kv.first);
    std::cout<<name<<" PASS checks="<<checks<<" random_operations=10000 remaining="<<ref.size()<<std::endl;
}
int main(int argc,char**argv){try{
#ifdef E4_ART_ONLY
    check<NativeArt>("libart");
#else
    if(argc<2)throw std::runtime_error("check_native lits|art");if(std::string(argv[1])=="lits")check<NativeLits>("LITS");else check<NativeArt>("libart");
#endif
    return 0;}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}

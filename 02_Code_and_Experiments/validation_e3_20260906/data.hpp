#pragma once
#include "selected.hpp"
#include <fstream>
#include <random>
namespace validation {
inline std::string hex_key(std::uint64_t x){char buf[16];const char*d="0123456789abcdef";for(int i=15;i>=0;--i){buf[i]=d[x&15];x>>=4;}return {buf,16};}
inline std::vector<Pair> make_data(const std::string&kind,std::size_t n,const std::string&file){
    std::vector<Pair> out;out.reserve(n);std::mt19937_64 rng(20260906);
    if(kind=="tranco"){
        std::ifstream in(file);if(!in)throw std::runtime_error("Tranco CSV unavailable");std::string line;
        while(out.size()<n&&std::getline(in,line)){auto p=line.find(',');if(p==std::string::npos)continue;auto key=line.substr(p+1);if(!key.empty()&&key.back()=='\r')key.pop_back();out.emplace_back(key,out.size()+1);}
        if(out.size()!=n)throw std::runtime_error("Tranco CSV is too short");
    }else for(std::size_t i=0;i<n;++i){
        std::string key;const auto type=kind=="mixed"?i%3:kind=="skew"?2:1;
        if(type==0)key="user/"+hex_key(rng())+hex_key(i);
        else if(type==1){key="https://example.test/catalog/"+hex_key(rng())+hex_key(i);while(key.size()<156)key.push_back(static_cast<char>('a'+rng()%26));}
        else {key="https://example.test/shared/"+std::string(1+rng()%96,'a')+"b"+hex_key(i);while(key.size()<156)key.push_back(static_cast<char>('a'+rng()%26));}
        if(kind=="mixed")key=std::string(1,static_cast<char>('0'+type))+"/"+key;
        out.emplace_back(std::move(key),i+1);
    }
    std::sort(out.begin(),out.end(),pair_less);
    for(std::size_t i=1;i<out.size();++i)if(compare(out[i-1].first,out[i].first)>=0)throw std::runtime_error("duplicate data key");
    return out;
}
struct Query {std::string key;std::uint64_t expected=0;bool hit=false;};
inline std::vector<Query> make_queries(const std::vector<Pair>&keys,std::size_t count,std::uint64_t seed,bool tuning,std::size_t offset=0){
    std::vector<std::size_t> eligible;
    for(std::size_t i=0;i<keys.size();++i)if(((offset+i)%4==0)==tuning)eligible.push_back(i);
    if(eligible.empty())throw std::runtime_error("empty query partition");
    std::mt19937_64 rng(seed);std::vector<Query> out(count);
    for(auto&q:out){const auto&kv=keys[eligible[rng()%eligible.size()]];q.hit=rng()%100>=10;q.key=kv.first;if(q.hit)q.expected=kv.second;else q.key.push_back('\0');}
    return out;
}
}

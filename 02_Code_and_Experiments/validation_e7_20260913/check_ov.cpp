// E7 correctness gate: the three overflow lookup policies must agree exactly on
// found/value/source for every query on the same index, and each policy must pass
// the std::map differential (insert/update/upsert/erase/scan) on adversarial keys.
#include "selected.hpp"
#include <iostream>
#include <map>
#include <random>
#include <set>
using namespace validation;
static void require(bool x,const char*why){if(!x)throw std::runtime_error(why);}
using Reference=std::map<std::string,std::uint64_t,Less>;
static std::vector<Pair> expected_scan(const Reference&r,std::string_view key,std::size_t n){
    std::vector<Pair> out;for(auto it=r.lower_bound(std::string(key));it!=r.end()&&out.size()<n;++it)out.push_back(*it);return out;
}
static const std::vector<Mode>& all_modes(){
    static const std::vector<Mode> m{Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Linear,32},
        Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Binary,32},
        Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Hybrid,4},
        Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Hybrid,16},
        Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Hybrid,128},
        Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Hybrid,1}};
    return m;
}
int main(){try{
    std::mt19937_64 rng(20260913);std::set<std::string,Less> pool_set;
    for(const auto&s:std::vector<std::string>{"","a","aa","aaa","z",std::string("a\0b",3),std::string("\0",1),
        std::string("\xff",1),std::string(8,'\0'),std::string(8,'\xff'),std::string(4096,'q')})pool_set.insert(s);
    for(int i=0;i<2200;++i){std::string s=i%3?"common/prefix/path/":"";
        for(std::size_t j=0,m=rng()%100;j<m;++j)s.push_back(static_cast<char>(rng()%256));pool_set.insert(s);}
    const std::vector<std::string> pool(pool_set.begin(),pool_set.end());
    const auto&modes=all_modes();
    std::size_t operations=0,agreement_checks=0,scans=0,source_oracle_checks=0;
    std::vector<std::uint32_t> strcmp_delta;   // Binary minus Linear per overflow-hit lookup
    struct Trial {std::uint32_t slots;std::uint32_t block;std::size_t region;std::size_t step;};
    const std::vector<Trial> trials{
        {8,512,48,6000},{16,512,48,6000},{16,1024,48,6000},{32,1024,32,8000},{16,1024,48,6000}};
    for(const auto&tr:trials){
        Reference ref;std::vector<Pair> initial;
        for(std::size_t i=0;i<pool.size();i+=11){initial.emplace_back(pool[i],i);ref[pool[i]]=i;}
        Config cc;cc.block_bytes=tr.block;cc.slots=tr.slots;cc.region_keys=tr.region;
        Index index(cc);index.bulk_load(initial);

        // direct source oracle: the 4096-byte key can never fit a primary block,
        // so a hit on it must report source==2; a short key must report 1 (or 2).
        const std::string big(4096,'q');
        {const auto hit=index.find(big,modes[0]);
         if(hit.found){require(hit.source==2,"oversized key must be overflow hit");++source_oracle_checks;}
         const auto miss=index.find(std::string("no such key anywhere"),modes[0]);
         require(!miss.found&&miss.source==0,"miss must be source 0");++source_oracle_checks;}

        for(std::size_t step=0;step<tr.step;++step){
            const auto&key=pool[rng()%pool.size()];const std::uint64_t value=step%7?rng():0;const auto op=rng()%7;
            if(op==0){const bool want=ref.emplace(key,value).second;require(index.insert(key,value)==want,"insert");}
            else if(op==1){auto it=ref.find(key);const bool want=it!=ref.end();if(want)it->second=value;require(index.update(key,value)==want,"update");}
            else if(op==2){const bool want=ref.erase(key)!=0;require(index.erase(key)==want,"erase");}
            else if(op==3){const bool want=ref.find(key)==ref.end();ref[key]=value;require(index.upsert(key,value)==want,"upsert");}
            else if(op==4){const auto n=rng()%30;require(index.scan(key,n)==expected_scan(ref,key,n),"range scan");++scans;}
            else{
                Result first;bool first_set=false;
                const auto it=ref.find(key);
                for(const auto&md:modes){
                    const auto got=index.find(key,md);
                    require(got.found==(it!=ref.end())&&(!got.found||got.value==it->second),"find vs map");
                    if(first_set){
                        require(got.found==first.found&&(!got.found||got.value==first.value),"policy found/value disagree");
                        require(got.source==first.source,"policy source disagree");
                    }else{first=got;first_set=true;}
                }
                require(!first.found||first.source==1||first.source==2,"source domain");
                if(first.found&&first.source==2){
                    const auto bin=index.find(key,modes[1]),lin=index.find(key,modes[0]);
                    strcmp_delta.push_back(bin.str_cmps>=lin.str_cmps?bin.str_cmps-lin.str_cmps:0);
                }
                ++agreement_checks;
            }
            require(index.size()==ref.size(),"size");++operations;
            if(step%500==0)require(index.scan({},ref.size()+1)==expected_scan(ref,{},ref.size()+1),"full scan");
        }
        {std::vector<Pair> want;for(const auto&kv:ref)want.push_back(kv);
         require(index.scan({},ref.size()+1)==want,"final scan");}
    }
    double mean_delta=0;if(!strcmp_delta.empty()){double s=0;for(auto d:strcmp_delta)s+=d;mean_delta=s/strcmp_delta.size();}
    std::cout<<"{\"status\":\"passed\",\"operations\":"<<operations
        <<",\"policy_agreement_checks\":"<<agreement_checks
        <<",\"scan_checks\":"<<scans
        <<",\"source_oracle_checks\":"<<source_oracle_checks
        <<",\"overflow_hit_lookups\":"<<strcmp_delta.size()
        <<",\"mean_strcmp_delta_binary_minus_linear\":"<<mean_delta
        <<",\"seed\":20260913}"<<std::endl;
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<"\n";return 1;}}

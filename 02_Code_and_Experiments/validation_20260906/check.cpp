#include "index.hpp"
#include <iostream>
#include <map>
#include <random>
#include <set>
using namespace validation;
static void require(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
static std::vector<Pair> expected_scan(const std::map<std::string,std::uint64_t,Less>&ref,std::string_view q,std::size_t n){
    std::vector<Pair> out;for(auto it=ref.lower_bound(std::string(q));it!=ref.end()&&out.size()<n;++it)out.push_back(*it);return out;
}
int main(){try{
    std::mt19937_64 rng(20260906);std::set<std::string,Less> pool_set;
    for(const std::string &s:std::vector<std::string>{"","a","aa","aaa","ab",std::string("a\0b",3),std::string("\0",1),std::string("\xff",1),std::string(4096,'q')})pool_set.insert(s);
    for(int i=0;i<2500;++i){
        std::string s=i%3?"https://example.test/long/shared/path/":"";
        auto n=rng()%96;
        for(std::size_t j=0;j<n;++j)s.push_back(static_cast<char>(rng()%256));
        pool_set.insert(s);
    }
    std::vector<std::string> pool(pool_set.begin(),pool_set.end());
    std::size_t fingerprint_checks=0,model_checks=0,operations=0,scan_checks=0,total_rebuilds=0,total_splits=0;
    for(const auto&s:pool)for(auto cut:std::array<std::size_t,4>{0,1,7,15}){
        const auto n=std::min(cut,s.size());Fingerprint f;for(std::size_t j=0;j<n;++j)f.byte(static_cast<unsigned char>(s[j]));f.tail(std::string_view(s).substr(n));
        require(f.tag()==Fingerprint::of(s),"streaming CRC differs");++fingerprint_checks;
    }
    for(int trial=0;trial<5;++trial){
        Config cfg;cfg.block_bytes=trial==0?512:trial==1?2048:1024;cfg.region_keys=48;
        if(trial==3)cfg.canonical=Route::Residual;
        if(trial==4){cfg.fixed_groups=1;cfg.region_keys=0;}
        Index index(cfg);std::map<std::string,std::uint64_t,Less> ref;
        std::vector<Pair> initial;
        if(trial%2)for(std::size_t i=0;i<pool.size();i+=13){initial.emplace_back(pool[i],i);ref[pool[i]]=i;}
        index.bulk_load(initial);
        auto modes=std::vector<Mode>{{Route::Full,Prefetch::Off,true},{Route::Interval,Prefetch::Off,true},{Route::Interval,Prefetch::Early,true},{Route::Interval,Prefetch::Late,true},{Route::Interval,Prefetch::Off,false}};
        if(cfg.canonical==Route::Residual)modes={{Route::Residual,Prefetch::Off,true},{Route::Residual,Prefetch::Early,true}};
        const int count=trial==4?3000:15000;
        for(int step=0;step<count;++step){
            const auto&key=pool[rng()%pool.size()];const auto value=(rng()%5==0)?0:rng();const auto op=rng()%7;
            if(op==0){bool want=ref.emplace(key,value).second;require(index.insert(key,value)==want,"insert result");}
            else if(op==1){auto it=ref.find(key);bool want=it!=ref.end();if(want)it->second=value;require(index.update(key,value)==want,"update result");}
            else if(op==2){bool want=ref.erase(key)!=0;require(index.erase(key)==want,"erase result");}
            else if(op==3){bool want=ref.find(key)==ref.end();ref[key]=value;require(index.upsert(key,value)==want,"upsert result");}
            else if(op==4){const auto n=rng()%40;require(index.scan(key,n)==expected_scan(ref,key,n),"range scan mismatch");++scan_checks;}
            else {auto it=ref.find(key);for(auto mode:modes){auto got=index.find(key,mode);require(got.found==(it!=ref.end()),"lookup existence");if(got.found)require(got.value==it->second,"lookup value");}}
            require(index.size()==ref.size(),"size mismatch");++operations;
            if(step%503==0){
                require(index.scan({},ref.size()+1)==expected_scan(ref,{},ref.size()+1),"complete ordered scan");++scan_checks;
                for(int q=0;q<30;++q){
                    const auto&key2=pool[rng()%pool.size()];const auto&r=index.region(index.locate(key2));
                    std::string_view s=key2;Fingerprint a,b;
                    auto f=r.model().predict<false>(s,r.groups(),Route::Full,a),e=r.model().predict<false>(s,r.groups(),Route::Interval,b);
                    require(f.group==e.group,"interval changed full-model bucket");++model_checks;
                }
            }
            if(step%997==0){index.rebuild_all();require(index.scan({},ref.size()+1)==expected_scan(ref,{},ref.size()+1),"rebuild changed records");++scan_checks;}
        }
        total_rebuilds+=index.rebuilds;total_splits+=index.splits;
        require(index.scan({},ref.size()+1)==expected_scan(ref,{},ref.size()+1),"final ordered scan");
    }
    // Exhaustive short binary-string ordering and early-group agreement.
    std::vector<Pair> exhaustive{{"",0}};
    for(int length=1;length<=5;++length){std::size_t count=1ULL<<(2*length);for(std::size_t x=0;x<count;++x){std::string s(length,'\0');auto v=x;for(int j=length-1;j>=0;--j){const unsigned char a[4]={0,97,98,255};s[j]=static_cast<char>(a[v&3]);v>>=2;}exhaustive.emplace_back(s,x);}}
    std::sort(exhaustive.begin(),exhaustive.end(),pair_less);
    Config ec;ec.region_keys=0;Region er(exhaustive,ec);std::uint32_t previous=0;
    for(const auto&kv:exhaustive){Fingerprint a,b;auto f=er.model().predict<false>(kv.first,er.groups(),Route::Full,a),e=er.model().predict<false>(kv.first,er.groups(),Route::Interval,b);require(f.group==e.group,"exhaustive group mismatch");require(f.group>=previous,"nonmonotone groups");previous=f.group;++model_checks;}
    std::cout<<"{\"status\":\"passed\",\"random_operations\":"<<operations<<",\"scan_checks\":"<<scan_checks<<",\"model_checks\":"<<model_checks<<",\"fingerprint_checks\":"<<fingerprint_checks<<",\"rebuilds\":"<<total_rebuilds<<",\"splits\":"<<total_splits<<",\"seed\":20260906}"<<std::endl;
}catch(const std::exception&e){std::cerr<<"FAILED: "<<e.what()<<std::endl;return 1;}}

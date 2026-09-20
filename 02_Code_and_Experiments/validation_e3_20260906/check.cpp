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
int main(){try{
    std::mt19937_64 rng(20260909);std::set<std::string,Less> pool_set;
    for(const auto&s:std::vector<std::string>{"","a","aa","aaa","z",std::string("a\0b",3),std::string("\0",1),std::string("\xff",1),std::string(4096,'q')})pool_set.insert(s);
    for(int i=0;i<2200;++i){std::string s=i%3?"common/prefix/path/":"";for(std::size_t j=0,n=rng()%100;j<n;++j)s.push_back(static_cast<char>(rng()%256));pool_set.insert(s);}
    std::vector<std::string> pool(pool_set.begin(),pool_set.end());
    std::size_t operations=0,scans=0,rebuilds=0,splits=0,stats_checks=0;
    for(int trial=0;trial<=CANDIDATES;++trial){
        std::vector<Pair> initial;Reference ref;
        if(trial%2||trial==CANDIDATES)for(std::size_t i=0;i<pool.size();i+=11){initial.emplace_back(pool[i],i);ref[pool[i]]=i;}
        const std::size_t r=32,regions=std::max<std::size_t>(1,(initial.size()+r-1)/r);
        std::vector<int> ids(regions);for(std::size_t i=0;i<regions;++i)ids[i]=trial==CANDIDATES?i%CANDIDATES:trial;
        SelectedIndex index;index.bulk_load(initial,ids,r);
        const int count=trial==CANDIDATES?20000:5000;
        for(int step=0;step<count;++step){
            const auto&key=pool[rng()%pool.size()];const std::uint64_t value=step%7?rng():0;const auto op=rng()%7;
            if(op==0){const bool want=ref.emplace(key,value).second;require(index.insert(key,value)==want,"insert");}
            else if(op==1){auto it=ref.find(key);const bool want=it!=ref.end();if(want)it->second=value;require(index.update(key,value)==want,"update");}
            else if(op==2){const bool want=ref.erase(key)!=0;require(index.erase(key)==want,"erase");}
            else if(op==3){const bool want=ref.find(key)==ref.end();ref[key]=value;require(index.upsert(key,value)==want,"upsert");}
            else if(op==4){const auto n=rng()%30;require(index.scan(key,n)==expected_scan(ref,key,n),"range scan");++scans;}
            else {const auto it=ref.find(key);const auto got=index.find(key);require(got.found==(it!=ref.end())&&(!got.found||got.value==it->second),"find");}
            require(index.size()==ref.size(),"size");++operations;
            if(step%223==0)index.reconfigure(rng()%index.region_count(),rng()%CANDIDATES);
            if(step%113==0){
                require(index.scan({},ref.size()+1)==expected_scan(ref,{},ref.size()+1),"all records after migration");++scans;
                for(std::size_t i=0;i<index.region_count();++i){
                    const auto&leaf=index.leaf(i);if(leaf.candidate_id()==SORTED_LEAF)continue;
                    std::size_t keys=0,overflow=0;const auto capacity=choice(leaf.candidate_id()).config.block_bytes-320;
                    for(const auto&s:leaf.group_stats()){
                        require(s.primary_entries+s.free_entries==16,"entry capacity");
                        require(s.primary_bytes+s.free_bytes+s.garbage_bytes==capacity,"byte capacity");
                        require(s.blocked_slots_only+s.blocked_bytes_only+s.blocked_both+s.fits_now==s.overflow_entries,"overflow reason partition");
                        require(s.oversized<=s.overflow_entries,"oversized count");
                        keys+=s.primary_entries+s.overflow_entries;overflow+=s.overflow_entries;++stats_checks;
                    }
                    require(keys==leaf.size()&&overflow==leaf.overflow_count(),"stats record conservation");
                }
            }
        }
        require(index.scan({},ref.size()+1)==expected_scan(ref,{},ref.size()+1),"final records");
        rebuilds+=index.rebuilds;splits+=index.splits;
    }
    std::vector<Pair> strings{{"",0}};
    for(int len=1;len<=5;++len)for(std::size_t x=0;x<(1ULL<<(2*len));++x){
        std::string s(len,0);auto v=x;for(int j=len-1;j>=0;--j){const unsigned char alphabet[]={0,97,98,255};s[j]=static_cast<char>(alphabet[v&3]);v>>=2;}strings.emplace_back(s,x);
    }
    std::sort(strings.begin(),strings.end(),pair_less);std::size_t model_checks=0;
    for(int c=0;c<SORTED_LEAF;++c){Region r(strings,choice(c).config);std::uint32_t previous=0;
        for(const auto&kv:strings){Fingerprint a,b;const auto s=r.residual(kv.first);auto full=r.model().predict<false>(s,r.groups(),Route::Full,a),early=r.model().predict<false>(s,r.groups(),Route::Interval,b);
            require(full.group==early.group&&full.group>=previous,"model interval/order");previous=full.group;++model_checks;}
    }
    std::size_t selection_checks=0;double worst_gap=0;
    for(int t=0;t<100;++t){
        Matrix m(3);std::vector<double>w{.2,.3,.5};Allowed allowed(3);std::vector<int> ref(3,REFERENCE);
        for(std::size_t i=0;i<m.size();++i)for(int c=0;c<9;++c){m[i][c].bytes=50+rng()%400;m[i][c].tune_ns=20+rng()%300;allowed[i].push_back(c);}
        const auto budget=selected_bytes(m,ref);const auto picked=select(m,w,allowed,budget,ref);
        require(selected_bytes(m,picked)<=budget,"budget exceeded");
        require(selected_cost(m,w,picked)<=selected_cost(m,w,ref)+1e-9,"incumbent regressed");
        double optimum=std::numeric_limits<double>::infinity();
        for(int a=0;a<9;++a)for(int b=0;b<9;++b)for(int c=0;c<9;++c){std::vector<int>s{a,b,c};if(selected_bytes(m,s)<=budget)optimum=std::min(optimum,selected_cost(m,w,s));}
        require(selected_cost(m,w,picked)+1e-9>=optimum,"impossible optimum");
        worst_gap=std::max(worst_gap,selected_cost(m,w,picked)-optimum);++selection_checks;
    }
    std::cout<<"{\"status\":\"passed\",\"operations\":"<<operations<<",\"scans\":"<<scans<<",\"model_checks\":"<<model_checks<<",\"group_stats_checks\":"<<stats_checks<<",\"rebuilds\":"<<rebuilds<<",\"splits\":"<<splits<<",\"selection_checks\":"<<selection_checks<<",\"selector_max_additive_gap_toy_ns\":"<<worst_gap<<",\"seed\":20260909}"<<std::endl;
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<std::endl;return 1;}}

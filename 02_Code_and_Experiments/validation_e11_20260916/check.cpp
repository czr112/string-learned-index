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
    std::mt19937_64 rng(20260913);std::set<std::string,Less> pool_set;
    for(const auto&s:std::vector<std::string>{"","a","aa","aaa","z",std::string("a\0b",3),std::string("\0",1),std::string("\xff",1),std::string(4096,'q')})pool_set.insert(s);
    for(int i=0;i<2200;++i){std::string s=i%3?"common/prefix/path/":"";for(std::size_t j=0,n=rng()%100;j<n;++j)s.push_back(static_cast<char>(rng()%256));pool_set.insert(s);}
    std::vector<std::string> pool(pool_set.begin(),pool_set.end());
    std::size_t operations=0,scans=0,rebuilds=0,splits=0,stats_checks=0,simd_ops=0,model_checks=0,fingerprint_checks=0;
    // E5-1: fingerprint streaming check (unchanged from E3).
    for(const auto&s:pool)for(auto cut:std::array<std::size_t,4>{0,1,7,15}){
        const auto n=std::min(cut,s.size());Fingerprint f;for(std::size_t j=0;j<n;++j)f.byte(static_cast<unsigned char>(s[j]));f.tail(std::string_view(s).substr(n));
        require(f.tag()==Fingerprint::of(s),"streaming CRC differs");++fingerprint_checks;
    }
    // E5-1: layout trials over slot counts. Conservation uses 64+S*16, never 320.
    struct Trial {int id;std::size_t ops;std::size_t region;};
    const std::vector<Trial> trials={
        {9,6000,48},   // c16 b512  S=8
        {11,6000,48},  // c16 b2048 S=8
        {0,6000,48},   // c16 b512  S=16
        {1,6000,48},   // c16 b1024 S=16 (reference layout)
        {18,6000,48},  // c16 b1024 S=32
        {19,6000,32},  // c16 b2048 S=32
        {7,15000,32},  // heterogeneous: cycles ids below
    };
    const std::vector<int> hetero_ids{9,0,18,1,19,4,24};
    const std::array<Mode,2> modes{Mode{Route::Interval,Prefetch::Off,true,false},Mode{Route::Interval,Prefetch::Off,true,true}};
    for(const auto&trial:trials){
        const bool hetero=trial.id==7;
        Reference ref;std::vector<Pair> initial;
        for(std::size_t i=0;i<pool.size();i+=13){initial.emplace_back(pool[i],i);ref[pool[i]]=i;}
        const std::size_t r=trial.region,regions=std::max<std::size_t>(1,(initial.size()+r-1)/r);
        std::vector<int> ids(regions);
        for(std::size_t i=0;i<regions;++i)ids[i]=hetero?hetero_ids[i%hetero_ids.size()]:trial.id;
        SelectedIndex index;index.bulk_load(initial,ids,r);
        for(int step=0;step<int(trial.ops);++step){
            const auto&key=pool[rng()%pool.size()];const std::uint64_t value=step%7?rng():0;const auto op=rng()%7;
            if(op==0){const bool want=ref.emplace(key,value).second;require(index.insert(key,value)==want,"insert");}
            else if(op==1){auto it=ref.find(key);const bool want=it!=ref.end();if(want)it->second=value;require(index.update(key,value)==want,"update");}
            else if(op==2){const bool want=ref.erase(key)!=0;require(index.erase(key)==want,"erase");}
            else if(op==3){const bool want=ref.find(key)==ref.end();ref[key]=value;require(index.upsert(key,value)==want,"upsert");}
            else if(op==4){const auto n=rng()%30;require(index.scan(key,n)==expected_scan(ref,key,n),"range scan");++scans;}
            else{const auto it=ref.find(key);const auto got0=index.find(key,modes[0]);const auto got1=index.find(key,modes[1]);
                require(got0.found==got1.found&&(!got0.found||got0.value==got1.value),"simd/scalar disagree");
                require(got0.found==(it!=ref.end())&&(!got0.found||got0.value==it->second),"find");
                if(got0.found)++simd_ops;}
            require(index.size()==ref.size(),"size");++operations;
            if(step%223==0)index.reconfigure(rng()%index.region_count(),hetero?hetero_ids[rng()%hetero_ids.size()]:rng()%CANDIDATES);
            if(step%113==0){
                require(index.scan({},ref.size()+1)==expected_scan(ref,{},ref.size()+1),"all records after migration");++scans;
                for(std::size_t i=0;i<index.region_count();++i){
                    const auto&leaf=index.leaf(i);if(leaf.candidate_id()==SORTED_LEAF)continue;
                    const auto&spec=cand_table[std::size_t(leaf.candidate_id())];
                    const auto capacity=spec.block-(sizeof(Header)+std::size_t(spec.slots)*sizeof(Entry));
                    for(const auto&s:leaf.group_stats()){
                        require(s.primary_entries+s.free_entries==spec.slots,"entry capacity");
                        require(s.primary_bytes+s.free_bytes+s.garbage_bytes==capacity,"byte capacity");
                        require(s.blocked_slots_only+s.blocked_bytes_only+s.blocked_both+s.fits_now==s.overflow_entries,"overflow reason partition");
                        require(s.oversized<=s.overflow_entries,"oversized count");
                        ++stats_checks;
                    }
                }
            }
        }
        require(index.scan({},ref.size()+1)==expected_scan(ref,{},ref.size()+1),"final records");
        rebuilds+=index.rebuilds;splits+=index.splits;
    }
    // E5-1: exhaustive short binary strings: early-stop group agreement + monotone
    // order for several slot layouts, including the two extremes.
    std::vector<Pair> strings{{"",0}};
    for(int len=1;len<=5;++len)for(std::size_t x=0;x<(1ULL<<(2*len));++x){
        std::string s(len,0);auto v=x;for(int j=len-1;j>=0;--j){const unsigned char alphabet[]={0,97,98,255};s[j]=static_cast<char>(alphabet[v&3]);v>>=2;}strings.emplace_back(s,x);
    }
    std::sort(strings.begin(),strings.end(),pair_less);
    for(int id:std::array<int,4>{9,1,18,19}){
        Region r(strings,choice(id).config);std::uint32_t previous=0;
        for(const auto&kv:strings){Fingerprint a,b;const auto s=r.residual(kv.first);
            const auto full=r.model().predict<false>(s,r.groups(),Route::Full,a),early=r.model().predict<false>(s,r.groups(),Route::Interval,b);
            require(full.group==early.group&&full.group>=previous,"model interval/order");previous=full.group;++model_checks;}
    }
    // E5-3: selector check on the widened candidate table (0..SORTED_LEAF-1 learned).
    std::size_t selection_checks=0;double worst_gap=0;
    for(int t=0;t<100;++t){
        Matrix m(3);std::vector<double>w{.2,.3,.5};Allowed allowed(3);std::vector<int> ref(3,REFERENCE);
        for(std::size_t i=0;i<m.size();++i)for(int c=0;c<SORTED_LEAF;++c){m[i][c].bytes=50+rng()%400;m[i][c].tune_ns=20+rng()%300;allowed[i].push_back(c);}
        const auto budget=selected_bytes(m,ref);const auto picked=select(m,w,allowed,budget,ref);
        require(selected_bytes(m,picked)<=budget,"budget exceeded");
        require(selected_cost(m,w,picked)<=selected_cost(m,w,ref)+1e-9,"incumbent regressed");
        double optimum=std::numeric_limits<double>::infinity();
        for(int a=0;a<SORTED_LEAF;++a)for(int b=0;b<SORTED_LEAF;++b)for(int c=0;c<SORTED_LEAF;++c){std::vector<int>s{a,b,c};if(selected_bytes(m,s)<=budget)optimum=std::min(optimum,selected_cost(m,w,s));}
        require(selected_cost(m,w,picked)+1e-9>=optimum,"impossible optimum");
        worst_gap=std::max(worst_gap,selected_cost(m,w,picked)-optimum);++selection_checks;
    }
    std::cout<<"{\"status\":\"passed\",\"operations\":"<<operations<<",\"scans\":"<<scans
        <<",\"simd_scalar_find_pairs\":"<<simd_ops<<",\"model_checks\":"<<model_checks
        <<",\"group_stats_checks\":"<<stats_checks<<",\"fingerprint_checks\":"<<fingerprint_checks
        <<",\"rebuilds\":"<<rebuilds<<",\"splits\":"<<splits
        <<",\"selection_checks\":"<<selection_checks<<",\"selector_max_additive_gap_toy_units\":"<<worst_gap
        <<",\"seed\":20260913}"<<std::endl;
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<std::endl;return 1;}}

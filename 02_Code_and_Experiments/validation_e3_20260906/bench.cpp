#include "data.hpp"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
using namespace validation;
using Clock=std::chrono::steady_clock;
static volatile std::uint64_t sink=0;
static double elapsed(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::nano>(b-a).count();}
static double median(std::vector<double> x){std::sort(x.begin(),x.end());return x[x.size()/2];}
static std::uint64_t checksum(Result r){return r.found?r.value+0x9e3779b97f4a7c15ULL:0;}
static std::uint64_t expected_sum(const std::vector<Query>&q){std::uint64_t n=0;for(const auto&x:q)n+=checksum({x.hit,x.expected});return n;}
static void verify(Result r,const Query&q){if(r.found!=q.hit||(r.found&&r.value!=q.expected))throw std::runtime_error("query mismatch");}
static std::uint64_t signature(const std::vector<int>&v){std::uint64_t h=1469598103934665603ULL;for(int x:v){h^=x;h*=1099511628211ULL;}return h;}
struct Policy {std::string name;std::vector<int> ids;std::unique_ptr<SelectedIndex> index;double build_ms=0,objective_ns=0;};
int main(int argc,char**argv){try{
    if(argc<5)throw std::invalid_argument("bench KIND N OUTPUT_DIR TRANC0_CSV [QUERIES] [ROUNDS] [SEEDS]");
    const std::string kind=argv[1];const std::size_t n=std::stoull(argv[2]);const std::filesystem::path dir=argv[3];
    const auto queries=argc>5?std::stoull(argv[5]):262144;const int rounds=argc>6?std::stoi(argv[6]):5,seeds=argc>7?std::stoi(argv[7]):3;
    if(n<512||!queries||rounds<1||seeds<1)throw std::invalid_argument("invalid size");
    std::filesystem::create_directories(dir);const auto keys=make_data(kind,n,argv[4]);
    constexpr std::size_t REGION=2048,TUNE_QUERIES=512,TUNE_PASSES=4,TUNE_ROUNDS=3;
    const auto nr=(n+REGION-1)/REGION;Matrix metrics(nr);std::vector<double> weights(nr);
    std::ofstream candidates(dir/"candidates.csv");candidates.exceptions(std::ios::badbit|std::ios::failbit);candidates<<std::setprecision(12);
    candidates<<"region,key_count,candidate,model,block,bytes,overflow_keys,primary_keys,primary_suffix_bytes,overflow_suffix_bytes,blocked_slots_only,blocked_bytes_only,blocked_both,oversized,fits_now,prefix_length,groups,build_ns,tune_r0_ns,tune_r1_ns,tune_r2_ns,tune_median_ns\n";
    const auto setup0=Clock::now();
    for(std::size_t i=0;i<nr;++i){
        const auto begin=i*REGION,end=std::min(n,begin+REGION);std::vector<Pair> part(keys.begin()+begin,keys.begin()+end);
        weights[i]=static_cast<double>(part.size())/n;
        const auto tune=make_queries(part,TUNE_QUERIES,20260910+i,true,begin);const auto want=expected_sum(tune)*TUNE_PASSES;
        std::vector<int> order(CANDIDATES);std::iota(order.begin(),order.end(),0);std::mt19937_64 rng(612093+i);std::shuffle(order.begin(),order.end(),rng);
        for(int c:order){
            const auto start=Clock::now();SelectedLeaf leaf(part,choice(c));const auto stop=Clock::now();
            auto&v=metrics[i][c];v.bytes=leaf.memory();v.overflow=leaf.overflow_count();v.build_ns=elapsed(start,stop);
            for(const auto&q:tune)verify(leaf.find(q.key),q);
            std::vector<double> times;
            for(std::size_t rep=0;rep<TUNE_ROUNDS;++rep){
                std::uint64_t sum=0;const auto t0=Clock::now();
                for(std::size_t pass=0;pass<TUNE_PASSES;++pass)for(const auto&q:tune)sum+=checksum(leaf.find(q.key));
                const auto t1=Clock::now();sink=sum;if(sum!=want)throw std::runtime_error("tuning checksum mismatch");
                times.push_back(elapsed(t0,t1)/(TUNE_QUERIES*TUNE_PASSES));
            }
            v.tune_ns=median(times);GroupStats total;const auto stats=leaf.group_stats();
            for(const auto&s:stats){total.primary_entries+=s.primary_entries;total.primary_bytes+=s.primary_bytes;total.overflow_bytes+=s.overflow_bytes;total.blocked_slots_only+=s.blocked_slots_only;total.blocked_bytes_only+=s.blocked_bytes_only;total.blocked_both+=s.blocked_both;total.oversized+=s.oversized;total.fits_now+=s.fits_now;}
            candidates<<i<<','<<part.size()<<','<<c<<','<<model_name(c)<<','<<(c==SORTED_LEAF?0:choice(c).config.block_bytes)<<','<<v.bytes<<','<<v.overflow<<','<<total.primary_entries<<','<<total.primary_bytes<<','<<total.overflow_bytes<<','<<total.blocked_slots_only<<','<<total.blocked_bytes_only<<','<<total.blocked_both<<','<<total.oversized<<','<<total.fits_now<<','<<leaf.prefix_length()<<','<<stats.size()<<','<<v.build_ns<<','<<times[0]<<','<<times[1]<<','<<times[2]<<','<<v.tune_ns<<'\n';
        }
        if((i+1)%64==0||i+1==nr)std::cerr<<"TUNED "<<i+1<<'/'<<nr<<'\n';
    }
    candidates.close();
    std::vector<int> reference(nr,REFERENCE);const auto budget=selected_bytes(metrics,reference);
    std::vector<int> uniform=reference;double best=selected_cost(metrics,weights,uniform);
    for(int c=0;c<SORTED_LEAF;++c){std::vector<int> v(nr,c);const auto cost=selected_cost(metrics,weights,v);if(selected_bytes(metrics,v)<=budget&&cost<best){best=cost;uniform=v;}}
    Allowed model_only(nr,std::vector<int>{1,4,7});
    auto first=select(metrics,weights,model_only,budget,reference);Allowed block_only(nr);
    for(std::size_t i=0;i<nr;++i)block_only[i]={first[i]/3*3,first[i]/3*3+1,first[i]/3*3+2};
    auto sequential=select(metrics,weights,block_only,budget,first);
    Allowed all_learned(nr),all(nr);
    for(std::size_t i=0;i<nr;++i){for(int c=0;c<SORTED_LEAF;++c)all_learned[i].push_back(c);all[i]=all_learned[i];all[i].push_back(SORTED_LEAF);}
    auto incumbent=selected_cost(metrics,weights,sequential)<selected_cost(metrics,weights,uniform)?sequential:uniform;
    auto joint=select(metrics,weights,all_learned,budget,incumbent);auto hybrid=select(metrics,weights,all,budget,joint);
    const double setup_ms=elapsed(setup0,Clock::now())/1e6;
    std::vector<Policy> policies;
    auto add=[&](const std::string&name,std::vector<int> ids){Policy p;p.name=name;p.ids=std::move(ids);p.objective_ns=selected_cost(metrics,weights,p.ids);policies.push_back(std::move(p));};
    add("fixed_context1024",reference);add("uniform_tuned",uniform);add("sequential",sequential);add("joint_learned",joint);add("joint_with_leaf",hybrid);add("sorted_leaves",std::vector<int>(nr,SORTED_LEAF));
    std::ofstream selections(dir/"selections.csv");selections.exceptions(std::ios::badbit|std::ios::failbit);selections<<"method,region,key_count,candidate,model,block,bytes,overflow_keys,prefix_length,tune_ns\n"<<std::setprecision(12);
    for(auto&p:policies){
        const auto t0=Clock::now();p.index=std::make_unique<SelectedIndex>();p.index->bulk_load(keys,p.ids,REGION);p.build_ms=elapsed(t0,Clock::now())/1e6;
        if(p.index->payload_memory()!=selected_bytes(metrics,p.ids))throw std::runtime_error("built bytes differ from candidate bytes");
        if(p.name!="sorted_leaves"&&p.index->payload_memory()>budget)throw std::runtime_error("built selection violates budget");
        for(std::size_t i=0;i<nr;++i){const auto&l=p.index->leaf(i);const auto c=p.ids[i];selections<<p.name<<','<<i<<','<<l.size()<<','<<c<<','<<model_name(c)<<','<<(c==SORTED_LEAF?0:choice(c).config.block_bytes)<<','<<l.memory()<<','<<l.overflow_count()<<','<<l.prefix_length()<<','<<metrics[i][c].tune_ns<<'\n';}
    }
    selections.close();
    std::ofstream groups(dir/"joint_groups.csv");groups.exceptions(std::ios::badbit|std::ios::failbit);groups<<"region,group,candidate,primary_entries,primary_bytes,overflow_entries,overflow_bytes,free_entries,free_bytes,garbage_bytes,blocked_slots_only,blocked_bytes_only,blocked_both,oversized,fits_now\n";
    for(std::size_t i=0;i<nr;++i)for(const auto&s:policies[3].index->leaf(i).group_stats())groups<<i<<','<<s.group<<','<<joint[i]<<','<<s.primary_entries<<','<<s.primary_bytes<<','<<s.overflow_entries<<','<<s.overflow_bytes<<','<<s.free_entries<<','<<s.free_bytes<<','<<s.garbage_bytes<<','<<s.blocked_slots_only<<','<<s.blocked_bytes_only<<','<<s.blocked_both<<','<<s.oversized<<','<<s.fits_now<<'\n';
    groups.close();
    std::size_t sorted_bytes=keys.capacity()*sizeof(Pair);for(const auto&kv:keys)sorted_bytes+=kv.first.capacity()+1;
    std::ofstream output(dir/"timings.csv");output.exceptions(std::ios::badbit|std::ios::failbit);output<<std::setprecision(12);
    output<<"dataset,n,queries,query_seed,repeat,method,ns_per_op,mops,bytes,leaf_bytes,budget_leaf_bytes,budget_total_bytes,budget_feasible,tune_objective_ns,overflow_keys,sorted_regions,selection_signature,build_ms,setup_ms,checksum\n";
    const auto outer=policies[0].index->memory()-policies[0].index->payload_memory();
    for(int seed=0;seed<seeds;++seed){
        const std::uint64_t qseed=20260920+seed;const auto eval=make_queries(keys,queries,qseed,false);const auto want=expected_sum(eval);
        auto lookup=[&](std::size_t method,const Query&q){
            if(method<policies.size())return policies[method].index->find(q.key);
            auto it=std::lower_bound(keys.begin(),keys.end(),q.key,[](const Pair&a,std::string_view b){return compare(a.first,b)<0;});
            return it!=keys.end()&&!compare(it->first,q.key)?Result{true,it->second}:Result{};
        };
        std::vector<std::size_t> order(policies.size()+1);std::iota(order.begin(),order.end(),0);std::mt19937_64 rng(qseed+390);
        for(int rep=0;rep<rounds;++rep){
            std::shuffle(order.begin(),order.end(),rng);
            for(auto method:order){
                std::uint64_t warm=0;for(std::size_t i=0;i<std::min<std::size_t>(20000,eval.size());++i){auto r=lookup(method,eval[i]);verify(r,eval[i]);warm+=checksum(r);}sink=warm;
                std::uint64_t sum=0;const auto t0=Clock::now();for(const auto&q:eval)sum+=checksum(lookup(method,q));const auto t1=Clock::now();sink=sum;
                if(sum!=want)throw std::runtime_error("evaluation checksum mismatch");
                const auto ns=elapsed(t0,t1)/eval.size();
                const bool array=method==policies.size();const auto bytes=array?sorted_bytes:policies[method].index->memory();
                const auto leaf_bytes=array?0:policies[method].index->payload_memory();
                output<<kind<<','<<n<<','<<queries<<','<<qseed<<','<<rep<<','<<(array?"sorted_array":policies[method].name)<<','<<ns<<','<<1000/ns<<','<<bytes<<','<<leaf_bytes<<','<<budget<<','<<budget+outer<<','<<(bytes<=budget+outer)<<','<<(array?0:policies[method].objective_ns)<<','<<(array?0:policies[method].index->overflow_count())<<','<<(array?0:std::count(policies[method].ids.begin(),policies[method].ids.end(),SORTED_LEAF))<<','<<(array?0:signature(policies[method].ids))<<','<<(array?0:policies[method].build_ms)<<','<<setup_ms<<','<<sum<<'\n';
            }
            output.flush();
        }
        std::cerr<<"EVALUATED seed="<<qseed<<" rounds="<<rounds<<'\n';
    }
    output.close();
    std::cerr<<"COMPLETED "<<kind<<" n="<<n<<" setup_ms="<<setup_ms<<" budget="<<budget+outer<<'\n';
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<std::endl;return 1;}}

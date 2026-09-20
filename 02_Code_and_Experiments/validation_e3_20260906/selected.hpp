#pragma once
#include "index.hpp"
#include <cmath>
#include <numeric>

namespace validation {
constexpr int REFERENCE=1, SORTED_LEAF=9, CANDIDATES=10;
struct Choice {int id=REFERENCE;Config config;bool sorted=false;};
inline Choice choice(int id){
    if(id<0||id>=CANDIDATES)throw std::invalid_argument("candidate id");
    Choice c;c.id=id;c.sorted=id==SORTED_LEAF;
    if(!c.sorted){c.config.model_kind=static_cast<ModelKind>(id/3);c.config.block_bytes=512U<<(id%3);}
    return c;
}
inline const char* model_name(int id){
    if(id==SORTED_LEAF)return "sorted";
    const char* names[]={"context16","position16","unigram"};return names[id/3];
}
inline constexpr Mode query_mode{Route::Interval,Prefetch::Off,true};

// All policies use this same wrapper and outer router. The sorted fallback also
// removes the region's shared prefix, and owns its suffix strings.
class SelectedLeaf {
    Choice choice_;
    std::unique_ptr<Region> learned_;
    std::string prefix_;
    std::vector<Pair> suffixes_;
    auto lower(std::string_view s){return std::lower_bound(suffixes_.begin(),suffixes_.end(),s,[](const Pair&a,std::string_view b){return compare(a.first,b)<0;});}
    auto lower(std::string_view s)const{return std::lower_bound(suffixes_.begin(),suffixes_.end(),s,[](const Pair&a,std::string_view b){return compare(a.first,b)<0;});}
public:
    SelectedLeaf(const std::vector<Pair>& data,Choice c):choice_(c){
        if(!c.sorted){learned_=std::make_unique<Region>(data,c.config);return;}
        if(!data.empty()){
            const auto&a=data.front().first;const auto&b=data.back().first;std::size_t n=0;
            while(n<std::min(a.size(),b.size())&&a[n]==b[n])++n;
            prefix_=a.substr(0,n);
        }
        suffixes_.reserve(data.size());
        for(const auto&kv:data)suffixes_.emplace_back(kv.first.substr(prefix_.size()),kv.second);
    }
    int candidate_id()const{return choice_.id;}
    bool accepts_prefix(std::string_view k)const{
        if(learned_)return learned_->accepts_prefix(k);
        return k.size()>=prefix_.size()&&(!prefix_.size()||std::memcmp(k.data(),prefix_.data(),prefix_.size())==0);
    }
    Result find(std::string_view k)const{
        if(learned_)return learned_->find(k,query_mode);
        if(!accepts_prefix(k))return {};
        const auto s=k.substr(prefix_.size());const auto it=lower(s);
        if(it==suffixes_.end()||compare(it->first,s))return {};
        return {true,it->second};
    }
    bool insert(std::string_view k,std::uint64_t v){
        if(learned_)return learned_->insert(k,v);
        if(!accepts_prefix(k))throw std::invalid_argument("sorted prefix requires rebuild");
        auto s=k.substr(prefix_.size());auto it=lower(s);
        if(it!=suffixes_.end()&&!compare(it->first,s))return false;
        suffixes_.insert(it,{std::string(s),v});return true;
    }
    bool update(std::string_view k,std::uint64_t v){
        if(learned_)return learned_->update(k,v);
        if(!accepts_prefix(k))return false;
        auto s=k.substr(prefix_.size());auto it=lower(s);
        if(it==suffixes_.end()||compare(it->first,s))return false;
        it->second=v;return true;
    }
    bool erase(std::string_view k){
        if(learned_)return learned_->erase(k);
        if(!accepts_prefix(k))return false;
        auto s=k.substr(prefix_.size());auto it=lower(s);
        if(it==suffixes_.end()||compare(it->first,s))return false;
        suffixes_.erase(it);return true;
    }
    void scan(std::string_view start,std::size_t limit,std::vector<Pair>&out)const{
        if(learned_){learned_->scan(start,limit,out);return;}
        if(out.size()>=limit)return;
        const bool matches=accepts_prefix(start);
        if(!matches&&compare(start,prefix_)>0)return;
        auto it=matches?lower(start.substr(prefix_.size())):suffixes_.begin();
        for(;it!=suffixes_.end()&&out.size()<limit;++it)out.emplace_back(prefix_+it->first,it->second);
    }
    std::vector<Pair> all()const{std::vector<Pair> v;v.reserve(size());scan({},size(),v);return v;}
    std::size_t size()const{return learned_?learned_->size():suffixes_.size();}
    std::size_t prefix_length()const{return learned_?learned_->prefix_length():prefix_.size();}
    std::size_t memory()const{
        std::size_t n=sizeof(*this)+prefix_.capacity()+1+suffixes_.capacity()*sizeof(Pair);
        if(learned_)n+=learned_->memory();
        else for(const auto&kv:suffixes_)n+=kv.first.capacity()+1;
        return n;
    }
    std::size_t overflow_count()const{return learned_?learned_->overflow_count():0;}
    std::vector<GroupStats> group_stats()const{return learned_?learned_->group_stats():std::vector<GroupStats>{};}
};

class SelectedIndex {
    struct Node {std::string fence;std::unique_ptr<SelectedLeaf> leaf;};
    std::vector<Node> nodes_;
    std::size_t region_keys_=2048,size_=0;
    void replace(std::size_t i,const std::vector<Pair>&data,int candidate){
        if(data.size()>2*region_keys_){
            const auto mid=data.size()/2;
            std::vector<Pair> a(data.begin(),data.begin()+mid),b(data.begin()+mid,data.end());
            auto left=std::make_unique<SelectedLeaf>(a,choice(candidate));
            auto right=std::make_unique<SelectedLeaf>(b,choice(candidate));
            const auto fence=b.front().first;
            nodes_[i].leaf=std::move(left);
            nodes_.insert(nodes_.begin()+i+1,Node{fence,std::move(right)});++splits;
        }else nodes_[i].leaf=std::make_unique<SelectedLeaf>(data,choice(candidate));
        ++rebuilds;
    }
public:
    std::size_t rebuilds=0,splits=0;
    void bulk_load(const std::vector<Pair>& data,const std::vector<int>&choices,std::size_t r=2048){
        if(!r)throw std::invalid_argument("region size must be positive");
        region_keys_=r;size_=data.size();nodes_.clear();
        const auto regions=std::max<std::size_t>(1,(data.size()+r-1)/r);
        if(choices.size()!=regions)throw std::invalid_argument("choices length");
        nodes_.reserve(regions);
        for(std::size_t i=0;i<regions;++i){
            const auto begin=std::min(i*r,data.size()),end=std::min(begin+r,data.size());
            std::vector<Pair> part(data.begin()+begin,data.begin()+end);
            nodes_.push_back({part.empty()?std::string{}:part.front().first,std::make_unique<SelectedLeaf>(part,choice(choices[i]))});
        }
    }
    std::size_t locate(std::string_view key)const{
        if(nodes_.empty())throw std::logic_error("bulk_load required");
        auto it=std::upper_bound(nodes_.begin(),nodes_.end(),key,[](std::string_view k,const Node& n){return compare(k,n.fence)<0;});
        return it==nodes_.begin()?0:static_cast<std::size_t>(it-nodes_.begin()-1);
    }
    const SelectedLeaf&leaf(std::size_t i)const{return *nodes_.at(i).leaf;}
    Result find(std::string_view key)const{return leaf(locate(key)).find(key);}
    std::vector<Pair> scan(std::string_view start,std::size_t n)const{
        std::vector<Pair> out;out.reserve(std::min(n,size_));
        for(std::size_t i=locate(start);i<nodes_.size()&&out.size()<n;++i)nodes_[i].leaf->scan(start,n,out);
        return out;
    }
    bool insert(std::string_view key,std::uint64_t value){
        const auto i=locate(key);auto&l=*nodes_[i].leaf;const auto candidate=l.candidate_id();
        if(!l.accepts_prefix(key)){
            auto data=l.all();auto it=std::lower_bound(data.begin(),data.end(),key,[](const Pair&a,std::string_view b){return compare(a.first,b)<0;});
            if(it!=data.end()&&!compare(it->first,key))return false;
            data.insert(it,{std::string(key),value});replace(i,data,candidate);++size_;return true;
        }
        if(!l.insert(key,value))return false;
        ++size_;if(l.size()>2*region_keys_)replace(i,l.all(),candidate);return true;
    }
    bool update(std::string_view k,std::uint64_t v){return nodes_[locate(k)].leaf->update(k,v);}
    bool erase(std::string_view k){const auto ok=nodes_[locate(k)].leaf->erase(k);if(ok)--size_;return ok;}
    bool upsert(std::string_view k,std::uint64_t v){if(update(k,v))return false;return insert(k,v);}
    void reconfigure(std::size_t i,int candidate){replace(i,leaf(i).all(),candidate);}
    std::size_t size()const{return size_;}
    std::size_t region_count()const{return nodes_.size();}
    std::size_t payload_memory()const{std::size_t n=0;for(const auto&x:nodes_)n+=x.leaf->memory();return n;}
    std::size_t memory()const{std::size_t n=sizeof(*this)+nodes_.capacity()*sizeof(Node)+payload_memory();for(const auto&x:nodes_)n+=x.fence.capacity()+1;return n;}
    std::size_t overflow_count()const{std::size_t n=0;for(const auto&x:nodes_)n+=x.leaf->overflow_count();return n;}
};

struct Metrics {
    std::size_t bytes=0,overflow=0;
    double tune_ns=0,build_ns=0;
};
using Matrix=std::vector<std::array<Metrics,CANDIDATES>>;
using Allowed=std::vector<std::vector<int>>;
inline std::size_t selected_bytes(const Matrix&m,const std::vector<int>&s){
    std::size_t n=0;for(std::size_t i=0;i<m.size();++i)n+=m[i][s[i]].bytes;return n;
}
inline double selected_cost(const Matrix&m,const std::vector<double>&w,const std::vector<int>&s){
    double n=0;for(std::size_t i=0;i<m.size();++i)n+=w[i]*m[i][s[i]].tune_ns;return n;
}
// Discrete Lagrangian search: returns a feasible selection and keeps the supplied
// feasible incumbent. It is an approximation, not an exact knapsack solver.
inline std::vector<int> select(const Matrix&m,const std::vector<double>&w,const Allowed&a,std::size_t budget,std::vector<int> incumbent){
    if(m.size()!=w.size()||m.size()!=a.size()||m.size()!=incumbent.size())throw std::invalid_argument("selector shape");
    for(std::size_t i=0;i<m.size();++i)
        if(a[i].empty()||std::find(a[i].begin(),a[i].end(),incumbent[i])==a[i].end())throw std::invalid_argument("incumbent not allowed");
    if(selected_bytes(m,incumbent)>budget)throw std::invalid_argument("infeasible incumbent");
    double best=selected_cost(m,w,incumbent);
    auto at=[&](double lambda){
        std::vector<int> s(m.size());
        for(std::size_t i=0;i<m.size();++i){
            s[i]=a[i][0];
            auto cost=[&](int c){return w[i]*m[i][c].tune_ns+lambda*m[i][c].bytes;};
            for(int c:a[i])if(cost(c)<cost(s[i])||(cost(c)==cost(s[i])&&m[i][c].bytes<m[i][s[i]].bytes))s[i]=c;
        }
        if(selected_bytes(m,s)<=budget){const auto c=selected_cost(m,w,s);if(c<best){best=c;incumbent=s;}}
        return s;
    };
    auto initial=at(0);if(selected_bytes(m,initial)<=budget)return incumbent;
    double lo=0,hi=1e-6;
    while(selected_bytes(m,at(hi))>budget){hi*=2;if(hi>1e12)throw std::runtime_error("selector failed to bracket budget");}
    for(int it=0;it<60;++it){const auto mid=(lo+hi)/2;if(selected_bytes(m,at(mid))>budget)lo=mid;else hi=mid;}
    return incumbent;
}
}

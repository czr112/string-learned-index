// Bounded Q entry diagnosis. Counter build only, no performance measurements.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main
#ifndef E12_COUNT
#error Q entry requires E12_COUNT
#endif

static int unsigned_compare(std::string_view a, std::string_view b) {
    for (std::size_t i=0;i<std::min(a.size(),b.size());++i) {
        const auto x=static_cast<unsigned char>(a[i]),y=static_cast<unsigned char>(b[i]);
        if(x!=y) return x<y?-1:1;
    }
    return a.size()==b.size()?0:(a.size()<b.size()?-1:1);
}
static std::uint64_t summary8(std::string_view s) {
    std::uint64_t v=0;
    for(std::size_t i=0;i<8;++i) v=(v<<8)|(i<s.size()?static_cast<unsigned char>(s[i]):0);
    return v;
}
using Boundaries=std::vector<std::pair<std::string,std::uint32_t>>;
struct Work { std::uint64_t probes=0, fallback=0; };
static std::size_t route(const Boundaries& cold,const std::vector<std::uint64_t>& hot,
                         std::string_view query,Work& work) {
    if(cold.size()!=hot.size()) throw std::runtime_error("hot/cold shape mismatch");
    const auto digest=summary8(query);std::size_t lo=0,hi=hot.size();
    while(lo<hi) {
        const auto mid=lo+(hi-lo)/2;++work.probes;
        bool before;
        if(digest==hot[mid]) {++work.fallback;before=unsigned_compare(query,cold[mid].first)<0;}
        else before=digest<hot[mid];
        if(before) hi=mid;else lo=mid+1;
    }
    return lo?cold[lo-1].second:SIZE_MAX;
}
static std::size_t reference(const Boundaries& b,std::string_view q) {
    const auto it=std::upper_bound(b.begin(),b.end(),q,
        [](std::string_view key,const auto& boundary){return unsigned_compare(key,boundary.first)<0;});
    return it==b.begin()?SIZE_MAX:std::size_t((it-1)->second);
}
static std::uint64_t edge_gate() {
    const std::vector<std::vector<std::string>> cases={
        {},{""},{"","a",std::string("a\0",2),std::string("a\0\0",3),std::string("a\0b",3)},
        {std::string(1,char(128)),std::string(1,char(255))},
        {"abcdefgh1","abcdefgh2","abcdefghzz"},
        {std::string(4096,'a'),std::string(4096,'a')+"b"}};
    std::uint64_t checked=0;
    for(const auto& keys:cases) {
        Boundaries b;std::vector<std::uint64_t> h;
        for(std::size_t i=0;i<keys.size();++i){b.emplace_back(keys[i],std::uint32_t(i*2+1));h.push_back(summary8(keys[i]));}
        std::vector<std::string> probes={"",std::string(1,char(255)),"abcdefgh"};
        for(const auto& key:keys) {
            probes.push_back(key);probes.push_back(key+std::string(1,'\0'));probes.push_back(key+"z");
            if(!key.empty()) probes.push_back(key.substr(0,key.size()-1));
        }
        for(const auto& key:probes){Work w;if(route(b,h,key,w)!=reference(b,key))throw std::runtime_error("binary edge route");++checked;}
    }
    return checked;
}
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows queries context16|lits");
        const bool lits=std::string(argv[3])=="lits";
        if(!lits&&std::string(argv[3])!="context16")throw std::invalid_argument("backend");
        auto rin=buffered_input(argv[1]),qin=buffered_input(argv[2]);
        const auto rows=unified::read_rows(rin);const auto queries=unified::read_queries(qin);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty input");
        auto cfg=e25sw::baseline_config(128);e25sw::pin_frozen_switches(cfg);
        cfg.model_backend=lits?ModelBackend::LitsHpt:ModelBackend::Context16;cfg.compact_region_routing=true;
        if(!cfg.compact_plan_commit||cfg.subblock_reclaim||cfg.reclaim_widest||cfg.primary_hole_reuse)
            throw std::runtime_error("configuration drift");
        Index index(cfg);std::vector<Pair> pairs;for(const auto& row:rows)pairs.emplace_back(row.key,row.value);
        index.bulk_load(pairs);std::vector<Pair>().swap(pairs);
        std::vector<std::vector<Boundaries>> cold(index.region_count());
        std::vector<std::vector<std::vector<std::uint64_t>>> hot(index.region_count());
        std::uint64_t boundaries=0,groups=0,nonempty_dirs=0,deep_queries=0,cold_key_bytes=0;
        for(std::size_t r=0;r<index.region_count();++r) {
            const auto& reg=index.region(r);const auto n=reg.slot_table().size();groups+=n;
            cold[r].resize(n);hot[r].resize(n);
            for(std::uint32_t g=0;g<n;++g) {
                cold[r][g]=reg.boundary_dir(g);boundaries+=cold[r][g].size();nonempty_dirs+=!cold[r][g].empty();
                for(const auto& b:cold[r][g]){hot[r][g].push_back(summary8(b.first));cold_key_bytes+=b.first.size();}
            }
        }
        Aggregate seq,ordinary;Work routing;std::uint64_t checksum=0,checked=0;
        const auto check_route=[&](std::string_view key, bool account) {
            const auto r=index.locate(key);const auto& reg=index.region(r);const auto g=reg.diag_group(key);
            if(g==UINT32_MAX)return;
            Work w;const auto got=route(cold[r][g],hot[r][g],reg.residual(key),w);
            if(got!=reference(cold[r][g],reg.residual(key)))throw std::runtime_error("directory route mismatch");
            ++checked;if(account){routing.probes+=w.probes;routing.fallback+=w.fallback;deep_queries+=reg.subblock_count(g)>1;}
        };
        for(const auto& row:rows)check_route(row.key,false);
        for(const auto& q:queries) {
            check_route(q.key,true);const auto a=index.find(q.key,mode(false)),b=index.find(q.key,mode(true));
            check(a,q);check(b,q);seq.add(a);ordinary.add(b);checksum=checksum*131+token(a);
        }
        // Planned flat representation: one uint64 summary per boundary and uint32 start per group;
        // include terminal starts and both vector objects per Region. Cold directory is retained.
        const auto extra=boundaries*8+(groups+index.region_count())*4+index.region_count()*2*sizeof(std::vector<std::uint64_t>);
        std::cout<<"{\"backend\":\""<<argv[3]<<"\",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"index_bytes\":"<<index.memory()<<",\"regions\":"<<index.region_count()<<",\"groups\":"<<groups
            <<",\"boundaries\":"<<boundaries<<",\"nonempty_directories\":"<<nonempty_dirs
            <<",\"deep_queries\":"<<deep_queries<<",\"routing_probes\":"<<routing.probes
            <<",\"full_fallbacks\":"<<routing.fallback<<",\"planned_hot_bytes\":"<<extra
            <<",\"diagnostic_cold_key_bytes\":"<<cold_key_bytes<<",\"route_checks\":"<<checked
            <<",\"edge_checks\":"<<edge_gate()<<",\"checksum\":"<<checksum<<",";
        seq.emit("sequential");std::cout<<',';ordinary.emit("ordinary_directory");std::cout<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

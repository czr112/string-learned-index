// One immutable index, C enabled. S0/S1 are repeats of the SAME sequential query.
// Ordinary directory and compact Q both use the real route/model on every query.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main

static Mode qmode(int arm) {
    auto m=mode(arm>=2);m.q_static=arm==3;return m;
}
static std::uint64_t validate(Index& index,const std::vector<string_index_bench::v1::Row>& rows,
                              const std::vector<unified::Query>& queries) {
    std::uint64_t checked=0;
    for(const auto& row:rows) {
        const auto a=index.find(row.key,qmode(0)),b=index.find(row.key,qmode(3));
        if(!a.found||!b.found||a.value!=row.value||b.value!=row.value)
            throw std::runtime_error("full row oracle differs");
        ++checked;
    }
    for(const auto& q:queries)for(int a=0;a<4;++a){check(index.find(q.key,qmode(a)),q);++checked;}
    return checked;
}
static std::uint64_t binary_gate() {
    auto cfg=e25sw::baseline_config(128);e25sw::pin_frozen_switches(cfg);
    cfg.region_keys=2048;cfg.compact_region_routing=true;
    // Force a single coarse group with many primary blocks to exercise collisions,
    // gaps and a short-prefix query. Context16 is the backend for binary-domain keys.
    std::vector<Pair> pairs;
    for(std::size_t i=0;i<320;++i) {
        std::string k="abcdefgh"+std::string(64,'x');
        k.push_back(char(i/256));k.push_back(char(i%256));pairs.emplace_back(k,i+1);
    }
    pairs.emplace_back("",10000);pairs.emplace_back(std::string("a\0",2),10001);
    pairs.emplace_back(std::string("a\0\0",3),10002);pairs.emplace_back(std::string(1,char(128)),10003);
    pairs.emplace_back(std::string(1,char(255)),10004);pairs.emplace_back(std::string(4096,'z'),10005);
    std::sort(pairs.begin(),pairs.end(),[](const Pair& a,const Pair& b){return compare(a.first,b.first)<0;});
    Index index(cfg);index.bulk_load(pairs);
    for(std::size_t r=0;r<index.region_count();++r)index.region(r).prepare_q_static();
    std::uint64_t checked=0;
    for(const auto& kv:pairs) {
        for(const auto& key:{kv.first,kv.first+std::string(1,'\0'),kv.first+std::string(1,char(255))}) {
            const auto it=std::lower_bound(pairs.begin(),pairs.end(),key,
                [](const Pair& a,std::string_view b){return compare(a.first,b)<0;});
            const bool found=it!=pairs.end()&&compare(it->first,key)==0;
            for(int a=0;a<4;++a) {
                const auto got=index.find(key,qmode(a));
                if(got.found!=found||(found&&got.value!=it->second))throw std::runtime_error("binary oracle mismatch");
                ++checked;
            }
        }
    }
    return checked;
}
int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::invalid_argument("rows queries backend repeats");
        const bool lits=std::string(argv[3])=="lits";
        if(!lits&&std::string(argv[3])!="context16")throw std::invalid_argument("backend");
        const int repeats=std::stoi(argv[4]);if(repeats<0||repeats%4||repeats>20)throw std::invalid_argument("balanced repeats");
        auto rin=buffered_input(argv[1]),qin=buffered_input(argv[2]);
        const auto rows=unified::read_rows(rin);const auto queries=unified::read_queries(qin);
        auto cfg=e25sw::baseline_config(128);e25sw::pin_frozen_switches(cfg);
        cfg.model_backend=lits?ModelBackend::LitsHpt:ModelBackend::Context16;cfg.compact_region_routing=true;
        Index index(cfg);std::vector<Pair> pairs;for(const auto& row:rows)pairs.emplace_back(row.key,row.value);
        index.bulk_load(pairs);std::vector<Pair>().swap(pairs);
        // Warm the lazy outer C directory before accounting and prepare Q from the live index.
        index.locate(queries.front().key);
        const auto base_bytes=index.memory()-index.region_count()*2*sizeof(std::vector<std::uint64_t>);
        std::size_t extra=0;
        for(std::size_t r=0;r<index.region_count();++r){index.region(r).prepare_q_static();extra+=index.region(r).q_extra_bytes();}
        const auto checked=validate(index,rows,queries),edge_checks=binary_gate();
        std::cout<<"{\"phase\":\"gate\",\"queries\":"<<queries.size()<<",\"row_query_checks\":"<<checked
            <<",\"edge_checks\":"<<edge_checks<<",\"extra_bytes\":"<<extra<<",\"baseline_bytes\":"<<base_bytes<<"}\n";
        for(int arm=0;arm<4;++arm)for(const auto& q:queries){const auto r=index.find(q.key,qmode(arm));check(r,q);sink=token(r);}
        for(int rep=0;rep<repeats;++rep)for(int pos=0;pos<4;++pos) {
            const int arm=(rep+pos)%4;std::uint64_t sum=0;
            const auto start=std::chrono::steady_clock::now();
            for(int pass=0;pass<100;++pass)for(const auto& q:queries){const auto r=index.find(q.key,qmode(arm));sum=sum*131+token(r);}
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();sink=sum;
            std::cout<<"{\"phase\":\"paired\",\"repeat\":"<<rep<<",\"position\":"<<pos<<",\"arm\":"<<arm
                <<",\"operations\":"<<100*queries.size()<<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<sum<<"}\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

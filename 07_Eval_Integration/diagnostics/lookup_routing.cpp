// Reuse the EXACT input parsing, configuration and check helpers from first diagnosis.
#define main lookup_work_diagnostic_main
#include "lookup_structure.cpp"
#undef main
#include "compact_fence_router.h"

int main(int argc,char** argv) {
    try {
        if(argc!=5) throw std::invalid_argument("rows queries backend repeats");
        auto row_input=buffered_input(argv[1]),query_input=buffered_input(argv[2]);
        const auto rows=unified::read_rows(row_input); const auto queries=unified::read_queries(query_input);
        const bool lits=std::string(argv[3])=="lits";
        if(!lits && std::string(argv[3])!="context16") throw std::invalid_argument("backend");
        const int repeats=std::stoi(argv[4]);
        if(repeats<0 || repeats>20 || rows.empty() || queries.empty()) throw std::invalid_argument("input");
        Config cfg=e25sw::baseline_config(128);e25sw::pin_frozen_switches(cfg);
        cfg.model_backend=lits?ModelBackend::LitsHpt:ModelBackend::Context16;
        Index index(cfg);std::vector<Pair> pairs; pairs.reserve(rows.size());
        for(const auto& r:rows) pairs.emplace_back(r.key,r.value);
        index.bulk_load(pairs);std::vector<Pair>().swap(pairs);
        std::vector<std::string_view> fences;
        for(std::size_t i=0;i<rows.size();i+=cfg.region_keys) fences.push_back(rows[i].key);
        CompactFenceRouter router(fences);
        if(fences.size()!=index.region_count()) throw std::runtime_error("region geometry differs");
        std::uint64_t route_checks=0;
        const auto check_route=[&](std::string_view key){
            ++route_checks;
            if(router.locate(key)!=index.locate(key)) throw std::runtime_error("compact routing differs");
        };
        for(const auto& row:rows) check_route(row.key);
        for(const auto& q:queries) {
            check_route(q.key);
            check(index.find_known(router.locate(q.key),q.key,mode(false)),q);
        }
        // Fence equality, shorter-prefix, appended NUL/high-byte and outside-domain
        // keys exercise routing only; they do not call the LITS prediction backend.
        check_route("");check_route(std::string(8,static_cast<char>(255)));
        for(const auto f:fences) {
            check_route(f);
            if(!f.empty()) check_route(f.substr(0,f.size()-1));
            for(unsigned byte:{0u,1u,127u,128u,255u}) {
                std::string key(f);key.push_back(static_cast<char>(byte));check_route(key);
            }
        }
        // Independent collision/domain cases, not just the three ASCII corpora.
        const std::vector<std::vector<std::string>> cases={
            {"","a",std::string("a\0",2),std::string("a\0b",3),"abcdefgh1","abcdefgh2"},
            {"commonprefix000","commonprefix001","commonprefix999"},
            {std::string(1,char(128)),std::string(1,char(255))}};
        for(const auto& keys:cases) {
            std::vector<std::string_view> views;for(const auto& k:keys) views.push_back(k);
            CompactFenceRouter test(views);
            std::vector<std::string> probes=keys;probes.push_back("");
            for(const auto& k:keys) {probes.push_back(k+std::string(1,'\0'));probes.push_back(k+"suffix");}
            for(const auto& k:probes) {
                const auto it=std::upper_bound(views.begin(),views.end(),std::string_view(k),
                    [](auto q,auto fence){return q.compare(fence)<0;});
                const auto expected=it==views.begin()?0:std::size_t(it-views.begin()-1);
                if(test.locate(k)!=expected) throw std::runtime_error("edge routing differs");
                ++route_checks;
            }
        }
        std::cout << "{\"phase\":\"gate\",\"route_checks\":" << route_checks
            << ",\"queries_checked\":" << queries.size() << ",\"auxiliary_bytes\":" << router.auxiliary_bytes()
            << ",\"global_prefix_bytes\":" << router.prefix_bytes() << "}\n";
        for(int variant=0;variant<2;++variant)
            for(const auto& q:queries) {
                const auto r=variant?index.find_known(router.locate(q.key),q.key,mode(false)):index.find(q.key,mode(false));
                check(r,q);sink=token(r);
            }
        for(int rep=0;rep<repeats;++rep)
            for(int position=0;position<2;++position) {
                const int variant=(rep+position)%2;std::uint64_t sum=0;
                const auto start=std::chrono::steady_clock::now();
                for(int pass=0;pass<100;++pass)
                    for(const auto& q:queries) {
                        const auto r=variant?index.find_known(router.locate(q.key),q.key,mode(false)):index.find(q.key,mode(false));
                        sum=sum*131+token(r);
                    }
                const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();sink=sum;
                std::cout << "{\"phase\":\"paired\",\"repeat\":" << rep << ",\"position\":" << position
                    << ",\"variant\":" << variant << ",\"operations\":" << 100*queries.size()
                    << ",\"elapsed_ns\":" << elapsed << ",\"checksum\":" << sum << "}\n";
            }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// C2 entry driver: (a) a per-query work ledger over the complete D1 query path and
// (b) a phase decomposition that times navigation, window selection and verification
// separately on the same object. The ledger copy must answer exactly like the frozen D1 core.
#include "shortkey_entry_index.hpp"
#include "collision_refined_index.hpp"
#include "../adapters/art/art_adapter.h"
#include "../adapters/hot/hot_adapter.h"
#include "static_input.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;

static volatile std::uint64_t sink=0;
static std::uint64_t token(std::optional<std::uint64_t> r) {
    return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
static std::uint64_t token_index(std::uint64_t x) {
    x^=x>>33;x*=UINT64_C(0xff51afd7ed558ccd);x^=x>>33;x*=UINT64_C(0xc4ceb9fe1a85ec53);x^=x>>33;return x;
}
using Clock=std::chrono::steady_clock;
static std::vector<std::uint32_t> permutation(std::size_t n,std::uint64_t repeat) {
    std::vector<std::uint32_t> p(n);std::iota(p.begin(),p.end(),std::uint32_t(0));
    std::mt19937_64 rng(UINT64_C(0x9e3779b97f4a7c15)^(repeat*UINT64_C(0x100000001b3))^std::uint64_t(n));
    std::shuffle(p.begin(),p.end(),rng);return p;
}
struct Stats {double mean=0;double median=0;double p95=0;std::uint64_t max=0,total=0;};
template<class T> static Stats stats(std::vector<T> values) {
    Stats s;if(values.empty())return s;
    for(const auto v:values)s.total+=std::uint64_t(v);
    s.mean=double(s.total)/double(values.size());
    std::sort(values.begin(),values.end());
    s.median=double(values[values.size()/2]);
    s.p95=double(values[std::size_t(double(values.size()-1)*0.95)]);
    s.max=std::uint64_t(values.back());return s;
}
static void number(const std::optional<std::uint64_t>& n) {if(n)std::cout<<*n;else std::cout<<"null";}
static void print_stats(const char* name,const Stats& s,bool comma=true) {
    std::cout<<(comma?",":"")<<"\""<<name<<"\":{\"mean\":"<<s.mean<<",\"median\":"<<s.median
        <<",\"p95\":"<<s.p95<<",\"max\":"<<s.max<<",\"total\":"<<s.total<<"}";
}

int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::invalid_argument("rows.bin queries.bin mode(ledger|phase) process_offset");
        const std::string mode=argv[3];
        if(mode!="ledger"&&mode!="phase")throw std::invalid_argument("unknown mode");
        const int offset=std::stoi(argv[4]);if(offset<0||offset>4)throw std::invalid_argument("offset");

        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});

        const auto build_start=Clock::now();
        const collision_refined::Index frozen(physical);
        const auto frozen_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-build_start).count());
        const auto copy_start=Clock::now();
        const shortkey_entry::Index copy(physical);
        const auto copy_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-copy_start).count());

        if(mode=="ledger") {
            // Same-source assertion: the diagnostic copy must match the frozen D1 core on
            // answers, locations and certificates for every row and every frozen query.
            std::uint64_t checked=0,locations=0,windows=0;
            auto compare_key=[&](std::string_view key) {
                if(copy.find_model(key)!=frozen.find_model(key))throw std::logic_error("model answer differs");
                if(copy.find_directory(key)!=frozen.find_directory(key))throw std::logic_error("directory answer differs");
                if(copy.find_hybrid(key)!=frozen.find_hybrid(key))throw std::logic_error("hybrid answer differs");
                const auto a=copy.locate_blocks<shortkey_entry::Lookup::learned>(key);
                const auto b=frozen.locate_blocks<collision_refined::Lookup::learned>(key);
                if(int(a.state)!=int(b.state))throw std::logic_error("state differs");
                if(a.region!=b.region||a.begin!=b.begin||a.end!=b.end||a.prefix_verified!=b.prefix_verified)
                    throw std::logic_error("location differs");
                ++locations;
                const auto w=copy.locate_window(key);
                if(w.region!=b.region||w.begin!=b.begin||w.end!=b.end)throw std::logic_error("window differs");
                ++windows;++checked;
            };
            for(const auto& r:rows)compare_key(r.key);
            for(const auto& q:queries)compare_key(q.key);
            const auto frozen_cert=frozen.verify_numeric_certificate();
            const auto copy_cert=copy.verify_numeric_certificate();
            if(frozen_cert!=copy_cert)throw std::logic_error("certificate count differs");
            for(const auto& q:queries)copy.check_window(q.key);

            // One counted pass over the frozen queries, per-query counters retained.
            std::vector<shortkey_entry::Work> works(queries.size());
            for(std::size_t i=0;i<queries.size();++i)copy.find_model(queries[i].key,&works[i]);
            auto field=[&](auto member) {
                std::vector<std::uint64_t> v;v.reserve(works.size());
                for(const auto& w:works)v.push_back(std::uint64_t(w.*member));
                return stats(v);
            };
            std::cout<<"{\"phase\":\"ledger\",\"queries\":"<<queries.size()<<",\"rows\":"<<rows.size()
                <<",\"checked_keys\":"<<checked<<",\"locations\":"<<locations<<",\"windows\":"<<windows
                <<",\"certificate_probes\":"<<copy_cert<<",\"counter\":{";
            print_stats("fence_steps",field(&shortkey_entry::Work::fence_steps),false);
            print_stats("fence_full_compares",field(&shortkey_entry::Work::fence_full_compares));
            print_stats("fence_bytes",field(&shortkey_entry::Work::fence_bytes));
            print_stats("region_prefix_bytes",field(&shortkey_entry::Work::region_prefix_bytes));
            print_stats("coordinate_builds",field(&shortkey_entry::Work::coordinate_builds));
            print_stats("refine_lookups",field(&shortkey_entry::Work::refine_lookups));
            print_stats("family_steps",field(&shortkey_entry::Work::family_steps));
            print_stats("local_steps",field(&shortkey_entry::Work::local_steps));
            print_stats("model_steps",field(&shortkey_entry::Work::model_steps));
            print_stats("window_blocks",field(&shortkey_entry::Work::window_blocks));
            print_stats("directory_steps",field(&shortkey_entry::Work::directory_steps));
            print_stats("fallback",field(&shortkey_entry::Work::fallback));
            print_stats("rejected",field(&shortkey_entry::Work::rejected));
            print_stats("tag_block_loads",field(&shortkey_entry::Work::tag_block_loads));
            print_stats("tag_candidates",field(&shortkey_entry::Work::tag_candidates));
            print_stats("length_rejects",field(&shortkey_entry::Work::length_rejects));
            print_stats("exact",field(&shortkey_entry::Work::exact));
            print_stats("crc_bytes",field(&shortkey_entry::Work::crc_bytes));
            print_stats("exact_bytes",field(&shortkey_entry::Work::exact_bytes));
            print_stats("blocks_entered",field(&shortkey_entry::Work::blocks),true);
            std::cout<<"},\"build_ns\":{\"frozen\":"<<frozen_ns<<",\"copy\":"<<copy_ns<<"}}\n";
            return 0;
        }

        // phase mode: four readings of the same object plus two external totals.
        const sib::Factory ext[]={make_art_index,make_hot_index};
        std::unique_ptr<sib::Index> art,hot;
        std::uint64_t art_ns{},hot_ns{};
        {
            const auto start=Clock::now();art=ext[0]({});art->bulk_load(rows);
            art_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
        }
        {
            const auto start=Clock::now();hot=ext[1]({});hot->bulk_load(rows);
            hot_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
        }
        const char* names[]={"region","window","window_AA","full","full_AA","directory","ART","HOT"};
        // Balanced rotation over eight arms; window_AA/full_AA measure the same call twice on
        // the same object, which is the noise band for the subtraction the phases rely on.
        constexpr int williams[]={0,1,7,2,6,3,5,4};
        auto pass=[&](int arm,const std::vector<std::uint32_t>& order) {
            std::uint64_t c=0;
            switch(arm) {
                case 0:for(const auto i:order)c=c*131+token_index(copy.locate_region(queries[i].key));break;
                case 1:case 2:for(const auto i:order){const auto w=copy.locate_window(queries[i].key);
                        c=c*131+token_index(std::uint64_t(w.region)*131+std::uint64_t(w.begin)*17+w.end);}break;
                case 3:case 4:for(const auto i:order)c=c*131+token(copy.find_model(queries[i].key));break;
                case 5:for(const auto i:order)c=c*131+token(copy.find_directory(queries[i].key));break;
                case 6:for(const auto i:order)c=c*131+token(art->find(queries[i].key));break;
                default:for(const auto i:order)c=c*131+token(hot->find(queries[i].key));break;
            }
            return c;
        };
        for(int arm=0;arm<8;++arm)pass(arm,permutation(queries.size(),0));sink=1;
        std::cout<<"{\"phase\":\"gate\",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"build_ns\":{\"copy\":"<<copy_ns<<",\"frozen\":"<<frozen_ns
            <<",\"ART\":"<<art_ns<<",\"HOT\":"<<hot_ns<<"}}\n";
        for(std::size_t repeat=0;repeat<6;++repeat) {
            const auto order=permutation(queries.size(),repeat);
            for(int position=0;position<8;++position) {
                const auto arm=(williams[position]+int(repeat)+offset)%8;
                const auto start=Clock::now();
                const auto checksum=pass(arm,order);
                const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                sink=checksum;
                std::cout<<"{\"phase\":\"pass\",\"mode\":\"phase\",\"repeat\":"<<repeat
                    <<",\"position\":"<<position<<",\"arm\":\""<<names[arm]<<"\",\"operations\":"<<queries.size()
                    <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

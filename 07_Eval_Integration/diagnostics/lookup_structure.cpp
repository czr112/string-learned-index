// Replay matrix2's exact interchange on the same construction/config as our adapter.
// E12_COUNT/E25_DIAG builds report logical work, never performance conclusions.
// Clean builds compare existing traversal policies on ONE immutable structure.
#include "index.hpp"
#include "switches.hpp"
#include "unified/runner.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;
namespace unified = string_index_bench::unified;

struct Aggregate {
    std::uint64_t queries=0, steps=0, blocks=0, batches=0, tags=0;
    std::uint64_t primary_cmps=0, overflow_cmps=0, overflow_entered=0, overflow_checks=0;
    std::uint64_t directory_probes=0, directory_bytes=0;
    void add(const Result& r) {
        ++queries; steps+=r.steps; blocks+=r.subblocks_visited; batches+=r.simd_batches;
        tags+=r.tag_candidates; primary_cmps+=r.str_cmps_primary;
        overflow_cmps+=r.str_cmps_overflow; overflow_entered+=r.overflow_entered;
        overflow_checks+=r.overflow_checks; directory_probes+=r.locate_probes;
        directory_bytes+=r.locate_cmp_bytes;
    }
    void emit(const char* name) const {
        std::cout << "\"" << name << "\":{\"queries\":" << queries
            << ",\"model_steps\":" << steps << ",\"primary_blocks\":" << blocks
            << ",\"simd_batches\":" << batches << ",\"tag_candidates\":" << tags
            << ",\"primary_comparisons\":" << primary_cmps
            << ",\"overflow_comparisons\":" << overflow_cmps
            << ",\"overflow_entered\":" << overflow_entered
            << ",\"overflow_checks\":" << overflow_checks
            << ",\"directory_probes\":" << directory_probes
            << ",\"directory_compare_bytes\":" << directory_bytes << "}";
    }
};

static Mode mode(bool locate) {
    Mode m; m.route=Route::Interval; m.prefetch=Prefetch::Off;
    m.fingerprint=true; m.simd=true; m.ov=OvSearch::Hybrid; m.ov_thr=128;
    m.locate=locate; return m;
}

static void check(const Result& r, const unified::Query& q) {
    if (r.found != q.expected_present || (r.found && r.value != q.expected_value))
        throw std::runtime_error("lookup result differs from interchange oracle");
}

static std::uint64_t token(const Result& r) {
    return r.found ? r.value ^ UINT64_C(0x9e3779b97f4a7c15) : UINT64_C(0xd1b54a32d192ed03);
}

static volatile std::uint64_t sink=0;

static std::istringstream buffered_input(const char* path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("cannot read interchange");
    std::string bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("failed to read interchange");
    return std::istringstream(std::move(bytes),std::ios::binary);
}

int main(int argc, char** argv) {
    try {
        if (argc!=5) throw std::invalid_argument("rows.bin queries.bin context16|lits repeats");
        auto row_input=buffered_input(argv[1]), query_input=buffered_input(argv[2]);
        const auto rows=unified::read_rows(row_input);
        const auto queries=unified::read_queries(query_input);
        if (queries.empty()) throw std::invalid_argument("empty query sequence");
        const bool lits=std::string(argv[3])=="lits";
        if (!lits && std::string(argv[3])!="context16") throw std::invalid_argument("backend");
        const int repeats=std::stoi(argv[4]);
        if (repeats<0 || repeats>20) throw std::invalid_argument("repeats");
        Config cfg=e25sw::baseline_config(128); e25sw::pin_frozen_switches(cfg);
        cfg.model_backend=lits?ModelBackend::LitsHpt:ModelBackend::Context16;
        if (!cfg.compact_plan_commit || cfg.subblock_reclaim || cfg.reclaim_widest)
            throw std::runtime_error("unexpected effective configuration");
        Index index(cfg);
        std::vector<Pair> pairs; pairs.reserve(rows.size());
        for (const auto& r:rows) pairs.emplace_back(r.key,r.value);
        index.bulk_load(pairs); // matches our_index_adapter.cpp, deliberately NOT R1
        std::vector<Pair>().swap(pairs);
        std::vector<std::size_t> routes; routes.reserve(queries.size());
        std::uint64_t prefix_rejected=0, lits_queries=0, query_bytes=0;
        for (const auto& q:queries) {
            const auto region=index.locate(q.key); routes.push_back(region);
            prefix_rejected+=!index.region(region).accepts_prefix(q.key);
            lits_queries+=index.region(region).uses_lits(); query_bytes+=q.key.size();
        }
        std::vector<std::size_t> overflow_sizes;
#ifdef E25_DIAG
        for (std::size_t r=0;r<index.region_count();++r)
            index.region(r).diag_overflow_sizes(overflow_sizes);
#endif
        const auto coverage=index.lits_coverage();
        for (int variant=0;variant<2;++variant) {
            Aggregate by_source[3]; std::uint64_t checksum=0;
#ifdef E25_DIAG
            e25::counts().reset();
#endif
            for (const auto& q:queries) {
                const auto r=index.find(q.key,mode(variant==1)); check(r,q);
                if (r.source>2) throw std::runtime_error("unexpected result source");
                by_source[r.source].add(r); checksum=checksum*131+token(r);
            }
            std::cout << "{\"phase\":\"work\",\"backend\":\"" << argv[3]
                << "\",\"locate\":" << variant << ",\"rows\":" << rows.size()
                << ",\"regions\":" << index.region_count()
                << ",\"overflow_records\":" << index.overflow_count()
#ifdef E25_DIAG
                << ",\"overflow_containers\":" << overflow_sizes.size()
                << ",\"overflow_max\":" << (overflow_sizes.empty()?0:*std::max_element(overflow_sizes.begin(),overflow_sizes.end()))
#endif
                << ",\"prefix_rejected\":" << prefix_rejected << ",\"query_bytes\":" << query_bytes
                << ",\"lits_regions\":" << coverage.regions_lits
                << ",\"lits_queries\":" << lits_queries << ",\"checksum\":" << checksum << ",";
            by_source[0].emit("miss"); std::cout << ','; by_source[1].emit("primary");
            std::cout << ','; by_source[2].emit("overflow");
#ifdef E25_DIAG
            const auto& c=e25::counts();
            std::cout << ",\"outer_locates\":" << c.locate_calls
                << ",\"context16_predictions\":" << c.predict_calls
                << ",\"fingerprint_bytes\":" << c.fp_hash_bytes
                << ",\"full_compare_calls\":" << c.cmp_calls
                << ",\"logical_compare_bytes\":" << c.cmp_bytes;
#endif
            std::cout << "}\n";
        }
#if !defined(E12_COUNT) && !defined(E25_DIAG)
        // Routing bypass is a diagnostic bound, NOT a deployable query optimization.
        // All arms share the same index and query sequence; each first warms once.
        for (int v=0;v<3;++v)
            for (std::size_t i=0;i<queries.size();++i) {
                const auto r=v==2?index.find_known(routes[i],queries[i].key,mode(false))
                                 :index.find(queries[i].key,mode(v==1));
                check(r,queries[i]); sink=token(r);
            }
        constexpr int passes=100;
        for (int rep=0;rep<repeats;++rep) {
            // Rotation covers each of the three positions when repeats is a multiple of 3.
            for (int pos=0;pos<3;++pos) {
                const int variant=(rep+pos)%3; std::uint64_t sum=0;
                const auto start=std::chrono::steady_clock::now();
                for (int pass=0;pass<passes;++pass)
                    for (std::size_t i=0;i<queries.size();++i) {
                        const auto r=variant==2?index.find_known(routes[i],queries[i].key,mode(false))
                                              :index.find(queries[i].key,mode(variant==1));
                        sum=sum*131+token(r);
                    }
                const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()-start).count(); sink=sum;
                std::cout << "{\"phase\":\"paired\",\"backend\":\"" << argv[3]
                    << "\",\"repeat\":" << rep << ",\"position\":" << pos
                    << ",\"variant\":" << variant << ",\"operations\":" << passes*queries.size()
                    << ",\"elapsed_ns\":" << elapsed << ",\"checksum\":" << sum << "}\n";
            }
        }
#else
        if (repeats!=0) throw std::invalid_argument("counted builds must not supply timings");
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

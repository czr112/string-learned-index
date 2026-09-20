// E13 diagnostic: where does range location move the work?
//
// Timing alone cannot show whether S_range trades miss-side traversal for hit-side
// boundary comparisons. This runs the SAME query set through the SAME layout twice
// -- locate off, locate on -- and counts what each path actually executes, split by
// whether the query was a hit or a miss.
//
// Counters come from index.hpp's E12_COUNT block, so this target is built WITH
// -DE12_COUNT while the timing bench is built without it.
//
// The counters are not commensurable: a boundary comparison, a SIMD tag batch and a
// sub-block visit do not cost the same. They are reported side by side to locate
// where the work went, never summed.

#include "data.hpp"
#include "e9_alloc.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

struct Q {
    std::string key;
    bool hit = false;
};

static std::vector<Q> make_queries(const std::vector<Pair>& keys, std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<Q> out;
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto& kv = keys[rng() % keys.size()];
        Q q;
        q.hit = i % 2 == 0;
        q.key = kv.first;
        if (!q.hit) {
            q.key += "~missing~";
            auto it = std::lower_bound(keys.begin(), keys.end(), q.key,
                [](const Pair& a, const std::string& b) { return compare(a.first, b) < 0; });
            if (it != keys.end() && it->first == q.key)
                throw std::runtime_error("negative query exists");
        }
        out.push_back(std::move(q));
    }

    return out;
}

struct Counts {
    std::uint64_t subblocks_visited = 0, candidate_blocks = 0;
    std::uint64_t simd_batches = 0, tag_candidates = 0;
    std::uint64_t str_cmps_primary = 0, str_cmps_overflow = 0;
    std::uint64_t overflow_entered = 0, overflow_probes = 0;
    std::uint64_t locate_probes = 0, locate_cmp_bytes = 0;
    std::uint64_t model_steps = 0, n = 0;

    void add(const Result& r) {
        subblocks_visited += r.subblocks_visited;
        candidate_blocks += r.candidate_blocks;
        simd_batches += r.simd_batches;
        tag_candidates += r.tag_candidates;
        str_cmps_primary += r.str_cmps_primary;
        str_cmps_overflow += r.str_cmps_overflow;
        overflow_entered += r.overflow_entered;
        overflow_probes += r.overflow_checks;
        locate_probes += r.locate_probes;
        locate_cmp_bytes += r.locate_cmp_bytes;
        model_steps += r.steps;
        ++n;
    }
};

static double mean(std::uint64_t total, std::uint64_t n) {
    return n ? double(total) / double(n) : 0.0;
}

static void emit(std::ostream& out, const Counts& c) {
    out << "\"subblocks_visited\":" << mean(c.subblocks_visited, c.n)
        << ",\"candidate_blocks\":" << mean(c.candidate_blocks, c.n)
        << ",\"simd_batches\":" << mean(c.simd_batches, c.n)
        << ",\"tag_candidates\":" << mean(c.tag_candidates, c.n)
        << ",\"str_cmps_primary\":" << mean(c.str_cmps_primary, c.n)
        << ",\"str_cmps_overflow\":" << mean(c.str_cmps_overflow, c.n)
        << ",\"overflow_entered\":" << mean(c.overflow_entered, c.n)
        << ",\"overflow_probes\":" << mean(c.overflow_probes, c.n)
        << ",\"locate_probes\":" << mean(c.locate_probes, c.n)
        << ",\"locate_cmp_bytes\":" << mean(c.locate_cmp_bytes, c.n)
        << ",\"model_steps\":" << mean(c.model_steps, c.n);
}

int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::invalid_argument("diag_e13 KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const auto keys = make_data(kind, nkeys, trcsv);

        const std::string frozen_rel = "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }
        const Mode plain{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};
        Mode range = plain;
        range.locate = true;

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;

        Index probe(c);
        probe.bulk_load(keys);

        std::vector<std::vector<GroupStats>> demand;
        std::vector<std::uint64_t> budget;
        for (std::size_t i = 0; i < probe.region_count(); ++i) {
            demand.push_back(probe.region(i).group_stats());
            budget.push_back(std::uint64_t(demand.back().size()) * 1024);
        }

        std::vector<Alloc> d_alloc, s_alloc;
        std::vector<std::vector<std::uint16_t>> s_caps;
        ClassDelta delta;
        StrictLog slog;
        for (std::size_t i = 0; i < demand.size(); ++i) {
            d_alloc.push_back(demand_single(demand[i], budget[i]));
            std::vector<std::uint16_t> caps;
            s_alloc.push_back(demand_strict(demand[i], d_alloc[i], slog, caps));
            s_caps.push_back(std::move(caps));
        }

        Index s_idx(cm);
        s_idx.bulk_load(keys);
        for (std::size_t i = 0; i < s_idx.region_count(); ++i)
            s_idx.rebuild_with_caps(i, s_idx.region(i).all(), s_alloc[i].sg, s_alloc[i].bb, s_caps[i]);

        const auto queries = make_queries(keys, 131072, 2026091600);

        // [mode 0 = S, 1 = S_range][class 0 = all, 1 = hit, 2 = miss]
        Counts cts[2][3];

        for (const auto& q : queries) {
            const Result a = s_idx.find(q.key, plain);
            const Result b = s_idx.find(q.key, range);
            if (a.found != b.found || (a.found && a.value != b.value))
                throw std::runtime_error("S and S_range disagree");

            const int cls = q.hit ? 1 : 2;
            cts[0][0].add(a);
            cts[1][0].add(b);
            cts[0][cls].add(a);
            cts[1][cls].add(b);
        }

        {
            std::ofstream out("results/main/e13_diag.jsonl", std::ios::app);
            out.exceptions(std::ios::badbit | std::ios::failbit);
            out << std::setprecision(10);
            out << "{\"kind\":\"" << kind << "\",\"queries\":" << queries.size()
                << ",\"note\":\"S and S_range share one Index object; only Mode::locate differs\""
                << ",\"by_class\":{";
            const char* cls_name[3] = {"all", "hit", "miss"};
            const char* mode_name[2] = {"S_locate_off", "S_range_locate_on"};
            for (int m = 0; m < 2; ++m) {
                if (m) out << ",";
                out << "\"" << mode_name[m] << "\":{";
                for (int k = 0; k < 3; ++k) {
                    if (k) out << ",";
                    out << "\"" << cls_name[k] << "\":{\"n\":" << cts[m][k].n << ",";
                    emit(out, cts[m][k]);
                    out << "}";
                }
                out << "}";
            }
            out << "}}" << std::endl;

            std::cerr << "hits=" << cts[0][1].n << " misses=" << cts[0][2].n << "\n";
            for (int m = 0; m < 2; ++m)
                std::cerr << "  " << mode_name[m]
                          << "  subblocks=" << mean(cts[m][0].subblocks_visited, cts[m][0].n)
                          << "  simd_batches=" << mean(cts[m][0].simd_batches, cts[m][0].n)
                          << "  locate_probes=" << mean(cts[m][0].locate_probes, cts[m][0].n)
                          << "  ov_probes=" << mean(cts[m][0].overflow_probes, cts[m][0].n) << "\n";
        }

        std::cerr << "COMPLETE " << kind << "\n";
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << "\n";
        return 1;
    }
}

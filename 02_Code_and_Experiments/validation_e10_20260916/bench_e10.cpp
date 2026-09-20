// E10 query access cost diagnosis (2026-09-16).
//
// Question: after multi-block reduces overflow, does it merely trade overflow
// scanning for sub-block traversal? This measures that trade directly.
//
// Scope: D, M and S are built from E9's allocators (e9_alloc.hpp) -- unchanged --
// and queried with ONE shared query sequence. No allocator changes, no timings,
// and no speed-up claim: the counters below are not commensurable (a tag batch, a
// string compare and a sub-block visit do not cost the same), so they are reported
// side by side to locate where the cost sits, never summed into a single score.
//
// Hit source is NOT required to match across layouts: a key that lands in D's
// overflow may already sit in S's primary blocks. That difference is the object of
// study, so only found/value are checked for agreement.

#include "data.hpp"
#include "e9_alloc.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

// ---------------------------------------------------------------------------

struct Q {
    std::string owned;
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
        q.hit = rng() % 2 == 0;
        q.owned = kv.first;
        if (!q.hit) {
            q.owned += "~missing~";
            auto it = std::lower_bound(keys.begin(), keys.end(), q.owned,
                [](const Pair& a, const std::string& b) { return compare(a.first, b) < 0; });
            if (it != keys.end() && it->first == q.owned)
                throw std::runtime_error("negative query exists");
        }
        q.key = q.owned;
        out.push_back(std::move(q));
    }

    return out;
}

// ---------------------------------------------------------------------------

struct AccessCounts {
    std::uint64_t subblocks_visited = 0;
    std::uint64_t simd_batches = 0;
    std::uint64_t tag_candidates = 0;
    std::uint64_t str_cmps_primary = 0;
    std::uint64_t str_cmps_overflow = 0;
    std::uint64_t overflow_entered = 0;
    std::uint64_t overflow_probes = 0;
    std::uint64_t model_steps = 0;
    std::uint64_t logical_cmp_bytes = 0;

    void add(const Result& r) {
        subblocks_visited += r.subblocks_visited;
        simd_batches += r.simd_batches;
        tag_candidates += r.tag_candidates;
        str_cmps_primary += r.str_cmps_primary;
        str_cmps_overflow += r.str_cmps_overflow;
        overflow_entered += r.overflow_entered;
        overflow_probes += r.overflow_checks;
        model_steps += r.steps;
        logical_cmp_bytes += r.cmp_bytes;
    }
};

static double mean(std::uint64_t total, std::size_t n) {
    return n ? double(total) / double(n) : 0.0;
}

static std::uint64_t percentile(std::vector<std::uint32_t> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const auto i = std::size_t(double(v.size() - 1) * p);
    return v[i];
}

static void emit_metrics(std::ostream& out, const AccessCounts& m, std::size_t n) {
    out << "\"subblocks_visited\":" << mean(m.subblocks_visited, n)
        << ",\"simd_batches\":" << mean(m.simd_batches, n)
        << ",\"tag_candidates\":" << mean(m.tag_candidates, n)
        << ",\"str_cmps_primary\":" << mean(m.str_cmps_primary, n)
        << ",\"str_cmps_overflow\":" << mean(m.str_cmps_overflow, n)
        << ",\"overflow_entered\":" << mean(m.overflow_entered, n)
        << ",\"overflow_probes\":" << mean(m.overflow_probes, n)
        << ",\"model_steps\":" << mean(m.model_steps, n)
        << ",\"logical_cmp_bytes\":" << mean(m.logical_cmp_bytes, n);
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::invalid_argument("bench_e10 KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const auto keys = make_data(kind, nkeys, trcsv);

        // Hybrid parameters come from E7's frozen file, same as E8/E9 used.
        std::uint32_t thr = 32;
        {
            std::ifstream fz("../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json");
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }
        const Mode mode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr};

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;

        Config cm = c;
        cm.multiblock = true;

        // --- demand, measured once on the uniform build ------------------------
        std::vector<std::vector<GroupStats>> demand;
        std::vector<std::uint64_t> budget;
        {
            Index probe(c);
            probe.bulk_load(keys);
            for (std::size_t i = 0; i < probe.region_count(); ++i) {
                demand.push_back(probe.region(i).group_stats());
                budget.push_back(std::uint64_t(demand.back().size()) * 1024);
            }
        }

        std::vector<Alloc> d_alloc, m_alloc, s_alloc;
        std::vector<std::vector<std::uint16_t>> s_caps;
        {
            ClassDelta delta;
            StrictLog slog;
            for (std::size_t i = 0; i < demand.size(); ++i) {
                d_alloc.push_back(demand_single(demand[i], budget[i]));
                m_alloc.push_back(demand_multi(demand[i], d_alloc[i], delta));
                std::vector<std::uint16_t> caps;
                s_alloc.push_back(demand_strict(demand[i], d_alloc[i], slog, caps));
                s_caps.push_back(std::move(caps));
            }
        }

        // --- build the three layouts -------------------------------------------
        Index d_idx(c);
        Index m_idx(cm);
        Index s_idx(cm);
        d_idx.bulk_load(keys);
        m_idx.bulk_load(keys);
        s_idx.bulk_load(keys);

        for (std::size_t i = 0; i < d_idx.region_count(); ++i) {
            d_idx.rebuild_with_caps(i, d_idx.region(i).all(), d_alloc[i].sg, d_alloc[i].bb);
            m_idx.rebuild_with_caps(i, m_idx.region(i).all(), m_alloc[i].sg, m_alloc[i].bb);
            s_idx.rebuild_with_caps(i, s_idx.region(i).all(), s_alloc[i].sg, s_alloc[i].bb, s_caps[i]);
        }

        const std::size_t nq = 131072;
        const auto queries = make_queries(keys, nq, 2026091600);

        // --- run the shared query sequence on each layout ----------------------
        const Index* layouts[3] = {&d_idx, &m_idx, &s_idx};
        const char* names[3] = {"D_demand_single", "M_multiblock", "S_multiblock_strict"};

        std::vector<Result> res[3];
        AccessCounts total[3];
        std::vector<std::uint32_t> sb_vis[3], ov_probes[3];

        for (int L = 0; L < 3; ++L) {
            res[L].reserve(nq);
            sb_vis[L].reserve(nq);
            ov_probes[L].reserve(nq);

            for (const auto& q : queries) {
                const Result r = layouts[L]->find(q.key, mode);
                res[L].push_back(r);
                total[L].add(r);
                sb_vis[L].push_back(r.subblocks_visited);
                ov_probes[L].push_back(r.overflow_checks);
            }

            // Same key set, so found/value must agree across layouts. Source is
            // deliberately not compared: it is expected to move.
            if (L > 0) {
                for (std::size_t i = 0; i < nq; ++i) {
                    if (res[L][i].found != res[0][i].found ||
                        (res[L][i].found && res[L][i].value != res[0][i].value))
                        throw std::runtime_error("layout answer mismatch");
                }
            }
        }

        // --- cohorts: partition by D's source, then look at the same queries ---
        const char* cohort_name[3] = {"primary_hit", "overflow_hit", "miss"};
        std::size_t cohort_count[3] = {0, 0, 0};
        AccessCounts cohort[3][3];   // [cohort][layout]

        for (std::size_t i = 0; i < nq; ++i) {
            const int cid = res[0][i].found ? (res[0][i].source == 2 ? 1 : 0) : 2;
            ++cohort_count[cid];
            for (int L = 0; L < 3; ++L) cohort[cid][L].add(res[L][i]);
        }

        // --- own classification: each layout by its own source -----------------
        AccessCounts own[3][3];
        std::size_t own_count[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

        for (int L = 0; L < 3; ++L) {
            for (const auto& r : res[L]) {
                const int cid = r.found ? (r.source == 2 ? 1 : 0) : 2;
                ++own_count[L][cid];
                own[L][cid].add(r);
            }
        }

        // --- emit --------------------------------------------------------------
        {
            std::ofstream out("results/main/e10_summary.jsonl", std::ios::app);
            out.exceptions(std::ios::badbit | std::ios::failbit);
            out << std::setprecision(10);

            out << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
                << ",\"queries\":" << nq << ",\"threshold\":" << thr;

            out << ",\"overall\":{";
            for (int L = 0; L < 3; ++L) {
                if (L) out << ",";
                out << "\"" << names[L] << "\":{";
                emit_metrics(out, total[L], nq);
                out << ",\"subblocks_visited_p95\":" << percentile(sb_vis[L], 0.95)
                    << ",\"subblocks_visited_max\":" << percentile(sb_vis[L], 1.0)
                    << ",\"overflow_probes_p95\":" << percentile(ov_probes[L], 0.95)
                    << ",\"overflow_probes_max\":" << percentile(ov_probes[L], 1.0)
                    << "}";
            }
            out << "}";

            out << ",\"by_own_source\":{";
            for (int L = 0; L < 3; ++L) {
                if (L) out << ",";
                out << "\"" << names[L] << "\":{";
                for (int c = 0; c < 3; ++c) {
                    if (c) out << ",";
                    out << "\"" << cohort_name[c] << "\":{\"count\":" << own_count[L][c] << ",";
                    emit_metrics(out, own[L][c], own_count[L][c]);
                    out << "}";
                }
                out << "}";
            }
            out << "}";

            out << ",\"cohort_by_D_source\":{";
            for (int c = 0; c < 3; ++c) {
                if (c) out << ",";
                out << "\"" << cohort_name[c] << "\":{\"count\":" << cohort_count[c];
                for (int L = 0; L < 3; ++L) {
                    out << ",\"" << names[L] << "\":{";
                    emit_metrics(out, cohort[c][L], cohort_count[c]);
                    out << "}";
                }
                out << "}";
            }
            out << "}}";
            out << std::endl;

            std::cerr << "cohorts by D source: primary=" << cohort_count[0]
                      << " overflow=" << cohort_count[1] << " miss=" << cohort_count[2] << "\n";
            for (int L = 0; L < 3; ++L)
                std::cerr << "  " << names[L]
                          << "  subblocks=" << mean(total[L].subblocks_visited, nq)
                          << "  simd_batches=" << mean(total[L].simd_batches, nq)
                          << "  ov_entered=" << mean(total[L].overflow_entered, nq)
                          << "  ov_probes=" << mean(total[L].overflow_probes, nq) << "\n";
        }

        std::cerr << "COMPLETE " << kind << "\n";
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << "\n";
        return 1;
    }
}

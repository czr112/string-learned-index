// E13: does sub-block range location pay off end to end?
//
// E12 is frozen; this round keeps its five methods and adds exactly one:
//
//   S_range   the SAME layout object as S, queried with Mode::locate = true.
//
// Because the boundary directory is built for every multi-block layout at
// construction time, S_range shares S's Index unchanged: same model, same grouping,
// same capacity table, same record placement, same directory -- and therefore the
// SAME space and the SAME build time. Only the lookup path differs. That makes
// S_range vs S a clean A/B on one variable.
//
// Queries are timed in three classes so a shift in the hit/miss balance cannot hide
// behind an aggregate: all, hits only, misses only. Range location could save the
// scan on a miss and simultaneously make a previously cheap primary hit pay for a
// boundary comparison, and only the per-class numbers show that.
//
// Protocol is E12's, unchanged: every method is verified query by query outside the
// timed region, then rounds are shuffled, warmed and checksummed. No tuning happens
// in this round either -- the layouts come from deterministic allocators over the
// full key set -- but the INHERITED fixed configuration is recorded with the run,
// because "no tuning this round" is not the same as "nothing to record".

#include "data.hpp"
#include "e9_alloc.hpp"
#include "native.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

using namespace validation;
using namespace validation::e9alloc;

using Clock = std::chrono::steady_clock;
static volatile std::uint64_t sink = 0;

static double ns(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}

static std::uint64_t checksum(const Result& r) {
    return r.found ? r.value + 0x9e3779b97f4a7c15ULL : 0;
}

struct BenchQuery {
    std::string key;
    std::uint64_t expected = 0;
    bool hit = false;
};

static std::vector<BenchQuery> make_queries(const std::vector<Pair>& keys, std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<BenchQuery> out;
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto& kv = keys[rng() % keys.size()];
        BenchQuery q;
        q.hit = i % 2 == 0;
        q.key = kv.first;
        if (q.hit) {
            q.expected = kv.second;
        } else {
            q.key += "~missing~";
            auto it = std::lower_bound(keys.begin(), keys.end(), q.key,
                [](const Pair& a, const std::string& b) { return compare(a.first, b) < 0; });
            if (it != keys.end() && it->first == q.key)
                throw std::runtime_error("negative query exists");
        }
        out.push_back(std::move(q));
    }

    std::shuffle(out.begin(), out.end(), rng);
    return out;
}

struct Method {
    std::string name;
    std::function<Result(const std::string&)> find;
    std::size_t bytes = 0;
    bool bytes_known = false;
    double build_ms = 0;
};

static const char* route_name(Route r) {
    return r == Route::Full ? "Full" : r == Route::Interval ? "Interval" : "Residual";
}
static const char* ov_name(OvSearch o) {
    return o == OvSearch::Linear ? "Linear" : o == OvSearch::Binary ? "Binary" : "Hybrid";
}
static const char* model_name(ModelKind k) {
    return k == ModelKind::Context16 ? "Context16" : k == ModelKind::Position16 ? "Position16" : "Unigram";
}

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        SetProcessAffinityMask(GetCurrentProcess(), 1);
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#elif defined(__linux__)
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 2)
            throw std::invalid_argument("bench_e13 KIND N [TRANCOCSV]");

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
        const Mode mode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};
        Mode mode_range = mode;
        mode_range.locate = true;

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;

        std::vector<Method> methods;

        {
            std::size_t bytes = keys.capacity() * sizeof(Pair);
            for (const auto& kv : keys) bytes += kv.first.capacity() + 1;
            methods.push_back({"sorted_array",
                [&keys](const std::string& k) {
                    auto it = std::lower_bound(keys.begin(), keys.end(), k,
                        [](const Pair& a, const std::string& b) { return compare(a.first, b) < 0; });
                    return it != keys.end() && it->first == k ? Result{true, it->second} : Result{};
                },
                bytes, true, 0.0});
        }

        {
            const auto t0 = Clock::now();
            auto art = std::make_shared<e12::NativeArt>(keys);
            const auto t1 = Clock::now();
            methods.push_back({"ART_libart",
                [art](const std::string& k) { return art->find(k); },
                art->bytes, true, ns(t0, t1) / 1e6});
        }

        {
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

            auto* d = new Index(c);
            const auto d0 = Clock::now();
            d->bulk_load(keys);
            for (std::size_t i = 0; i < d->region_count(); ++i)
                d->rebuild_with_caps(i, d->region(i).all(), d_alloc[i].sg, d_alloc[i].bb);
            const auto d1 = Clock::now();

            auto* s = new Index(cm);
            const auto s0 = Clock::now();
            s->bulk_load(keys);
            for (std::size_t i = 0; i < s->region_count(); ++i)
                s->rebuild_with_caps(i, s->region(i).all(), s_alloc[i].sg, s_alloc[i].bb, s_caps[i]);
            const auto s1 = Clock::now();

            methods.push_back({"D_demand_single",
                [d, mode](const std::string& k) { return d->find(k, mode); },
                d->memory(), true, ns(d0, d1) / 1e6});
            // S and S_range are the SAME object: identical layout, identical bytes.
            methods.push_back({"S_multiblock_strict",
                [s, mode](const std::string& k) { return s->find(k, mode); },
                s->memory(), true, ns(s0, s1) / 1e6});
            methods.push_back({"S_range_locate",
                [s, mode_range](const std::string& k) { return s->find(k, mode_range); },
                s->memory(), true, ns(s0, s1) / 1e6});
        }

        {
            const std::size_t r = 2048;
            const std::size_t regions = std::max<std::size_t>(1, (keys.size() + r - 1) / r);
            const auto t0 = Clock::now();
            auto* leaf = new SelectedIndex();
            leaf->bulk_load(keys, std::vector<int>(regions, SORTED_LEAF), r);
            const auto t1 = Clock::now();
            methods.push_back({"sorted_leaf_prefix_compressed",
                [leaf, mode](const std::string& k) { return leaf->find(k, mode); },
                leaf->memory(), true, ns(t0, t1) / 1e6});
        }

        // --- build record, INCLUDING the inherited fixed configuration ---------
        {
            std::ofstream b("results/main/e13_build.jsonl", std::ios::app);
            b.exceptions(std::ios::badbit | std::ios::failbit);
            b << std::setprecision(10);
            b << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
              << ",\"config\":{\"model_kind\":\"" << model_name(c.model_kind) << "\""
              << ",\"block_bytes\":" << c.block_bytes
              << ",\"slots\":" << c.slots
              << ",\"region_keys\":" << c.region_keys
              << ",\"route\":\"" << route_name(mode.route) << "\""
              << ",\"prefetch\":\"Off\""
              << ",\"fingerprint\":true"
              << ",\"simd\":true"
              << ",\"ov_search\":\"" << ov_name(mode.ov) << "\""
              << ",\"ov_threshold\":" << mode.ov_thr
              << ",\"ov_threshold_source\":\"" << frozen_rel << "\""
              << ",\"S_demand_allocator\":\"demand_strict\""
              << ",\"D_demand_allocator\":\"demand_single\""
              << ",\"S_range_is_same_object_as_S\":true"
              << ",\"query_hit_ratio\":0.5"
              << ",\"miss_key_construction\":\"source key + ~missing~, verified absent before timing\""
              << ",\"query_tuning_this_round\":false}"
              << ",\"methods\":[";
            for (std::size_t i = 0; i < methods.size(); ++i) {
                if (i) b << ",";
                b << "{\"name\":\"" << methods[i].name << "\",\"bytes\":" << methods[i].bytes
                  << ",\"bytes_known\":" << (methods[i].bytes_known ? "true" : "false")
                  << ",\"build_ms\":" << methods[i].build_ms << "}";
            }
            b << "]}" << std::endl;
        }

        // --- timed runs, split by query class -----------------------------------
        const std::size_t nq = 131072;
        const int seeds = 3;
        const int rounds = 5;

        std::ofstream csv("results/main/e13_timings.csv", std::ios::app);
        csv.exceptions(std::ios::badbit | std::ios::failbit);
        csv << std::setprecision(12);

        for (int seed = 0; seed < seeds; ++seed) {
            const auto all = make_queries(keys, nq, 2026091600 + seed);

            std::vector<BenchQuery> hits, misses;
            for (const auto& x : all) (x.hit ? hits : misses).push_back(x);

            const auto expected_of = [](const std::vector<BenchQuery>& v) {
                std::uint64_t s = 0;
                for (const auto& x : v) s += x.hit ? x.expected + 0x9e3779b97f4a7c15ULL : 0;
                return s;
            };
            const std::uint64_t want_all = expected_of(all);
            const std::uint64_t want_hit = expected_of(hits);
            const std::uint64_t want_miss = expected_of(misses);

            // Verify every query of every method, outside the timed region.
            for (auto& m : methods) {
                for (const auto& x : all) {
                    const Result r = m.find(x.key);
                    if (r.found != x.hit || (r.found && r.value != x.expected))
                        throw std::runtime_error("verification failed: " + m.name);
                }
            }

            struct Class { const char* name; const std::vector<BenchQuery>* q; std::uint64_t want; };
            const std::vector<Class> classes{{"all", &all, want_all},
                                             {"hit", &hits, want_hit},
                                             {"miss", &misses, want_miss}};

            for (int round = 0; round < rounds; ++round) {
                for (const auto& cls : classes) {
                    std::vector<std::size_t> order(methods.size());
                    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
                    std::shuffle(order.begin(), order.end(),
                                 std::mt19937_64(2026091631 + seed * 17 + round + std::strlen(cls.name)));

                    for (std::size_t idx : order) {
                        auto& m = methods[idx];
                        const auto& q = *cls.q;

                        std::uint64_t warm = 0;
                        for (std::size_t i = 0; i < std::min<std::size_t>(20000, q.size()); ++i)
                            warm += checksum(m.find(q[i].key));
                        sink = warm;

                        std::uint64_t sum = 0;
                        const auto t0 = Clock::now();
                        for (const auto& x : q) sum += checksum(m.find(x.key));
                        const auto t1 = Clock::now();

                        if (sum != cls.want) throw std::runtime_error("batch checksum: " + m.name);

                        csv << kind << ',' << keys.size() << ',' << q.size() << ',' << seed << ',' << round
                            << ',' << cls.name << ',' << m.name << ',' << ns(t0, t1) / double(q.size())
                            << ',' << m.bytes << ',' << (m.bytes_known ? 1 : 0)
                            << ',' << m.build_ms << ",1\n";
                    }
                }
            }

            std::cerr << "seed " << seed << " done (hits=" << hits.size()
                      << " misses=" << misses.size() << ")\n";
        }

        std::cerr << "COMPLETE " << kind << "\n";
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << "\n";
        return 1;
    }
}

// E8 v3 benchmark (review 2026-09-13): THREE real variants, randomly interleaved
// per seed, one shared query identity, strict per-region primary-byte budget:
//   V1: legacy fixed-stride addressing + uniform capacity   (original structure)
//   V2: offset-directory addressing + uniform capacity      (directory cost)
//   V3: offset-directory addressing + demand capacity       (reallocation gain)
// V1 runs on its own Index (fixed stride cannot host demand caps); V2/V3 share
// one Index and switch by rebuilding (rebuilds counted). Also outputs: space
// decomposition per variant, and E9 feasibility — how many remaining overflow
// records the CURRENT budget can absorb inside 48-slot-capped groups after
// reserving 17 B/record for the extra descriptor+tag.

#include "data.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

using namespace validation;
using Clock = std::chrono::steady_clock;

static volatile std::uint64_t sink = 0;

static double ns(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}

static std::uint64_t checksum(Result r) {
    return r.found ? r.value + 0x9e3779b97f4a7c15ULL : 0;
}

// ---------------------------------------------------------------------------
// Queries: test-identity only (rank % 4 != 0), 50% hits, independent copies;
// misses append "~missing~" and are verified absent before any timing.
// ---------------------------------------------------------------------------

struct Q {
    std::string owned;
    std::string key;
    std::uint64_t expected = 0;
    bool hit = false;
};

static std::vector<Q> make_q_test(const std::vector<Pair>& keys, std::size_t count, std::uint64_t seed) {
    std::vector<std::size_t> test_e;
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (i % 4) test_e.push_back(i);

    std::mt19937_64 rng(seed);
    std::vector<Q> out;
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto& kv = keys[test_e[rng() % test_e.size()]];
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
        if (q.hit) q.expected = kv.second;
        out.push_back(std::move(q));
    }

    std::shuffle(out.begin(), out.end(), rng);
    return out;
}

static std::uint64_t align64(std::uint64_t x) {
    return (x + 63) & ~std::uint64_t{63};
}

// ---------------------------------------------------------------------------
// E8-13: strict-budget demand allocator. sg = clamp(entries, 1, 48) lets empty
// groups shrink to one slot, freeing budget for crowded ones; suffix proportional;
// any overshoot is shaved 64B at a time from the group with the most shaveable
// slack; a residual shortfall (budget below the sum of legal minima) is returned,
// never hidden.
// ---------------------------------------------------------------------------

static std::uint64_t demand_rebuild(Index& idx) {
    std::uint64_t shortfall = 0;

    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const auto& reg = idx.region(i);
        const auto gs = reg.group_stats();
        const std::size_t G = gs.size();
        const std::uint64_t budget = std::uint64_t(G) * 1024;

        std::vector<std::uint16_t> sg(G);
        std::vector<std::uint64_t> sfx(G);
        std::uint64_t fixed = 0, sfx_demand = 0;
        for (std::size_t g = 0; g < G; ++g) {
            const std::uint64_t ent = gs[g].primary_entries + gs[g].overflow_entries;
            sg[g] = static_cast<std::uint16_t>(std::min<std::uint64_t>(std::max<std::uint64_t>(ent, 1), 48));
            sfx[g] = std::min<std::uint64_t>(gs[g].primary_bytes + gs[g].overflow_bytes, 60000);
            fixed += 64 + std::uint64_t(sg[g]) * 16;
            sfx_demand += sfx[g];
        }

        const std::uint64_t avail = budget > fixed ? budget - fixed : 0;
        const double scale = sfx_demand ? double(avail) / double(sfx_demand) : 0.0;

        std::vector<std::uint32_t> bb(G);
        std::uint64_t used = 0;
        for (std::size_t g = 0; g < G; ++g) {
            const std::uint64_t so = 64 + std::uint64_t(sg[g]) * 16;
            const std::uint64_t sf = std::min<std::uint64_t>(sfx[g], std::uint64_t(double(sfx[g]) * scale));
            const std::uint64_t min_bb = align64(so + 1);
            bb[g] = static_cast<std::uint32_t>(std::max(align64(so + sf), min_bb));
            used += bb[g];
        }

        // Shave overshoot from the group with the most shaveable slack.
        while (used > budget) {
            std::size_t best = SIZE_MAX;
            std::uint64_t best_slack = 0;
            for (std::size_t g = 0; g < G; ++g) {
                const std::uint64_t slack = bb[g] - align64(64 + std::uint64_t(sg[g]) * 16 + 1);
                if (slack > best_slack) { best_slack = slack; best = g; }
            }
            if (best == SIZE_MAX || best_slack == 0) break;
            const std::uint64_t step = std::min<std::uint64_t>(64, best_slack);
            bb[best] -= static_cast<std::uint32_t>(align64(step));
            used -= align64(step);
        }

        // Distribute leftover round-robin.
        std::size_t guard = 0;
        while (used < budget && guard++ < G * 1024) {
            for (std::size_t g = 0; g < G && used < budget; ++g) {
                bb[g] += 64;
                used += 64;
            }
        }

        if (used > budget) shortfall += used - budget;

        for (std::size_t g = 0; g < G; ++g) {
            const auto so = 64 + std::uint64_t(sg[g]) * 16;
            if (sg[g] == 0 || sg[g] > 48 || bb[g] % 64 || bb[g] <= so || bb[g] > 65535)
                std::cerr << "REAL violation region=" << i << " g=" << g
                          << " sg=" << sg[g] << " bb=" << bb[g] << " so=" << so << "\n";
        }

        auto data = reg.all();
        idx.rebuild_with_caps(i, std::move(data), std::move(sg), std::move(bb));
    }
    return shortfall;
}

static void uniform_rebuild(Index& idx) {
    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const std::size_t G = idx.region(i).slot_table().size();
        idx.rebuild_with_caps(i, idx.region(i).all(),
                              std::vector<std::uint16_t>(G, 16),
                              std::vector<std::uint32_t>(G, 1024));
    }
}

// ---------------------------------------------------------------------------
// E8 review (2026-09-14): independent space accounting.
//
// The previous version reported `model_other = total - blocks - overflow - tables`
// and then "verified" `blocks + overflow + tables + model_other == total`. That is
// a tautology — it holds by construction and would also hold after unsigned
// wraparound, so it proved nothing.
//
// Now every component is accumulated on its own from Index::memory_breakdown(), and
// the component sum is reconciled against Index::memory(), which walks the regions
// through a different code path. Each component is also checked against the total
// BEFORE any subtraction, which is what catches the uint64 underflow the earlier
// version hit.
// ---------------------------------------------------------------------------

struct Space {
    std::uint64_t blocks = 0, model = 0, prefix = 0, overflow = 0, tables = 0,
                  regions_self = 0, fences = 0, self = 0;
    std::uint64_t total = 0;      // Index::memory(), computed by the other path
    unsigned __int128 sum = 0;    // component sum, accumulated in 128-bit

    void take(const Index& idx) {
        const auto b = idx.memory_breakdown();
        blocks += b.blocks;
        model += b.model;
        prefix += b.prefix;
        overflow += b.overflow;
        tables += b.tables;
        regions_self += b.regions_self;
        fences += b.fences;
        self += b.self;
        total = idx.memory();
        sum = 0;
        for (std::uint64_t v : {blocks, model, prefix, overflow, tables, regions_self, fences, self})
            sum += v;   // 128-bit: the addition itself cannot wrap
    }

    // Every component must be <= the total; a wrapped (underflowed) component would
    // violate this immediately and is the failure mode the old check could not see.
    bool components_sane() const {
        for (std::uint64_t v : {blocks, model, prefix, overflow, tables, regions_self, fences, self})
            if (v > total) return false;
        return true;
    }
    bool reconciles() const {
        return components_sane() && sum < (static_cast<unsigned __int128>(1) << 63) &&
               static_cast<std::uint64_t>(sum) == total;
    }
};

static void emit_space(std::ostream& out, const Space& s) {
    out << "{\"blocks\":" << s.blocks
        << ",\"model\":" << s.model
        << ",\"prefix\":" << s.prefix
        << ",\"overflow_containers\":" << s.overflow
        << ",\"tables\":" << s.tables
        << ",\"regions_self\":" << s.regions_self
        << ",\"fences\":" << s.fences
        << ",\"self\":" << s.self
        << ",\"component_sum\":" << static_cast<std::uint64_t>(s.sum)
        << ",\"total\":" << s.total
        << ",\"components_sane\":" << (s.components_sane() ? "true" : "false")
        << ",\"reconciles\":" << (s.reconciles() ? "true" : "false") << "}";
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        SetProcessAffinityMask(GetCurrentProcess(), 1);
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#elif defined(__linux__)
        // Linux path (E8 review 2026-09-14): the original code pinned only on
        // Windows, so a Linux run was unpinned and migratable. Pin to CPU 0 like
        // the first three rounds did with taskset; nice() is best effort.
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
        (void)nice(-5);
#endif
        if (argc < 2)
            throw std::invalid_argument("bench_e8 KIND N [TRANCOCVS]");

        const std::string kind = argv[1];
        const std::size_t n = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const auto keys = make_data(kind, n, trcsv);

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
        const Mode hyb{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr};
        const std::size_t nq = 131072;
        const int seeds = 3;
        std::cerr << "frozen hybrid threshold=" << thr << "\n";

        Config c_dir;
        c_dir.offset_directory = true;
        Config c_fix;
        c_fix.offset_directory = false;

        Index v1(c_fix);
        v1.bulk_load(keys);

        Index idx(c_dir);
        idx.bulk_load(keys);
        const std::size_t overflow_start = idx.overflow_count();

        // ------------------------------------------------------------------
        // Interleaved measurement: per seed a random order of {V1, V2, V3};
        // V2/V3 switch by rebuilding (counted). Same queries (same seed) for
        // all three variants.
        // ------------------------------------------------------------------
        std::ofstream csv("results/main/e8_phases_" + kind + ".csv");
        csv.exceptions(std::ios::badbit | std::ios::failbit);
        csv << std::setprecision(12);
        csv << "phase,seed,ns_per_op\n";

        enum State { S_U, S_D };
        State cur = S_U;
        std::uint64_t u_rebuilds = 0, d_rebuilds = 0, shortfall = 0;
        std::vector<double> v1_v, u_v, d_v;

        for (int seed = 0; seed < seeds; ++seed) {
            auto q = make_q_test(keys, nq, 2026091410 + static_cast<std::uint64_t>(seed));
            const std::uint64_t want = [&] {
                std::uint64_t s = 0;
                for (const auto& x : q) s += checksum({x.hit, x.expected});
                return s;
            }();

            std::vector<int> order{0, 1, 2};
            std::mt19937_64 rng(2026091499 + static_cast<std::uint64_t>(seed));
            std::shuffle(order.begin(), order.end(), rng);

            for (auto which : order) {
                const std::string phase = which == 0 ? "V1" : which == 1 ? "V2" : "V3";
                if (which == 1 && cur != S_U) { uniform_rebuild(idx); cur = S_U; ++u_rebuilds; }
                if (which == 2 && cur != S_D) { shortfall += demand_rebuild(idx); cur = S_D; ++d_rebuilds; }

                // Warm-up pass over the same batch. E8 previously had NO warm-up,
                // unlike the E5/E6 benches, so whichever variant happened to be
                // measured first in a seed paid the page-in and cold-cache cost.
                std::uint64_t warm = 0;
                if (which == 0) {
                    for (const auto& x : q) warm += checksum(v1.find(x.key, hyb));
                } else {
                    for (const auto& x : q) warm += checksum(idx.find(x.key, hyb));
                }
                if (warm != want) throw std::runtime_error("warmup checksum " + phase);

                std::uint64_t sum = 0;
                const auto t0 = Clock::now();
                if (which == 0) {
                    for (const auto& x : q) sum += checksum(v1.find(x.key, hyb));
                } else {
                    for (const auto& x : q) sum += checksum(idx.find(x.key, hyb));
                }
                const auto t1 = Clock::now();

                if (sum != want) throw std::runtime_error("checksum " + phase);
                const double v = ns(t0, t1) / q.size();
                (which == 0 ? v1_v : which == 1 ? u_v : d_v).push_back(v);
                csv << phase << ',' << seed << ',' << v << '\n';
                std::cerr << phase << " seed " << seed << " " << v << "\n";
            }
        }

        // ------------------------------------------------------------------
        // Final decompositions, captured in a fixed order (V1, then U, then D).
        // ------------------------------------------------------------------
        auto med = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };

        Space v1_sp, u_sp, d_sp;

        v1_sp.take(v1);

        // Cross-check of the claim that offset_directory changes only addressing,
        // not layout: V1 and the uniform directory index must have identical
        // capacity tables and therefore identical overflow counts.
        if (v1.overflow_count() != overflow_start)
            throw std::runtime_error("V1/uniform overflow count mismatch");

        uniform_rebuild(idx);
        ++u_rebuilds;
        const std::size_t uniform_overflow = idx.overflow_count();
        u_sp.take(idx);

        const std::uint64_t d_shortfall = demand_rebuild(idx);
        ++d_rebuilds;
        const std::uint64_t all_shortfall = shortfall + d_shortfall;
        d_sp.take(idx);
        const std::size_t demand_overflow = idx.overflow_count();

        // Hard gate: the decomposition must reconcile. A wrapped component cannot
        // reach here, because Space::components_sane() rejects v > total.
        if (!v1_sp.reconciles() || !u_sp.reconciles() || !d_sp.reconciles()) {
            std::cerr << "SPACE ACCOUNTING FAILED v1=" << v1_sp.reconciles()
                      << " v2=" << u_sp.reconciles() << " v3=" << d_sp.reconciles() << "\n";
            throw std::runtime_error("space accounting does not reconcile");
        }

        // ------------------------------------------------------------------
        // E9 feasibility on the demand layout: absorbable overflow records inside
        // 48-slot-capped groups, reserving 17 B per added slot (Entry 16 + tag 1).
        // ------------------------------------------------------------------
        std::uint64_t capped_cfg = 0, capped_full = 0;
        std::uint64_t overflow_in_capped = 0, absorbable = 0, absorb_in_full = 0;
        for (std::size_t i = 0; i < idx.region_count(); ++i) {
            const auto& reg = idx.region(i);
            const auto& sg = reg.slot_table();
            const auto gs = reg.group_stats();
            for (std::uint32_t g = 0; g < gs.size(); ++g) {
                if (sg[g] != 48) continue;
                ++capped_cfg;
                const bool full = gs[g].primary_entries == 48;
                if (full) ++capped_full;
                overflow_in_capped += gs[g].overflow_entries;
                if (gs[g].overflow_entries == 0) continue;

                auto lens = reg.overflow_lengths(g);
                std::sort(lens.begin(), lens.end());
                std::uint64_t running = gs[g].free_bytes;
                std::uint64_t k = 0;
                for (std::uint32_t len : lens) {
                    const std::uint64_t cost = std::uint64_t(len) + 17;
                    if (running >= cost) { running -= cost; ++k; }
                    else break;
                }
                absorbable += k;
                if (full) absorb_in_full += k;
            }
        }

        std::ofstream out("results/main/e8_summary.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(12);
        out << "{\"kind\":\"" << kind << "\",\"threshold\":" << thr
            << ",\"keys\":" << keys.size()
            << ",\"space_note\":\"components accumulated independently; total from Index::memory()\""
            << ",\"V1_fixed_uniform\":{\"offset_directory\":false"
            << ",\"overflow_keys\":" << v1.overflow_count()
            << ",\"median_ns\":" << med(v1_v)
            << ",\"space\":";
        emit_space(out, v1_sp);
        out << "}"
            << ",\"V2_dir_uniform\":{\"offset_directory\":true"
            << ",\"overflow_keys\":" << uniform_overflow
            << ",\"median_ns\":" << med(u_v)
            << ",\"rebuilds\":" << u_rebuilds
            << ",\"space\":";
        emit_space(out, u_sp);
        out << "}"
            << ",\"V3_dir_demand\":{\"offset_directory\":true"
            << ",\"overflow_keys\":" << demand_overflow
            << ",\"median_ns\":" << med(d_v)
            << ",\"rebuilds\":" << d_rebuilds
            << ",\"budget_shortfall_bytes_last\":" << d_shortfall
            << ",\"budget_shortfall_bytes_all\":" << all_shortfall
            << ",\"space\":";
        emit_space(out, d_sp);
        out << "}"
            << ",\"e9_feasibility\":{\"idealized_suffix_plus_17B_per_record\":true"
            << ",\"excludes\":\"multi-block headers, 64B alignment, sub-block directory\""
            << ",\"groups_configured_48slots\":" << capped_cfg
            << ",\"groups_with_all_48_slots_occupied\":" << capped_full
            << ",\"overflow_records_in_capped_groups\":" << overflow_in_capped
            << ",\"absorbable_within_current_budget_17B_per_slot\":" << absorbable
            << ",\"absorbable_in_full_groups\":" << absorb_in_full << "}}"
            << std::endl;

        std::cerr << "COMPLETE " << kind << "\n";
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << "\n";
        return 1;
    }
}

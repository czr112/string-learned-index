// G3/A1' gate: widening WHICH sub-blocks the reclaim may touch must not change WHAT the
// index holds, and the widened branch must actually run.
//
// Derived from check_e31.cpp -- that file and its build record are untouched, because they
// back E31's delivered results. Three arms here:
//
//   frozen  the shipped configuration, reclaim off
//   E31     shipped + subblock_reclaim          -- tightens the rank-selected sub-block only
//   WIDE    E31     + reclaim_widest            -- then the group's OTHER blocks, dirtiest
//                                                 first, up to a fixed budget
//
// The single variable between E31 and WIDE is `reclaim_widest`. Between frozen and E31 it is
// `subblock_reclaim`. Both are asserted with require_only_switches_differ, so an arm that
// silently became a copy of another fails the gate instead of passing a comparison that
// never happened -- the mistake E30 caught in the switch whitelist.
//
// What must hold:
//   * CONTENT and KEY ORDER identical across all three arms and against the reference,
//     element by element at every checkpoint (not by checksum);
//   * the WIDENED branch actually RAN. The counters are process-global and only the WIDE
//     arm has the switch on, so `reclaim_widened > 0` at the last checkpoint is exactly
//     "the new code was reached". Asserted, not printed and ignored.
//   * the boundary directory still lists each non-empty sub-block and its minimum key, on
//     ALL THREE arms, so a failure cannot be blamed on the new code by default.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

struct Pipeline {
    std::vector<Alloc> s_alloc;
    std::vector<std::vector<std::uint16_t>> s_caps;
};

static Pipeline make_plan(const std::vector<std::vector<GroupStats>>& demand) {
    Pipeline p;
    for (const auto& gs : demand) {
        const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
        const Alloc d = demand_single(gs, budget);
        StrictLog log;
        std::vector<std::uint16_t> caps;
        p.s_alloc.push_back(demand_strict(gs, d, log, caps));
        p.s_caps.push_back(std::move(caps));
    }
    return p;
}

static auto sg_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint16_t>> o;
    for (const auto& a : v) o.push_back(a.sg);
    return o;
}

static auto bb_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint32_t>> o;
    for (const auto& a : v) o.push_back(a.bb);
    return o;
}

// The reclaim's claim is that it moves bytes inside one sub-block and nothing else, so the
// boundary directory needs no rebuild. Re-check that directly: for every multi-block group,
// the directory must still list exactly the non-empty sub-blocks, in order, and each entry
// must still be that sub-block's minimum key. Returns how many groups were compared.
static std::uint64_t check_directory(const Index& idx) {
    std::uint64_t groups_checked = 0;

    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const auto& r = idx.region(i);

        for (std::uint32_t g = 0; g < r.groups(); ++g) {
            const std::size_t nb = r.subblock_count(g);
            if (nb < 2) continue;
            ++groups_checked;

            const auto dir = r.boundary_dir(g);
            std::size_t rank = 0;

            for (std::size_t k = 0; k < nb; ++k) {
                if (r.subblock_entries(g, k) == 0) continue;

                if (rank >= dir.size() || dir[rank].second != k)
                    throw std::runtime_error("boundary directory no longer lists this sub-block");

                const auto keys = r.subblock_keys(g, k);
                if (keys.empty() || keys.front() != dir[rank].first)
                    throw std::runtime_error("boundary is no longer the sub-block minimum");

                ++rank;
            }

            if (dir.size() != rank)
                throw std::runtime_error("directory size != non-empty sub-blocks");
        }
    }

    return groups_checked;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_widest KIND N [TRANCOCSV] [RUNTAG] [MODE]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const std::string mode = argc > 5 ? argv[5] : "same";
        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        const std::string frozen_rel =
            "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss; ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        Config frozen = e25sw::baseline_config(thr);
        frozen.write_fast_locate = true;
        frozen.reuse_locate_ctx = true;
        frozen.scan_reserve_result = true;
        frozen.compact_reuse_tag = true;
        frozen.compact_needs_garbage = true;

        Config e31 = frozen;
        e31.subblock_reclaim = true;

        Config wide = e31;
        wide.reclaim_widest = true;

        e25sw::require_only_switches_differ(frozen, e31, {"subblock_reclaim"});
        e25sw::require_only_switches_differ(e31, wide, {"reclaim_widest"});
        e25sw::require_only_switches_differ(frozen, wide, {"subblock_reclaim", "reclaim_widest"});

        std::cerr << "arms frozen[" << e25sw::switches(frozen) << "]\n"
                  << "     E31   [" << e25sw::switches(e31) << "]\n"
                  << "     WIDE  [" << e25sw::switches(wide) << "]" << std::endl;

        Pipeline plan;
        {
            Index probe(frozen);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        std::deque<std::string> live;
        std::unordered_set<std::string> live_set;
        Reference ref;

        for (const auto& kv : keys) {
            live.push_back(kv.first);
            ref[kv.first] = kv.second;
            live_set.insert(kv.first);
        }

        auto a = std::make_unique<Index>(frozen);
        auto b = std::make_unique<Index>(e31);
        auto c = std::make_unique<Index>(wide);

        a->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
        b->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
        c->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        std::uint64_t serial = 0, compared = 0;
        std::size_t done = 0;
        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        for (double frac : CPS) {
            const std::size_t target = static_cast<std::size_t>(double(N) * frac);

            while (done < target) {
                const std::string victim = live.front();
                live.pop_front();
                live_set.erase(victim);

                std::string base = victim;
                const auto pos = base.find("~r");
                if (pos != std::string::npos) base.resize(pos);

                std::string fresh;

                if (mode == "grow") {
                    fresh = base + "~r" + std::to_string(serial);
                } else {
                    fresh = base;
                    bool ok = false;
                    for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                        fresh.back() = static_cast<char>('a' + ((serial + t) % 26));
                        if (fresh != victim && ref.find(fresh) == ref.end() &&
                            live_set.find(fresh) == live_set.end()) { ok = true; break; }
                    }
                    if (!ok) throw std::runtime_error("alphabet exhausted");
                }

                const std::uint64_t val = 0x7000ULL + serial;
                ++serial;
                ref.erase(victim);
                ref[fresh] = val;
                a->erase(victim); a->insert(fresh, val);
                b->erase(victim); b->insert(fresh, val);
                c->erase(victim); c->insert(fresh, val);
                live.push_back(fresh);
                live_set.insert(fresh);
                ++done;
            }

            // Full content comparison, element by element, across all three arms AND the
            // reference. Agreeing on a wrong answer is not a pass.
            const auto ca = a->scan(std::string_view{}, a->size());
            const auto cb = b->scan(std::string_view{}, b->size());
            const auto cc = c->scan(std::string_view{}, c->size());

            if (ca.size() != ref.size() || cb.size() != ref.size() || cc.size() != ref.size())
                throw std::runtime_error("size != reference");

            for (std::size_t i = 0; i < ca.size(); ++i) {
                ++compared;

                if (ca[i].first != cb[i].first || ca[i].second != cb[i].second ||
                    ca[i].first != cc[i].first || ca[i].second != cc[i].second)
                    throw std::runtime_error("content or order differs at " + std::to_string(i) +
                                             " key=" + ca[i].first);

                const auto it = ref.find(ca[i].first);
                if (it == ref.end() || it->second != ca[i].second)
                    throw std::runtime_error("disagrees with reference: " + ca[i].first);
            }

#ifdef E25_DIAG
            const std::uint64_t reclaims = validation::e25::counts().subblock_reclaims;
            const std::uint64_t reclaim_helped = validation::e25::counts().subblock_reclaim_helped;
            const std::uint64_t widened = validation::e25::counts().reclaim_widened;
            const std::uint64_t widened_helped = validation::e25::counts().reclaim_widened_helped;
            const std::uint64_t widened_seen = validation::e25::counts().reclaim_widened_blocks_examined;
#else
#error "check_widest must be built with -DE25_DIAG: it asserts that the widened branch ran"
#endif

            const std::uint64_t dir_groups = check_directory(*a) + check_directory(*b) + check_directory(*c);

            if (frac == 2.00 && reclaims == 0)
                throw std::runtime_error("the reclaim branch never ran: this gate verified nothing about it");

            if (frac == 2.00 && widened == 0)
                throw std::runtime_error(
                    "the WIDENED branch never ran: the gate would be certifying nothing about it");

            std::cout << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\""
                      << ",\"frac\":" << frac << ",\"live\":" << ref.size()
                      << ",\"frozen_overflow\":" << a->overflow_count()
                      << ",\"e31_overflow\":" << b->overflow_count()
                      << ",\"wide_overflow\":" << c->overflow_count()
                      << ",\"frozen_memory\":" << a->memory()
                      << ",\"e31_memory\":" << b->memory()
                      << ",\"wide_memory\":" << c->memory()
                      << ",\"records_compared\":" << compared
                      << ",\"dir_groups_checked\":" << dir_groups
                      << ",\"subblock_reclaims\":" << reclaims
                      << ",\"subblock_reclaim_helped\":" << reclaim_helped
                      << ",\"reclaim_widened\":" << widened
                      << ",\"reclaim_widened_helped\":" << widened_helped
                      << ",\"reclaim_widened_blocks_examined\":" << widened_seen
                      << "}" << std::endl;

            std::cerr << kind << " " << mode << " frac=" << frac << " ok" << std::endl;
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

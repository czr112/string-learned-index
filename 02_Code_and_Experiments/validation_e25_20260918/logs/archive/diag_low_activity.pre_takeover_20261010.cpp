// Low-activity sub-block reclaim: MECHANISM diagnostic, not a timing benchmark.
//
// THE QUESTION. The candidate lifts repack_subblock's `n < 2` early return and makes
// subblock_garbage() report garbage for those same blocks, so reclaim_widen can rank them.
// The entry gate (check_low_activity) already proved the branch runs and that the two arms
// stay observationally equal. What it did NOT do is run the regime where the earlier hole
// diagnosis found the opportunity: LONG REPLACEMENT, where 53%-98% of block-level byte
// refusals land on blocks holding 0-1 live records (RESULTS_HOLES_ENTRY.md).
//
// TWO ARMS, SAME OPERATION SEQUENCE. E31 on in both (subblock_reclaim), reclaim_widest off
// in both, compact_region_routing off in both; the ONLY difference is reclaim_low_activity.
// Every mutation is generated OUTSIDE any timed span.
//
// COUNTERS ARE SEGMENTED. The E25/E18 counters are cleared at every checkpoint, so each row
// reports what happened INSIDE that segment. A "total" row repeats the whole 0->200% span
// with the counters cleared once; the analyzer checks that it equals the sum of the
// segments rather than trusting it.
//
// NO TIMING. A count locates an opportunity; it does not price it.
//
// USAGE:  diag_low_activity KIND N [TRANCOCSV]
// Modes:  fifo (erase the oldest live key), random (fixed-seed permuted victims),
//         insert (pure insert control, no erases).

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

#ifndef E25_DIAG
#error "diag_low_activity must be built with -DE25_DIAG"
#endif

namespace {

std::uint64_t checks = 0;
std::uint64_t failures = 0;

void require(bool condition, const std::string& what) {
    ++checks;

    if (!condition) {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

struct UByteLess {
    bool operator()(std::string_view a, std::string_view b) const {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](char x, char y) {
                                                return static_cast<unsigned char>(x) <
                                                       static_cast<unsigned char>(y);
                                            });
    }
};

using Reference = std::map<std::string, std::uint64_t, UByteLess>;

const Mode kMode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

Config arm_config(std::uint32_t thr, bool low_activity) {
    Config c = e25sw::baseline_config(thr);
    e25sw::pin_frozen_switches(c);
    c.subblock_reclaim = true;
    c.reclaim_widest = false;
    c.compact_region_routing = false;
    c.reclaim_low_activity = low_activity;
    return c;
}

struct Row {
    std::string kind, mode, arm;
    double frac = 0.0;
    std::uint64_t subblock_reclaims = 0, subblock_reclaim_helped = 0;
    std::uint64_t la_reclaimed = 0, la_n0 = 0, la_n1 = 0, la_bytes = 0;
    std::uint64_t reclaim_widened = 0, reclaim_widened_helped = 0;
    std::uint64_t place_fail_slots = 0, place_fail_bytes = 0;
    std::uint64_t compact_entered = 0, compact_helped = 0, compact_no_help = 0;
    std::uint64_t compact_plan_rejected = 0;
    std::uint64_t overflow_inserts = 0, insert_attempts = 0;
    std::uint64_t size = 0, live_keys = 0, regions = 0;
    std::uint64_t index_bytes = 0, overflow_bytes = 0, blocks_bytes = 0;
};

std::uint64_t mem_index_bytes(const Index& ix) {
    const auto m = ix.memory_breakdown();
    return m.blocks + m.model + m.prefix + m.overflow + m.tables + m.directory +
           m.regions_self + m.fences + m.self;
}

void emit(std::ostream& out, const Row& r) {
    out << "{\"kind\":\"" << r.kind << "\",\"mode\":\"" << r.mode
        << "\",\"arm\":\"" << r.arm << "\",\"frac\":" << r.frac
        << ",\"subblock_reclaims\":" << r.subblock_reclaims
        << ",\"subblock_reclaim_helped\":" << r.subblock_reclaim_helped
        << ",\"low_activity_reclaimed\":" << r.la_reclaimed
        << ",\"low_activity_n0\":" << r.la_n0
        << ",\"low_activity_n1\":" << r.la_n1
        << ",\"low_activity_bytes\":" << r.la_bytes
        << ",\"reclaim_widened\":" << r.reclaim_widened
        << ",\"reclaim_widened_helped\":" << r.reclaim_widened_helped
        << ",\"place_fail_slots\":" << r.place_fail_slots
        << ",\"place_fail_bytes\":" << r.place_fail_bytes
        << ",\"compact_entered\":" << r.compact_entered
        << ",\"compact_helped\":" << r.compact_helped
        << ",\"compact_no_help\":" << r.compact_no_help
        << ",\"compact_plan_rejected\":" << r.compact_plan_rejected
        << ",\"overflow_inserts\":" << r.overflow_inserts
        << ",\"insert_attempts\":" << r.insert_attempts
        << ",\"size\":" << r.size
        << ",\"live_keys\":" << r.live_keys
        << ",\"regions\":" << r.regions
        << ",\"index_bytes\":" << r.index_bytes
        << ",\"overflow_bytes\":" << r.overflow_bytes
        << ",\"blocks_bytes\":" << r.blocks_bytes
        << "}" << std::endl;
}

Row snapshot_row(const std::string& kind, const std::string& mode, const std::string& arm,
                 double frac, const Index& ix, std::size_t live) {
    const auto& C = e25::counts();
    const auto& E = e18::counts();
    Row r;
    r.kind = kind;
    r.mode = mode;
    r.arm = arm;
    r.frac = frac;
    r.subblock_reclaims = C.subblock_reclaims;
    r.subblock_reclaim_helped = C.subblock_reclaim_helped;
    r.la_reclaimed = C.low_activity_reclaimed;
    r.la_n0 = C.low_activity_n0;
    r.la_n1 = C.low_activity_n1;
    r.la_bytes = C.low_activity_bytes;
    r.reclaim_widened = C.reclaim_widened;
    r.reclaim_widened_helped = C.reclaim_widened_helped;
    r.place_fail_slots = C.place_fail_slots;
    r.place_fail_bytes = C.place_fail_bytes;
    r.compact_entered = C.compact_entered;
    r.compact_helped = C.compact_helped;
    r.compact_no_help = C.compact_no_help;
    r.compact_plan_rejected = C.compact_plan_rejected;
    r.overflow_inserts = E.overflow_inserts;
    r.insert_attempts = E.insert_attempts;
    r.size = ix.size();
    r.live_keys = live;
    r.regions = ix.region_count();
    const auto m = ix.memory_breakdown();
    r.index_bytes = mem_index_bytes(ix);
    r.overflow_bytes = m.overflow;
    r.blocks_bytes = m.blocks;
    return r;
}

Row diff_row(const Row& b, const Row& a) {
    Row d = b;
    d.subblock_reclaims = b.subblock_reclaims - a.subblock_reclaims;
    d.subblock_reclaim_helped = b.subblock_reclaim_helped - a.subblock_reclaim_helped;
    d.la_reclaimed = b.la_reclaimed - a.la_reclaimed;
    d.la_n0 = b.la_n0 - a.la_n0;
    d.la_n1 = b.la_n1 - a.la_n1;
    d.la_bytes = b.la_bytes - a.la_bytes;
    d.reclaim_widened = b.reclaim_widened - a.reclaim_widened;
    d.reclaim_widened_helped = b.reclaim_widened_helped - a.reclaim_widened_helped;
    d.place_fail_slots = b.place_fail_slots - a.place_fail_slots;
    d.place_fail_bytes = b.place_fail_bytes - a.place_fail_bytes;
    d.compact_entered = b.compact_entered - a.compact_entered;
    d.compact_helped = b.compact_helped - a.compact_helped;
    d.compact_no_help = b.compact_no_help - a.compact_no_help;
    d.compact_plan_rejected = b.compact_plan_rejected - a.compact_plan_rejected;
    d.overflow_inserts = b.overflow_inserts - a.overflow_inserts;
    d.insert_attempts = b.insert_attempts - a.insert_attempts;
    return d;
}

void verify(const Index& ix, const Reference& ref, const std::string& label) {
    require(ix.size() == ref.size(), label + ": size matches the reference");

    std::size_t probes = 0;
    std::size_t mismatch = 0;

    for (const auto& kv : ref) {
        const Result r = ix.find(kv.first, kMode);
        ++probes;

        if (!r.found || r.value != kv.second) ++mismatch;
    }

    require(probes == ref.size(), label + ": every reference key was probed");
    require(mismatch == 0, label + ": every reference key finds its value");

    require(!ix.find(std::string("~absent~", 8), kMode).found,
            label + ": an absent key misses");

    std::vector<Pair> got = ix.scan(std::string(), ref.size() + 16);
    require(got.size() == ref.size(), label + ": the scan returns every live key");

    std::size_t row_mismatch = 0;
    std::size_t i = 0;

    for (const auto& kv : ref) {
        if (i >= got.size() || got[i].first != kv.first || got[i].second != kv.second)
            ++row_mismatch;
        ++i;
    }

    require(row_mismatch == 0, label + ": the scan agrees row by row");
}

std::string normalize_victim(const std::string& victim) {
    std::string base = victim;
    const auto pos = base.find("~f");

    if (pos != std::string::npos) base.resize(pos);

    return base;
}

struct Move {
    std::string victim;
    std::string fresh;
    std::uint64_t value = 0;
};

struct Plan {
    std::vector<std::vector<std::uint16_t>> sg;
    std::vector<std::vector<std::uint32_t>> bb;
    std::vector<std::vector<std::uint16_t>> caps;
};

Plan make_plan(const Config& cfg, const std::vector<Pair>& pairs) {
    Index probe(cfg);
    probe.bulk_load(pairs);
    std::vector<std::vector<GroupStats>> demand;

    for (std::size_t i = 0; i < probe.region_count(); ++i)
        demand.push_back(probe.region(i).group_stats());

    Plan p;

    for (const auto& gs : demand) {
        const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
        const Alloc d = demand_single(gs, budget);
        StrictLog log;
        std::vector<std::uint16_t> c;
        const Alloc strict = demand_strict(gs, d, log, c);
        p.sg.push_back(strict.sg);
        p.bb.push_back(strict.bb);
        p.caps.push_back(std::move(c));
    }

    return p;
}

void run_mode(std::ostream& out, const std::string& kind, const std::string& mode,
              const std::vector<Pair>& keys, std::uint32_t thr, bool do_erase) {
    const std::size_t N = keys.size();
    const Config off = arm_config(thr, false);
    Config on = off;
    on.reclaim_low_activity = true;

    e25sw::require_only_switches_differ(off, on, {"reclaim_low_activity"});

    const Plan plan = make_plan(off, keys);

    auto a = std::make_unique<Index>(off);
    auto b = std::make_unique<Index>(on);
    a->bulk_load_with_plan(keys, plan.sg, plan.bb, plan.caps);
    b->bulk_load_with_plan(keys, plan.sg, plan.bb, plan.caps);

    Reference ref;

    for (const auto& kv : keys) ref.emplace(kv.first, kv.second);

    std::deque<std::string> live;
    std::unordered_set<std::string> live_set;

    for (const auto& kv : keys) {
        live.push_back(kv.first);
        live_set.insert(kv.first);
    }

    std::vector<std::size_t> perm(N);

    for (std::size_t i = 0; i < N; ++i) perm[i] = i;

    std::mt19937_64 rng(20261010);
    std::shuffle(perm.begin(), perm.end(), rng);

    const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
    std::size_t done = 0;
    std::uint64_t serial = 0;
    std::size_t perm_pos = 0;
    const char* arm_names[2] = {"off", "on"};

    for (int ci = 0; ci < 4; ++ci) {
        const double frac = CPS[ci];
        const std::size_t target = static_cast<std::size_t>(double(N) * frac);
        std::vector<Move> seg;

        for (std::size_t i = done; i < target; ++i) {
            // THE VICTIM HAS TO EXIST IN THE REFERENCE, and this is not a detail. In random
            // mode a position can be drawn twice; in fifo mode the queue holds keys already
            // replaced. A victim that is no longer live makes erase() a NO-OP, and the
            // replacement then grows the index while the intended swap did not happen -- the
            // exact silent corruption this program exists to catch. Advancing past absent
            // candidates keeps the count identity "one erase + one insert = no change" true
            // by construction rather than by hope.
            std::string victim;
            std::size_t guard = 0;

            while (victim.empty()) {
                if (mode == "fifo") {
                    const std::string k = live.front();
                    live.pop_front();

                    if (ref.count(k) != 0) victim = k;
                } else {
                    const std::string k = keys[perm[perm_pos % N]].first;
                    ++perm_pos;

                    if (ref.count(k) != 0) victim = k;
                }

                if (++guard > 4 * N + 16)
                    throw std::runtime_error("victim search exhausted in mode " + mode);
            }

            live_set.erase(victim);
            const std::string base = normalize_victim(victim);
            ++serial;
            std::string fresh = base + "~f" + std::to_string(serial);

            while (ref.count(fresh) != 0) fresh += "x";

            const std::uint64_t value = 0x5000ULL + serial;
            live.push_back(fresh);
            live_set.insert(fresh);
            ref.erase(victim);
            ref.emplace(fresh, value);
            seg.push_back({victim, fresh, value});
        }

        done = target;

        for (int arm = 0; arm < 2; ++arm) {
            Index& ix = (arm == 0) ? *a : *b;
            e25::counts().reset();
            e18::counts().reset();
            const Row before = snapshot_row(kind, mode, arm_names[arm], frac, ix, live.size());

            if (getenv("LA_TRACE")) {
                fprintf(stderr, "TRACE start frac=%f arm=%s ix=%zu live=%zu seg=%zu\n", frac,
                        arm_names[arm], ix.size(), live.size(), seg.size());
            }

            for (const auto& m : seg) {
                const std::size_t s0 = ix.size();
                const bool er = do_erase ? ix.erase(m.victim) : true;
                require(er, "the victim of a replacement must be present");
                const std::size_t s1 = ix.size();
                const bool in = ix.insert(m.fresh, m.value);
                const std::size_t s2 = ix.size();

                if (getenv("LA_TRACE")) {
                    fprintf(stderr, "TRACE op ix=%zu s0=%zu s1=%zu s2=%zu er=%d in=%d\n",
                            ix.size(), s0, s1, s2, int(er), int(in));
                }

                if (!in || s2 != s1 + 1) {
                    // Print BOTH sizes and the two answers, because "the insert returned
                    // true but the index did not grow" and "the insert returned false" are
                    // different defects and the counter alone cannot tell them apart.
                    const Result pr = ix.find(m.fresh, kMode);
                    throw std::runtime_error(
                        "replacement did not add exactly one: erase=" + std::to_string(int(er)) +
                        " insert=" + std::to_string(int(in)) +
                        " size " + std::to_string(s0) + " -> " + std::to_string(s1) +
                        " -> " + std::to_string(s2) +
                        " probe_found=" + std::to_string(int(pr.found)) +
                        " ref_has_fresh=" + std::to_string(int(ref.count(m.fresh))) +
                        " victim_len=" + std::to_string(m.victim.size()) +
                        " fresh_len=" + std::to_string(m.fresh.size()) +
                        " at frac " + std::to_string(frac) +
                        " arm " + arm_names[arm]);
                }
            }

            std::size_t fresh_missing = 0;
            std::size_t fresh_wrong = 0;

            for (const auto& m : seg) {
                const Result pr = ix.find(m.fresh, kMode);

                if (!pr.found) ++fresh_missing;
                else if (pr.value != m.value) ++fresh_wrong;
            }

            if (fresh_missing || fresh_wrong)
                throw std::runtime_error("post-segment audit: missing=" +
                                         std::to_string(fresh_missing) + " wrong=" +
                                         std::to_string(fresh_wrong) +
                                         " at frac " + std::to_string(frac) + " arm " +
                                         arm_names[arm]);

            const Row after = snapshot_row(kind, mode, arm_names[arm], frac, ix, live.size());
            emit(out, diff_row(after, before));

            if (ix.size() != live.size())
                throw std::runtime_error("SEGMENT live-count drift [seg=" +
                                         std::to_string(seg.size()) + " checked=" +
                                         std::to_string(seg.size()) + "] at frac " +
                                         std::to_string(frac) +
                                         " arm " + arm_names[arm] + ": index=" +
                                         std::to_string(ix.size()) + " live=" +
                                         std::to_string(live.size()));
        }

        verify(*a, ref, kind + "/" + mode + "/off");
        verify(*b, ref, kind + "/" + mode + "/on");
    }
}

void run_insert_control(std::ostream& out, const std::string& kind, const std::vector<Pair>& keys,
                        std::uint32_t thr) {
    std::vector<Pair> loaded;
    std::vector<Pair> pending;

    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i % 2 == 0) loaded.push_back(keys[i]);
        else pending.push_back(keys[i]);
    }

    const Config off = arm_config(thr, false);
    Config on = off;
    on.reclaim_low_activity = true;

    const Plan plan = make_plan(off, loaded);

    auto a = std::make_unique<Index>(off);
    auto b = std::make_unique<Index>(on);
    a->bulk_load_with_plan(loaded, plan.sg, plan.bb, plan.caps);
    b->bulk_load_with_plan(loaded, plan.sg, plan.bb, plan.caps);

    Reference ref;

    for (const auto& kv : loaded) ref.emplace(kv.first, kv.second);

    const char* arm_names[2] = {"off", "on"};

    for (int arm = 0; arm < 2; ++arm) {
        Index& ix = (arm == 0) ? *a : *b;
        e25::counts().reset();
        e18::counts().reset();
        const Row before = snapshot_row(kind, "insert", arm_names[arm], 1.0, ix, loaded.size());

        for (const auto& kv : pending) ix.insert(kv.first, kv.second);

        const Row after = snapshot_row(kind, "insert", arm_names[arm], 1.0, ix, ref.size());
        emit(out, diff_row(after, before));
        require(ix.size() == ref.size(), kind + "/insert/" + arm_names[arm] + ": size");
    }

    for (const auto& kv : pending) ref.emplace(kv.first, kv.second);

    verify(*a, ref, kind + "/insert/off");
    verify(*b, ref, kind + "/insert/on");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_low_activity KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        const auto keys = make_data(kind, nkeys, trcsv);

        const std::string frozen_rel =
            "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
            std::ifstream fz(frozen_rel);

            if (fz) {
                std::stringstream ss;
                ss << fz.rdbuf();
                const std::string t = ss.str();
                const auto p = t.find("\"frozen_hybrid_threshold\":");

                if (p != std::string::npos)
                    thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
            }
        }

        std::ostringstream body;
        body << std::setprecision(10);

        run_mode(body, kind, "fifo", keys, thr, true);
        run_mode(body, kind, "random", keys, thr, true);
        run_insert_control(body, kind, keys, thr);

        std::cout << body.str();

        if (failures != 0) {
            std::cout << failures << " FAILED out of " << checks << "\n";
            return 1;
        }

        std::cout << "# " << checks << "/" << checks << " checks passed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
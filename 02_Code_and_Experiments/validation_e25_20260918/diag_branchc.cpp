// Branch-C entry diagnosis: how much of the overflow container's retained capacity is
// POST-DECLINE retention (reclaimable by a shrink path) as opposed to the doubling policy's
// geometric overhang (not reclaimable without changing the growth policy)?
//
// THE DECOMPOSITION IS THE WHOLE POINT. A naive "capacity - size" reports A + B together,
// and on a monotone workload that sum is almost entirely A. E24's own table shows it: the
// capacity/live ratio sits at 1.31-1.41 while the record count spans 53x -- a growth-policy
// constant, not retention. So this program reports, per container:
//
//   Term A = cap_elems - size_high_water    capacity this container has NEVER USED
//   Term B = size_high_water - size         capacity it used before and is not using now
//
// NEITHER TERM IS "THE RECLAIMABLE ONE", and an earlier draft of this file said A was
// unreclaimable while B was the only reclaimable part. That is arithmetically false: a shrink
// takes capacity off the top of the allocation, so shrinking to the live count collects
// A + B and shrinking to the historical peak collects A. What the split actually measures is
// DURABILITY, not reclaimability -- A is headroom the container has never touched, B is
// headroom it touched and may want again. The round's verdict rides on that reading, so the
// two comments must not be confused.
//
// TWO WORKLOADS, AND NEITHER ONE ALONE SETTLES IT:
//   W1 long replacement  -- kept because it reproduces E24's caliber. An earlier draft called
//                           its Term B "identically 0 by construction" because the LIVE COUNT
//                           is constant; that is a stock-level-to-per-element inference, the
//                           exact error class this project has already committed twice, and
//                           the round's own W1 rows refute it (tranco reads Sigma B = 90-939
//                           elements). W1's Term B is MEASURED small, not structurally absent.
//   W2 net decline       -- deliberately deepens the decline (strides 4, 2, 1) so Term B is
//                           exercised rather than incidental.
//
// AND THE RE-GROWTH COUNT IS NOT A GROWTH COUNT. What decides whether shrinking is durable is
// whether live count comes BACK after it falls. So the trackers count DROPS and REGROWTH
// after a drop, not cumulative capacity changes -- in W1 every capacity change is growth the
// container legitimately needed, and calling that "re-growth" would answer a different
// question.
//
// READ-ONLY: no mutator, no Config switch, no prototype. COUNTS LOCATE CANDIDATES; NO SPEEDUP
// IS ESTIMATED and no timings are taken.
//
// Output: results/main/branchc_<kind>.jsonl
//   {"t":"agg",...}  the aggregates, emitted explicitly so the decision does not depend on
//                    re-deriving them from the per-container dump
//   {"t":"c",...}    one record per container, for the distribution
//
// Usage:  diag_branchc KIND N [TRANCOCSV]

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
#error "diag_branchc.cpp needs -DE25_DIAG: the trackers are diagnostic-only"
#endif

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

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_branchc KIND N [TRANCOCSV]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

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

        // THE SHIPPED CONFIGURATION. Branch C's entry condition is about the default path.
        Config cfg = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg);

        Pipeline plan;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        std::ofstream out(("results/main/branchc_" + kind + ".jsonl").c_str(), std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        // The record size comes FROM THE INDEX, not from the driver. The review's first
        // finding was that this container's capacity had no element-unit accessor and the
        // only capacity-shaped quantity was in bytes; re-deriving the size here would
        // reintroduce exactly that unit ambiguity at the call site instead of at the source.
        const std::size_t REC_BYTES = Region::diag_overflow_record_bytes();

        // Emit the aggregates EXPLICITLY, in two populations: every container that exists,
        // and the non-empty ones. E24's caliber was over non-empty containers, so the
        // comparison uses that one; the every-container row shows what the empties retain.
        const auto dump = [&](const char* workload, const char* phase, double frac,
                              const Index& s, std::size_t live_total, bool truncated) {
            std::vector<Region::OvCapStat> caps;
            s.diag_overflow_caps(caps);

            struct Agg {
                std::size_t n = 0, n_empty = 0, sum_size = 0, sum_cap = 0, sum_A = 0, sum_B = 0;
                std::size_t sum_cap_bytes = 0;
                std::uint64_t drops = 0, regrowth = 0;
                bool any_stable = false, all_stable = true;
            } all, nonempty;

            for (const auto& c : caps) {
                const std::size_t A = c.cap_elems > c.high_water ? c.cap_elems - c.high_water : 0;
                const std::size_t B = c.high_water > c.size ? c.high_water - c.size : 0;

                const bool e = (c.size == 0);
                for (Agg* a : {&all, e ? nullptr : &nonempty}) {
                    if (!a) continue;
                    a->n += 1;
                    if (e) a->n_empty += 1;
                    a->sum_size += c.size;
                    a->sum_cap += c.cap_elems;
                    a->sum_cap_bytes += c.cap_bytes;
                    a->sum_A += A;
                    a->sum_B += B;
                    a->drops += c.drops;
                    a->regrowth += c.regrowth;
                    a->any_stable = a->any_stable || c.stable;
                    a->all_stable = a->all_stable && c.stable;
                }

                out << "{\"t\":\"c\",\"kind\":\"" << kind << "\",\"workload\":\"" << workload
                    << "\",\"phase\":\"" << phase << "\""
                    << ",\"frac\":" << frac
                    << ",\"size\":" << c.size
                    << ",\"cap_elems\":" << c.cap_elems
                    << ",\"cap_bytes\":" << c.cap_bytes
                    << ",\"high_water\":" << c.high_water
                    << ",\"A\":" << A
                    << ",\"B\":" << B
                    << ",\"drops\":" << c.drops
                    << ",\"regrowth\":" << c.regrowth
                    << ",\"stable\":" << (c.stable ? "true" : "false")
                    << "}" << std::endl;
            }

            const auto emit_agg = [&](const char* which, const Agg& a) {
                const double weighted = a.sum_cap ? double(a.sum_B) / double(a.sum_cap) : 0.0;
                const double naive = a.sum_cap
                    ? double(a.sum_A + a.sum_B) / double(a.sum_cap) : 0.0;

                out << "{\"t\":\"agg\",\"kind\":\"" << kind << "\",\"workload\":\"" << workload
                    << "\",\"phase\":\"" << phase << "\",\"population\":\"" << which << "\""
                    << ",\"frac\":" << frac
                    << ",\"live_total\":" << live_total
                    << ",\"truncated_horizon\":" << (truncated ? "true" : "false")
                    << ",\"containers\":" << a.n
                    << ",\"containers_empty\":" << a.n_empty
                    << ",\"sum_size_elems\":" << a.sum_size
                    << ",\"sum_cap_elems\":" << a.sum_cap
                    << ",\"sum_cap_bytes\":" << a.sum_cap_bytes
                    << ",\"sum_A_elems\":" << a.sum_A
                    << ",\"sum_B_elems\":" << a.sum_B
                    << ",\"sum_B_bytes\":" << a.sum_B * REC_BYTES
                    << ",\"weighted_B_over_cap\":" << weighted
                    << ",\"naive_AplusB_over_cap\":" << naive
                    << ",\"sum_drops\":" << a.drops
                    << ",\"sum_regrowth\":" << a.regrowth
                    << ",\"any_stable\":" << (a.any_stable ? "true" : "false")
                    << ",\"all_stable\":" << (a.all_stable ? "true" : "false")
                    << "}" << std::endl;
            };

            emit_agg("all", all);
            emit_agg("nonempty", nonempty);
        };

        // ---------------- W1: long replacement to 200% ----------------
        // Kept to reproduce E24's caliber. NOTE what it does NOT establish: the live count is
        // constant, but that says nothing about any INDIVIDUAL container, and W1's own rows
        // show containers falling below their own peak (tranco reads Sigma B = 90-939
        // elements). This arm's Term B is measured small, not absent by construction. An
        // earlier draft of this file claimed the latter -- a stock-level-to-per-element
        // inference, which is the error class this project has committed twice before.
        {
            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;
            for (const auto& kv : keys) {
                live.push_back(kv.first);
                live_set.insert(kv.first);
            }

            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

            // The pre-replacement state, so the reader can see the DYNAMIC path's overflow
            // stock rather than having to infer it. On the smoke corpus it is 99 records in
            // 31 containers against a bulk-loaded set of 20,000 keys -- i.e. the planned
            // load leaves almost nothing in overflow and the replacement churn is what
            // creates containers at all.
            dump("replace", "before", 0.00, *s, s->size(), false);

            const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
            std::size_t done = 0;
            std::uint64_t serial = 0;

            for (int ci = 0; ci < 4; ++ci) {
                const double frac = CPS[ci];
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);

                for (std::size_t i = done; i < target; ++i) {
                    const std::string victim = live.front();
                    live.pop_front();
                    live_set.erase(victim);

                    std::string base = victim;
                    const auto pos = base.find("~r");
                    if (pos != std::string::npos) base.resize(pos);

                    std::string fresh = base + "~r" + std::to_string(serial++);
                    while (!live_set.insert(fresh).second) fresh += "x";

                    live.push_back(fresh);
                    s->erase(victim);
                    s->insert(fresh, 0x7000ULL + serial);
                }

                done = target;

                // A counts-only probe still has to stand on a correct run: if the index's
                // live count has drifted from the driver's own bookkeeping, every capacity
                // figure below describes a structure neither of us intended.
                if (s->size() != live.size())
                    throw std::runtime_error("W1 live-count drift at frac " +
                                             std::to_string(frac));

                // The horizon note: if the overflow stock is still climbing at this
                // checkpoint, the LAST interval is truncated and its re-growth risk is
                // under-observed. Stated in the record rather than left implicit.
                const bool truncated = (ci == 3);
                dump("replace", truncated ? "past_one_full" : "short_term", frac, *s,
                     s->size(), truncated);
            }
        }

        // ---------------- W2: net decline ----------------
        // W2 needs three properties that the obvious implementation does not have.
        //
        // 1. SPATIALLY UNIFORM. Erasing the first k keys in sorted order drains a few
        //    containers completely and leaves the rest untouched, so every container is
        //    either at its peak or at zero and NO PARTIAL DECLINE EVER OCCURS. Term B then
        //    reduces to "empty containers are empty", which is true by construction and
        //    answers nothing. The smoke run of that version read Sigma B = 35 over the whole
        //    population while the NON-EMPTY population read exactly 0. Erasures therefore
        //    step by a stride across the key space, so each container loses a fraction.
        //
        // 2. INSERTS, or regrowth is vacuous. Under pure erasure nothing ever comes back, so
        //    regrowth == 0 by construction and the half of the entry rule that asks whether
        //    a declined container REFILLS cannot be evaluated. One fresh key per four
        //    erasures keeps the net negative while letting a container that just fell be
        //    targeted again -- the fresh key is derived from the victim, so it lands in the
        //    same neighbourhood rather than somewhere harmless.
        //
        // 3. AN EXPLICIT TREND. Strides 4, 2, 1 give 25%, 50%, 100% erasure of the original
        //    set, crossing the halfway point rather than sampling one arbitrary depth.
        {
            std::vector<std::string> live;
            std::vector<char> alive;
            std::unordered_set<std::string> live_set;
            for (const auto& kv : keys) {
                live.push_back(kv.first);
                live_set.insert(kv.first);
            }
            alive.assign(live.size(), 1);

            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

            dump("decline", "before", 1.00, *s, s->size(), false);

            const int STRIDES[3] = {4, 2, 1};
            std::uint64_t serial = 0;
            std::uint64_t erased = 0;

            for (int si = 0; si < 3; ++si) {
                const int d = STRIDES[si];

                for (std::size_t i = 0; i < live.size(); ++i) {
                    if (!alive[i] || (i % static_cast<std::size_t>(d) != 0)) continue;

                    const std::string victim = live[i];
                    alive[i] = 0;
                    live_set.erase(victim);
                    s->erase(victim);
                    ++erased;

                    // One insert per four erasures: the net stays a decline, but a container
                    // that just fell is eligible to be refilled, so regrowth is measurable.
                    if (erased % 4 == 0) {
                        std::string base = victim;
                        const auto pos = base.find("~r");
                        if (pos != std::string::npos) base.resize(pos);

                        std::string fresh = base + "~q" + std::to_string(serial++);
                        while (!live_set.insert(fresh).second) fresh += "x";

                        s->insert(fresh, 0x9000ULL + serial);
                    }
                }

                if (s->size() != live_set.size())
                    throw std::runtime_error("W2 live-count drift at stride " +
                                             std::to_string(d));

                const double frac = double(1) - double(si + 1) / 4.0;
                dump("decline", si == 2 ? "past_half" : "short_term", frac, *s, s->size(), false);
            }
        }

        std::cout << "{\"kind\":\"" << kind << "\",\"case\":\"done\"}" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

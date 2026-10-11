// Hole-reuse (本块空洞复用) entry diagnosis: when a placement is refused for want of
// BYTES, is there a dead range in THAT SAME sub-block that could have held the record?
//
// THE REFUSAL IS A CONTIGUOUS-RUN TEST, NOT A CAPACITY TEST. insert_into refuses when
// "s.size() > h.bottom - start_of(g)" -- the free run at the bottom of the suffix area.
// It never looks at the dead ranges inside the occupied region, so a block can hold
// plenty of dead bytes and still turn a record away. That gap is the candidate.
//
// THE PAIRING IS PER SUB-BLOCK, AND THAT IS NOT A DETAIL. A dead range in sub-block X
// cannot hold a suffix that belongs in sub-block Y: placement is per-block and invariant
// M1 keeps the blocks' key ranges disjoint. Comparing the index-wide distribution of hole
// sizes against the index-wide distribution of suffix lengths would pair holes with
// requests that could never reach them and OVERSTATE the match rate. Every number here is
// produced AT the refusal, ON the block that refused.
//
// TWO TESTS, ONE EVENT, because the case for the candidate is a comparison:
//   fits_hole    some dead range in this block is at least this long -- copies NOTHING
//   fits_repack  contiguous free + this block's dead bytes covers it -- this is what
//                repack_subblock() does today, and it copies every live suffix
// Reported apart so "与紧凑化单独对照" is a paired comparison rather than two rates
// measured over different populations.
//
// KEY ORDER IS NOT PART OF THE TEST. insert_into writes at bottom - len, so the physical
// order of suffixes is INSERTION order, not key order; Entry::offset is read only by
// suffix(), by repack_subblock (which re-derives the order itself) and by an assert that
// the range stays inside the block. The entry ARRAY index carries key order. So a dead
// range is usable wherever it lies, and no ordering constraint has to be modelled.
//
// TWO WORKLOADS. Replacement erases and inserts, so it both CUTS holes and consumes them,
// which is the candidate's own regime. Pure insert barely cuts any, and is the control
// that says whether a match rate is a property of the workload or of the structure.
//
// READ-ONLY: no mutator, no Config switch, no prototype. NO TIMING -- a match rate is a
// count, and counts locate candidates, they do not price them.
//
// Output: results/main/holes_<kind>.jsonl, one row per byte refusal, plus a summary.
// Usage:  diag_holes KIND N [TRANCOCSV]

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
#error "diag_holes.cpp needs -DE25_DIAG: the sweep and the event log are diagnostic-only"
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

// A snapshot of the counters this round reads. Taken before and after each workload so
// the two cannot be confused with each other or with the previous workload's totals.
struct Snap {
    std::uint64_t fail_bytes = 0, fail_slots = 0;
    std::uint64_t hole_fits = 0, hole_absent = 0, repack_fits = 0;
    std::uint64_t len_sum = 0, best_sum = 0, garbage_sum = 0, live_sum = 0;
    std::uint64_t sweep_checks = 0, sweep_mismatch = 0;
    std::uint64_t recorded = 0, dropped = 0;
    std::uint64_t reclaims = 0, reclaim_helped = 0;
    // What the SHIPPED path actually runs. subblock_reclaim and reclaim_widest are both
    // false there, so `reclaims` above is zero by configuration rather than by absence of
    // opportunity -- and the reader has to be able to see that rather than infer it.
    std::uint64_t compact_entered = 0, compact_helped = 0, compact_no_help = 0;
    std::uint64_t whole_group_garbage = 0;
    // I10's prepare phase proving the group cannot hold its own records. This is the
    // expected dominant outcome on these workloads and it is why compact() helps so
    // rarely: it is not that consolidation would not free the bytes, it is that the
    // plan refuses to run at all. Without this number the two readings are
    // indistinguishable from the outside.
    std::uint64_t compact_plan_rejected = 0;
    // E18, a different counter set and not E25_DIAG-gated: how many records actually
    // reached a container. A refusal is a block-level event; this is the insert-level one,
    // and the distance between the two is the whole reason a per-block pairing is needed.
    std::uint64_t overflow_inserts = 0;
    std::uint64_t insert_attempts = 0;
    std::uint64_t n0 = 0, n1 = 0, n2 = 0, best_eq_garbage = 0;
    std::uint64_t count_lt2 = 0, slots_not_free = 0, repack_guarded = 0;
};

static Snap snapshot() {
    const auto& C = e25::counts();
    Snap s;
    s.fail_bytes = C.place_fail_bytes;
    s.fail_slots = C.place_fail_slots;
    s.hole_fits = C.fail_bytes_hole_fits;
    s.hole_absent = C.fail_bytes_hole_absent;
    s.repack_fits = C.fail_bytes_repack_fits;
    s.len_sum = C.fail_bytes_len_sum;
    s.best_sum = C.fail_bytes_best_hole_sum;
    s.garbage_sum = C.fail_bytes_garbage_sum;
    s.live_sum = C.fail_bytes_live_sum;
    s.sweep_checks = C.hole_sweep_checks;
    s.sweep_mismatch = C.hole_sweep_mismatch;
    s.recorded = C.hole_events_recorded;
    s.dropped = C.hole_events_dropped;
    s.reclaims = C.subblock_reclaims;
    s.reclaim_helped = C.subblock_reclaim_helped;
    s.compact_entered = C.compact_entered;
    s.compact_helped = C.compact_helped;
    s.compact_no_help = C.compact_no_help;
    s.whole_group_garbage = C.compact_garbage_before;
    s.compact_plan_rejected = C.compact_plan_rejected;
    s.overflow_inserts = validation::e18::counts().overflow_inserts;
    s.insert_attempts = validation::e18::counts().insert_attempts;
    s.n0 = C.hole_nholes0;
    s.n1 = C.hole_nholes1;
    s.n2 = C.hole_nholes2plus;
    s.best_eq_garbage = C.hole_best_eq_garbage;
    s.count_lt2 = C.hole_count_lt2;
    s.slots_not_free = C.hole_slots_not_free;
    s.repack_guarded = C.fail_bytes_repack_guarded;
    return s;
}

static Snap diff(const Snap& a, const Snap& b) {
    Snap d;
    d.fail_bytes = b.fail_bytes - a.fail_bytes;
    d.fail_slots = b.fail_slots - a.fail_slots;
    d.hole_fits = b.hole_fits - a.hole_fits;
    d.hole_absent = b.hole_absent - a.hole_absent;
    d.repack_fits = b.repack_fits - a.repack_fits;
    d.len_sum = b.len_sum - a.len_sum;
    d.best_sum = b.best_sum - a.best_sum;
    d.garbage_sum = b.garbage_sum - a.garbage_sum;
    d.live_sum = b.live_sum - a.live_sum;
    d.sweep_checks = b.sweep_checks - a.sweep_checks;
    d.sweep_mismatch = b.sweep_mismatch - a.sweep_mismatch;
    d.recorded = b.recorded - a.recorded;
    d.dropped = b.dropped - a.dropped;
    d.reclaims = b.reclaims - a.reclaims;
    d.reclaim_helped = b.reclaim_helped - a.reclaim_helped;
    d.compact_entered = b.compact_entered - a.compact_entered;
    d.compact_helped = b.compact_helped - a.compact_helped;
    d.compact_no_help = b.compact_no_help - a.compact_no_help;
    d.whole_group_garbage = b.whole_group_garbage - a.whole_group_garbage;
    d.compact_plan_rejected = b.compact_plan_rejected - a.compact_plan_rejected;
    d.overflow_inserts = b.overflow_inserts - a.overflow_inserts;
    d.insert_attempts = b.insert_attempts - a.insert_attempts;
    d.n0 = b.n0 - a.n0;
    d.n1 = b.n1 - a.n1;
    d.n2 = b.n2 - a.n2;
    d.best_eq_garbage = b.best_eq_garbage - a.best_eq_garbage;
    d.count_lt2 = b.count_lt2 - a.count_lt2;
    d.slots_not_free = b.slots_not_free - a.slots_not_free;
    d.repack_guarded = b.repack_guarded - a.repack_guarded;
    return d;
}

static void emit(std::ostream& out, const char* kind, const char* workload, double frac,
                 const Region::HoleEvent& e) {
    out << "{\"kind\":\"" << kind << "\",\"workload\":\"" << workload
        << "\",\"frac\":" << frac
        << ",\"group\":" << e.group
        << ",\"blk\":" << e.blk
        << ",\"len\":" << e.len
        << ",\"contig\":" << e.contig
        << ",\"garbage\":" << e.garbage
        << ",\"live\":" << e.live
        << ",\"nholes\":" << e.nholes
        << ",\"best_hole\":" << e.best_hole
        << ",\"fits_hole\":" << (e.fits_hole ? "true" : "false")
        << ",\"fits_repack\":" << (e.fits_repack ? "true" : "false")
        << ",\"slots_free\":" << (e.slots_free ? "true" : "false")
        << "}" << std::endl;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_holes KIND N [TRANCOCSV]");
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

            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");

            if (p == std::string::npos) throw std::runtime_error("frozen field missing");

            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        // THE SHIPPED CONFIGURATION. The candidate would have to pay for itself on the
        // default path, so the default path is what gets measured.
        Config cfg = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg);

        std::ofstream out(("results/main/holes_" + kind + ".jsonl").c_str(), std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        std::ostringstream summary;
        summary << std::setprecision(6);

        // The events of one segment, attributed to that segment and then cleared. Without
        // the clear the first quarter's refusals are indistinguishable from the last's, and
        // the per-checkpoint counts would describe a population the rows do not.
        const auto flush_events = [&](Index& s, const char* workload, double frac) {
            std::vector<Region::HoleEvent> ev;
            s.diag_hole_events(ev);

            for (const auto& e : ev) emit(out, kind.c_str(), workload, frac, e);

            s.diag_clear_hole_events();

            return ev.size();
        };

        const auto report = [&](const char* workload, double frac, const Snap& d,
                                std::size_t rows) {
            const double fit_rate = d.fail_bytes
                ? double(d.hole_fits) / double(d.fail_bytes) : 0.0;
            const double repack_rate = d.fail_bytes
                ? double(d.repack_fits) / double(d.fail_bytes) : 0.0;
            const double mean_len = d.fail_bytes
                ? double(d.len_sum) / double(d.fail_bytes) : 0.0;
            const double mean_best = d.fail_bytes
                ? double(d.best_sum) / double(d.fail_bytes) : 0.0;

            summary << "{\"kind\":\"" << kind << "\",\"workload\":\"" << workload
                    << "\",\"frac\":" << frac
                    << ",\"fail_bytes\":" << d.fail_bytes
                    << ",\"fail_slots\":" << d.fail_slots
                    << ",\"hole_fits\":" << d.hole_fits
                    << ",\"hole_absent\":" << d.hole_absent
                    << ",\"hole_match_rate\":" << fit_rate
                    << ",\"repack_fits\":" << d.repack_fits
                    << ",\"repack_match_rate\":" << repack_rate
                    << ",\"mean_len\":" << mean_len
                    << ",\"mean_best_hole\":" << mean_best
                    << ",\"mean_garbage\":" << (d.fail_bytes
                        ? double(d.garbage_sum) / double(d.fail_bytes) : 0.0)
                    << ",\"mean_live_to_copy\":" << (d.fail_bytes
                        ? double(d.live_sum) / double(d.fail_bytes) : 0.0)
                    << ",\"sweep_checks\":" << d.sweep_checks
                    << ",\"sweep_mismatch\":" << d.sweep_mismatch
                    << ",\"events_recorded\":" << d.recorded
                    << ",\"events_dropped\":" << d.dropped
                    << ",\"rows_written\":" << rows
                    << ",\"subblock_reclaims\":" << d.reclaims
                    << ",\"subblock_reclaim_helped\":" << d.reclaim_helped
                    << ",\"compact_entered\":" << d.compact_entered
                    << ",\"compact_helped\":" << d.compact_helped
                    << ",\"compact_no_help\":" << d.compact_no_help
                    << ",\"whole_group_garbage\":" << d.whole_group_garbage
                    << ",\"compact_plan_rejected\":" << d.compact_plan_rejected
                    << ",\"overflow_inserts\":" << d.overflow_inserts
                    << ",\"insert_attempts\":" << d.insert_attempts
                    << ",\"repack_guarded\":" << d.repack_guarded
                    << ",\"nholes_0\":" << d.n0
                    << ",\"nholes_1\":" << d.n1
                    << ",\"nholes_2plus\":" << d.n2
                    << ",\"best_eq_garbage\":" << d.best_eq_garbage
                    << ",\"count_lt2\":" << d.count_lt2
                    << ",\"slots_not_free\":" << d.slots_not_free
                    << "}" << std::endl;
        };

        // ---------------- W1: long replacement to 200% ----------------
        // The candidate's own regime: erase cuts dead ranges, insert wants them back.
        {
            Pipeline plan;
            {
                Index probe(cfg);
                probe.bulk_load(keys);
                std::vector<std::vector<GroupStats>> demand;

                for (std::size_t i = 0; i < probe.region_count(); ++i)
                    demand.push_back(probe.region(i).group_stats());

                plan = make_plan(demand);
            }

            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;

            for (const auto& kv : keys) {
                live.push_back(kv.first);
                live_set.insert(kv.first);
            }

            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

            // The load is not an insert workload. Anything it recorded would otherwise be
            // reported as if the replacement had produced it.
            s->diag_clear_hole_events();

            const Snap a = snapshot();
            const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
            std::size_t done = 0;
            std::size_t total_rows = 0;
            std::uint64_t serial = 0;

            for (int ci = 0; ci < 4; ++ci) {
                const double frac = CPS[ci];
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                const Snap seg_a = snapshot();

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

                if (s->size() != live.size())
                    throw std::runtime_error("W1 live-count drift at frac " +
                                             std::to_string(frac));

                const Snap seg_b = snapshot();
                const std::size_t rows = flush_events(*s, "replace", frac);
                total_rows += rows;
                report("replace", frac, diff(seg_a, seg_b), rows);
            }

            report("replace_total", 2.00, diff(a, snapshot()), total_rows);
        }

        // ---------------- W2: pure insert ----------------
        // Control. The even-indexed half is loaded, the odd half is inserted: real keys in
        // real key ranges, so the insert stream is not skewed to the tail of the region
        // order. Nothing is erased, so ALMOST NO DEAD RANGES ARE CUT -- which is exactly
        // what makes it the control for "is the match rate a property of the workload".
        {
            std::vector<Pair> loaded;
            std::vector<std::pair<std::string, std::uint64_t>> pending;

            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (i % 2 == 0) loaded.push_back(keys[i]);
                else pending.emplace_back(keys[i].first, keys[i].second);
            }

            Pipeline plan;
            {
                Index probe(cfg);
                probe.bulk_load(loaded);
                std::vector<std::vector<GroupStats>> demand;

                for (std::size_t i = 0; i < probe.region_count(); ++i)
                    demand.push_back(probe.region(i).group_stats());

                plan = make_plan(demand);
            }

            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(loaded, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            s->diag_clear_hole_events();

            const Snap a = snapshot();

            for (const auto& kv : pending) s->insert(kv.first, kv.second);

            if (s->size() != keys.size())
                throw std::runtime_error("W2 live-count drift: " +
                                         std::to_string(s->size()));

            const Snap b = snapshot();
            const std::size_t rows = flush_events(*s, "insert", 1.0);
            report("insert", 1.0, diff(a, b), rows);
        }


        // ---------------- W3: replacement with a PERMUTED erase order ----------------
        // W1 erases victims in key order (`live.front()`), so it always removes the lowest
        // key -- which is the lowest-offset record of whatever block it falls in. That is a
        // property of THIS DRIVER, not of the layout, and it is what makes the dead region a
        // single run: erase_at drops a descriptor without lowering h.bottom, so erasing a
        // record that is not topmost leaves a gap that is NOT adjacent to bb_, and
        // Header::bottom (a low-water mark) cannot tell a leading gap from a trailing one.
        // So W1 was measuring its own erase order. This arm permutes it, with a fixed seed so
        // the run stays reproducible.
        //
        // The counterfactual question it answers: does the "one contiguous run" structure
        // survive when the victims are not removed top-down?
        {
            Pipeline plan;
            {
                Index probe(cfg);
                probe.bulk_load(keys);
                std::vector<std::vector<GroupStats>> demand;

                for (std::size_t i = 0; i < probe.region_count(); ++i)
                    demand.push_back(probe.region(i).group_stats());

                plan = make_plan(demand);
            }

            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            s->diag_clear_hole_events();

            // A REFILLABLE pool of victims, reshuffled whenever it runs out. A single
            // permutation of the N key positions cannot serve a run that erases 2N times --
            // indexing it past N is what segfaulted this arm on its first attempt. Each pass
            // takes the currently-live keys, shuffles them, and erases them in that order;
            // the keys it inserts become victims of a later pass.
            std::unordered_set<std::string> live_set;

            for (const auto& kv : keys) live_set.insert(kv.first);

            std::mt19937_64 rng(20260927);
            std::vector<std::string> victims;
            std::size_t next = 0;

            const auto refill = [&]() {
                victims.assign(live_set.begin(), live_set.end());
                std::shuffle(victims.begin(), victims.end(), rng);
                next = 0;
            };

            refill();

            const Snap a = snapshot();
            const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
            std::uint64_t serial = 0;
            std::size_t done = 0;

            for (int ci = 0; ci < 4; ++ci) {
                const double frac = CPS[ci];
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                const Snap seg_a = snapshot();

                for (std::size_t i = done; i < target; ++i) {
                    if (next >= victims.size()) refill();

                    const std::string victim = victims[next++];
                    live_set.erase(victim);

                    // Strip OUR OWN suffix marker, not W1's: the victims of a later pass are
                    // keys this arm inserted, and "~r" would never appear on them.
                    std::string base = victim;
                    const auto pos = base.find("~z");

                    if (pos != std::string::npos) base.resize(pos);

                    std::string fresh = base + "~z" + std::to_string(serial++);

                    while (!live_set.insert(fresh).second) fresh += "x";

                    s->erase(victim);
                    s->insert(fresh, 0x9000ULL + serial);
                }

                done = target;

                if (s->size() != live_set.size())
                    throw std::runtime_error("W3 live-count drift at frac " +
                                             std::to_string(frac));

                const Snap seg_b = snapshot();
                const std::size_t rows = flush_events(*s, "permuted", frac);
                report("permuted", frac, diff(seg_a, seg_b), rows);
            }

            report("permuted_total", 2.00, diff(a, snapshot()), 0);
        }

        std::cout << summary.str();


        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

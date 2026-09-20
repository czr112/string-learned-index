// E31 item 3 -- the checklist's section 5 PRECONDITION, measured instead of assumed.
//
// The checklist forbids choosing between O1/O2/O3 until there is evidence that the
// ordered overflow vector's movement / construction / allocation is a large share of
// maintenance time. That share was never isolated: what E28-E31 measured is how OFTEN a
// replacement lands in overflow and what reclaim does to that count, not what the
// container costs. The container is still `std::vector<OverflowRecord>` with a
// std::lower_bound plus a vector::insert of a constructed std::string, and an erase that
// moves every record above the hole.
//
// So this program times the container from the INSIDE, with the fields added to the E30
// timer block for it (ov_insert_ns/ov_insert_calls around put_overflow's lower_bound +
// shift + std::string construction; ov_erase_ns/ov_erase_calls around erase_at's vector
// erase + destruction) and counts its movement with the E25 counters
// (ov_shifted_records_insert/bytes, ov_shifted_records_erase/bytes). Movement is a number
// of bytes here, not an adjective.
//
// THIS IS A SHARE-OF-COST DIAGNOSTIC, NOT A SPEEDUP CLAIM. Counters and clocks are
// compiled in; every figure describes THIS build's decomposition of its own maintenance
// span. No wall-clock comparison against the flag-free binary is made or implied, and
// none should be quoted from this file.
//
// THE ONE NUMBER IT EXISTS TO PRODUCE is ov_share_of_maintenance: the fraction of the
// timed replacement work that runs inside the overflow container. That fraction is the
// CEILING on what any overflow container can save -- a container that moved, constructed
// and allocated nothing would recover at most that much, and O2/O3 recover less, because
// they pay for a merge that O1 does not.
//
// WORKLOAD: identical to bench_long.cpp -- same datasets, same live-key ring, the same
// "~r" trim so a replacement keeps its key length and its rank position, the same
// 25/50/100/200% checkpoints, both modes, the frozen-version configuration. The one
// deliberate difference is that the erase pass and the insert pass are clocked apart, so
// each share divides by the whole of the operation it belongs to; their sum is the
// maintenance total. That difference is why a number from here is not a bench_long
// reproduction of anything.
//
// ONE THING TO EXPECT, found by the pilot run and flagged in the output so it cannot be
// misread. ov_shifted_records_insert comes out EXACTLY ZERO on every dataset and mode
// tried (0 of 14,644 inserts at the 200% checkpoint of mixed/same). That is a
// measurement, not a dead counter: put_overflow is entered (ov_insert_calls is counted at
// the same site) and the byte figure is the same expression times sizeof(OverflowRecord),
// so a zero sum says every one of those inserts landed at the vector's END. The reason is
// the workload: the live-key ring is a sorted FIFO, so the anchors -- and therefore the
// fresh keys -- arrive in increasing key order and each new key is the largest in its
// container. ALL of the container's movement is therefore on the ERASE side, which
// removes from the front of the ring and so from near the front of the container.
// `ov_insert_all_appends` states that case in the record itself.
//
// That asymmetry is why the two sides are reported apart: on THIS workload a container
// replacement buys nothing on the insert side and buys the memmove on the erase side,
// while a workload that inserts in RANDOM key order would move on the insert side too.
// The movement and the time are reported separately because the erase clock covers the
// memmove AND the destruction of the erased record, and the two have different
// signatures across datasets (mixed moves hundreds of records per erase, skew moves tens
// but frees a long heap string each time) -- a replacement has to address whichever of
// the two actually dominates, and this program reports enough to tell which it is.
//
// Build with BOTH -DE25_DIAG and -DE30_TIME (the #error below states why each is needed).
// Registered in tools_build.py exactly like bench_repack.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <chrono>
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
#include <unordered_set>
#include <vector>

#ifdef __linux__
#include <sched.h>
#endif

#ifndef E25_DIAG
#error "diag_ov_cost needs -DE25_DIAG: the overflow movement counters and the container-size accessor are declared under it, and with the macro off the report would be a column of zeros that reads like 'the overflow container is free'."
#endif
#ifndef E30_TIME
#error "diag_ov_cost needs -DE30_TIME: the overflow clocks live in the E30 timer block (E30_T0/E30_ADD), and without the macro E30_T0() expands to 0 and the timer fields do not exist."
#endif

using namespace validation;
using namespace validation::e9alloc;

using Clock = std::chrono::steady_clock;
using Reference = std::map<std::string, std::uint64_t, Less>;

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
// A share is 0 when its denominator is 0, NOT NaN and NOT a huge ratio: an empty timed
// span is missing evidence, and a NaN would propagate into the analysis instead of
// showing up as "nothing was measured here".
static double share_of(double part_ms, double whole_ms) {
    return whole_ms > 0 ? part_ms / whole_ms : 0.0;
}

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
#ifdef __linux__
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 4) throw std::invalid_argument("diag_ov_cost KIND N REPS [TRANCOCSV] [RUNTAG] [MODE]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argc > 4 ? argv[4] : "";
        const std::string run_tag = argc > 5 ? argv[5] : "";
        const std::string mode = argc > 6 ? argv[6] : "same";
        if (mode != "grow" && mode != "same")
            throw std::invalid_argument("MODE must be grow or same");
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

        // The FROZEN configuration (2026-09-20), unchanged. The precondition is about the
        // overflow vector of the version the round SHIPS, not a variant of it, so the arm
        // studied is the shipped one and nothing is flipped to make the container look
        // better or worse.
        Config cfg = e25sw::baseline_config(thr);
        cfg.write_fast_locate = true;
        cfg.reuse_locate_ctx = true;
        cfg.scan_reserve_result = true;
        cfg.compact_reuse_tag = true;
        cfg.compact_needs_garbage = true;
        // Printed AND recorded, so a reader never has to infer which configuration
        // produced a line.
        std::cerr << "diag_ov_cost switches " << e25sw::switches(cfg) << std::endl;

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        Pipeline plan;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        std::ofstream out("results/main/ov_cost_diag.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        // Container-size thresholds. The overflow container is meant to hold the handful of
        // records a refused placement could not take; a container above a few dozen records
        // is a second index growing inside a group. Whether the mass sits at "one or two"
        // or at "hundreds" is what decides whether O1's ordered array or a tree-shaped
        // container is even the right question, so both ends are reported.
        const std::size_t THRESH[3] = {8, 32, 128};
        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        for (int r = 0; r < reps; ++r) {
            // Live-key ring: deque front is the next delete target, the new key is
            // appended. The live count is constant at N.
            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;
            Reference ref;
            for (const auto& kv : keys) { live.push_back(kv.first); ref[kv.first] = kv.second; live_set.insert(kv.first); }

            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

            // The counters and the clocks are process-global and cumulative, and the bulk
            // load sends records to overflow too -- the Region constructor places the whole
            // sorted input through the same put_overflow. Reset AFTER the load, so every
            // figure below is the replacement workload's own work and nothing else. This
            // differs from bench_long, which resets BEFORE the load because it wants
            // construction folded into its compaction totals.
            validation::e25::counts().reset();
            validation::e30::timers().reset();

            const std::size_t live0 = s->size();
            std::size_t done = 0;
            double cum_erase = 0, cum_insert = 0;
            std::uint64_t serial = 0;

            for (double frac : CPS) {
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                if (target <= done) continue;

                // Reference upkeep and key generation OUTSIDE the timed span. The rules are
                // bench_long's verbatim -- the victim is replaced IN ITS OWN PLACE, its
                // previous generated suffix stripped and a fresh one appended, because an
                // anchored replacement keeps the key length and the key-space position and
                // therefore measures the index's behaviour rather than a drifting key
                // population. See that file for the two earlier versions that failed this.
                struct Move { std::string victim, fresh; std::uint64_t value; };
                std::vector<Move> seg;
                seg.reserve(target - done);
                for (std::size_t i = done; i < target; ++i) {
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
                        // Same length, same anchor: change one byte until the key is one
                        // the reference does not already hold, so any space growth is
                        // churn and not inflation.
                        fresh = base;
                        bool ok = false;
                        for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                            fresh.back() = static_cast<char>('a' + ((serial + t) % 26));
                            if (fresh != victim && ref.find(fresh) == ref.end() &&
                                live_set.find(fresh) == live_set.end()) { ok = true; break; }
                        }
                        if (!ok) throw std::runtime_error("same-length replacement exhausted its alphabet");
                    }
                    const std::uint64_t val = 0x7000ULL + serial;
                    ++serial;
                    seg.push_back({victim, fresh, val});
                    live.push_back(fresh);
                    live_set.insert(fresh);
                }
                for (const auto& m : seg) {
                    if (ref.erase(m.victim) != 1) throw std::runtime_error("erase target absent");
                    if (ref.find(m.fresh) != ref.end()) throw std::runtime_error("insert target present");
                    ref[m.fresh] = m.value;
                }

                // The two passes are clocked apart so the insert share and the erase share
                // each divide by the whole of their own operation. This is bench_long's
                // single span split by one extra clock read; the sum is the maintenance
                // total the overflow share is taken against.
                const auto ae = Clock::now();
                for (const auto& m : seg) s->erase(m.victim);
                cum_erase += ms_between(ae, Clock::now());

                const auto ai = Clock::now();
                for (const auto& m : seg) s->insert(m.fresh, m.value);
                cum_insert += ms_between(ai, Clock::now());
                done = target;

                if (s->size() != ref.size())
                    throw std::runtime_error("live count drifted from the reference");
                // Spot-check the index against the reference, so a decomposition of a
                // silently-nonfunctional workload is impossible.
                for (std::size_t i = 0; i < seg.size(); i += std::max<std::size_t>(1, seg.size() / 16)) {
                    const Result res = s->find(seg[i].fresh, qmode);
                    if (!res.found || res.value != seg[i].value)
                        throw std::runtime_error("spot check failed after replacement");
                }

                // Container sizes and all counters are read AFTER the clock stops.
                std::vector<std::size_t> sizes;
                s->diag_overflow_sizes(sizes);

                // E33/O1: the same walk, but lengths instead of counts. sso_char_bytes is
                // the character data that rides INSIDE the 48-byte record (SSO) and therefore
                // moves with it on every erase; heap_char_bytes stays put. The first number
                // is the ceiling on what moving handles instead of records can save.
                std::vector<std::size_t> lens;
                s->diag_overflow_suffix_lengths(lens);
                std::uint64_t len_sum = 0, len_max = 0, sso = 0;
                std::uint64_t sso_char_bytes = 0, heap_char_bytes = 0;
                for (std::size_t v : lens) {
                    len_sum += v;
                    if (v > len_max) len_max = v;
                    if (v <= 15) {
                        ++sso;
                        sso_char_bytes += v;
                    } else {
                        heap_char_bytes += v;
                    }
                }
                const std::uint64_t heap_strings = std::uint64_t(lens.size()) - sso;
                std::uint64_t sz_sum = 0, sz_max = 0, ge[3] = {0, 0, 0};
                for (std::size_t v : sizes) {
                    sz_sum += v;
                    if (v > sz_max) sz_max = v;
                    for (int t = 0; t < 3; ++t) if (v >= THRESH[t]) ++ge[t];
                }

                const auto& C = validation::e25::counts();
                const auto& T = validation::e30::timers();
                // Both are put_overflow entry counters -- string_allocs is incremented at
                // exactly one site, the one the overflow clock wraps. The instrument
                // therefore checks itself: if they disagree, the clock was placed at a
                // different call site than the counter and every share below is
                // meaningless rather than merely imprecise.
                if (C.string_allocs != T.ov_insert_calls)
                    throw std::runtime_error("instrument disagrees: string_allocs != ov_insert_calls");

                const double ins_ms = cum_insert, ers_ms = cum_erase;
                const double maint_ms = ins_ms + ers_ms;
                const double ov_i_ms = double(T.ov_insert_ns) / 1e6;
                const double ov_e_ms = double(T.ov_erase_ns) / 1e6;

                out << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode
                    << "\",\"run_tag\":\"" << run_tag << "\",\"rep\":" << r
                    << ",\"frac\":" << frac
                    << ",\"cycles\":" << done
                    << ",\"live\":" << s->size()
                    << ",\"live0\":" << live0
                    << ",\"maintenance_ms\":" << maint_ms
                    << ",\"erase_ms\":" << ers_ms
                    << ",\"insert_ms\":" << ins_ms
                    << ",\"ov_insert_ms\":" << ov_i_ms
                    << ",\"ov_insert_calls\":" << T.ov_insert_calls
                    << ",\"ov_erase_ms\":" << ov_e_ms
                    << ",\"ov_erase_calls\":" << T.ov_erase_calls
                    << ",\"ov_ns_per_insert\":" << (T.ov_insert_calls ? double(T.ov_insert_ns) / double(T.ov_insert_calls) : 0.0)
                    << ",\"ov_ns_per_erase\":" << (T.ov_erase_calls ? double(T.ov_erase_ns) / double(T.ov_erase_calls) : 0.0)
                    << ",\"ov_shifted_records_insert\":" << C.ov_shifted_records_insert
                    << ",\"ov_shifted_bytes_insert\":" << C.ov_shifted_bytes_insert
                    // 1 when the summed insert shift distance is zero, i.e. EVERY overflow
                    // insert at this checkpoint appended at the container's end. Stated in
                    // the record because a zero movement counter otherwise reads like an
                    // instrument that never ran -- it is proven live by ov_insert_calls,
                    // counted at the same site.
                    << ",\"ov_insert_all_appends\":" << (C.ov_shifted_records_insert == 0 ? 1 : 0)
                    << ",\"ov_shifted_records_erase\":" << C.ov_shifted_records_erase
                    << ",\"ov_shifted_bytes_erase\":" << C.ov_shifted_bytes_erase
                    << ",\"ov_rec_bytes\":" << s->diag_overflow_record_bytes()
                    << ",\"ov_suffix_len_mean\":" << (lens.empty() ? 0.0 : double(len_sum) / double(lens.size()))
                    << ",\"ov_suffix_len_max\":" << len_max
                    << ",\"ov_suffix_sso_inline\":" << sso
                    << ",\"ov_suffix_heap\":" << heap_strings
                    << ",\"ov_sso_char_bytes\":" << sso_char_bytes
                    << ",\"ov_heap_char_bytes\":" << heap_char_bytes
                    << ",\"ov_bytes_shifted_per_insert\":" << (T.ov_insert_calls ? double(C.ov_shifted_bytes_insert) / double(T.ov_insert_calls) : 0.0)
                    << ",\"ov_bytes_shifted_per_erase\":" << (T.ov_erase_calls ? double(C.ov_shifted_bytes_erase) / double(T.ov_erase_calls) : 0.0)
                    << ",\"e30_erase_ms\":" << double(T.erase_ns) / 1e6
                    << ",\"e30_erase_calls\":" << T.erase_calls
                    << ",\"erase_minus_ov_ms\":" << (double(T.erase_ns) - double(T.ov_erase_ns)) / 1e6
                    << ",\"e30_compact_ms\":" << double(T.compact_ns) / 1e6
                    << ",\"e30_compact_calls\":" << T.compact_calls
                    << ",\"e30_collect_ms\":" << double(T.collect_ns) / 1e6
                    << ",\"string_allocs\":" << C.string_allocs
                    << ",\"string_frees\":" << C.string_frees
                    << ",\"overflow_total\":" << s->overflow_count()
                    << ",\"ov_containers\":" << sizes.size()
                    << ",\"ov_size_mean\":" << (sizes.empty() ? 0.0 : double(sz_sum) / double(sizes.size()))
                    << ",\"ov_size_max\":" << sz_max
                    << ",\"ov_containers_ge_8\":" << ge[0]
                    << ",\"ov_containers_ge_32\":" << ge[1]
                    << ",\"ov_containers_ge_128\":" << ge[2]
                    // The three shares. ov_share_of_maintenance is THE number: the most of
                    // maintenance an ideal overflow container could ever save.
                    << ",\"ov_insert_share\":" << share_of(ov_i_ms, ins_ms)
                    << ",\"ov_erase_share\":" << share_of(ov_e_ms, ers_ms)
                    << ",\"ov_share_of_maintenance\":" << share_of(ov_i_ms + ov_e_ms, maint_ms)
                    << ",\"switches\":\"" << e25sw::switches(cfg) << "\""
                    << "}" << std::endl;

                std::cerr << kind << " " << mode << " rep" << r << " frac=" << frac
                          << " ov_share_of_maintenance " << share_of(ov_i_ms + ov_e_ms, maint_ms)
                          << " (insert " << share_of(ov_i_ms, ins_ms)
                          << ", erase " << share_of(ov_e_ms, ers_ms) << ")"
                          << " containers " << sizes.size() << " max " << sz_max << std::endl;
                if (C.ov_shifted_records_insert == 0 && T.ov_insert_calls > 0)
                    std::cerr << "  note: all " << T.ov_insert_calls
                              << " overflow inserts appended -- the insert-side movement is "
                                 "genuinely zero (the ring delivers fresh keys in increasing "
                                 "order), so read the erase side for movement." << std::endl;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

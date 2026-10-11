// Long-run replacement with the E31H arm: hysteresis on the E31 reclaim trigger.
//
// Derived from bench_long_art.cpp, which produced results/main/long_e31art_<mode>.jsonl
// with five arms. That file and its build record are left exactly as they were -- the
// record has to keep describing the source that produced those records. This program asks
// a different question (checklist item 4.3: does a minimum operation interval plus a
// "garbage must have grown" condition on the reclaim trigger remove E31's wasted passes?)
// and writes its own file:
//
//   results/main/long_hyst_<mode>.jsonl
//
// FOUR arms:
//   C2    tag reuse + the "cannot free enough bytes" trigger gate       <- the FROZEN version
//   E31   C2 + in-place sub-block reclaim (default off; the E31 candidate)
//   E31H  E31 + reclaim_hysteresis (this program's candidate)
//   ART   libart, the standing baseline -- NOT a Config switch, so it carries no
//         structural columns (libart exposes no overflow/bytes accounting here) and
//         its index bytes are reported as 0 with that meaning stated in the report.
//
// The arm-order rotation of bench_long_art.cpp is kept, so no arm is systematically first
// or last, and the switch string printed by the gate machinery shows the effective
// configuration of every arm.
//
// I10 CLOSING ROUND: the old compaction path against the safe one, paired.
//
// The per-operation latency distribution below (p50/p99/p99.9/max) is inherited
// unchanged from bench_long_hyst.cpp -- it is why this template was chosen. The plan's
// cost is an extra O(n * nb) pass over a group's live records, so it should show up in
// the tail and under high compaction frequency rather than in the mean.
//
// INHERITED, unchanged (checklist item 9, the high-percentile stall gap): the maintenance
// operations are timed INDIVIDUALLY inside the timed span, so the record carries the
// per-operation latency distribution -- p50/p99/p99.9 and the max -- appended after the
// fields bench_long_art.cpp already writes. The clock reads this adds are paid identically
// by every arm, which keeps the arm-to-arm RATIO usable; the absolute cum_ms is therefore
// NOT comparable against long_e31art_*.jsonl, whose build has no per-operation timing.
//
// WORKLOAD DEFINITION is bench_long_art.cpp's, unchanged: a live-key ring keeps the live
// count at ~N and cycles are unbounded, so a run out to 200% of N is possible.
//
// With -DE25_DIAG the same source also reports the reclaim-skip counters, so the hysteresis
// is countable, and asserts at the end of the E31H arm that it actually skipped a reclaim --
// a gate that passes without executing its branch proves nothing (the E31 round's lesson).

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "native_e22.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_set>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
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
using Reference = std::map<std::string, std::uint64_t, Less>;

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
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
        if (argc < 4) throw std::invalid_argument("bench_long KIND N REPS [TRANCOCSV] [RUNTAG] [MODE]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argc > 4 ? argv[4] : "";
        const std::string run_tag = argc > 5 ? argv[5] : "";
        // MODE controls whether the replacement key is the same length as the key it
        // replaces. "same" isolates churn; "grow" adds a bounded suffix and therefore
        // also drifts the key-length distribution. Both are reported, because only the
        // same-length run can say whether SPACE fails to stabilise under pure churn.
        const std::string mode = argc > 6 ? argv[6] : "grow";
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

        Config base = e25sw::baseline_config(thr);
        base.write_fast_locate = true;
        base.reuse_locate_ctx = true;
        base.scan_reserve_result = true;
        // ART is libart, not a Config, so cfg stays three wide and the ART arm is taken
        // by index in the loop below.
        // Two Index arms plus libart. The SINGLE variable between them is the I10 switch,
        // and that is asserted below, so an arm that silently became a copy of the other
        // fails the run instead of passing a comparison that never happened.
        //
        // OLD is the shipped compaction: rebuild_dir() runs AFTER the replay and after
        // dir.clear(), which is where the commit phase can be interrupted.
        // NEW is the same algorithm with the plan: the placement decisions, the capacity
        // proof and the directory are all produced BEFORE anything in the group moves.
        // The work is the same; only WHEN it happens changes -- which is why this is a
        // timing question about a tail, not about a different data structure.
        Config cfg[2] = {base, base};
        cfg[0].compact_reuse_tag = true;
        cfg[0].compact_needs_garbage = true;
        // EXPLICIT, not inherited: baseline_config now ships the fix, so an arm that
        // just took the baseline would silently BE the NEW arm and the assertion below
        // would fail -- which is exactly how this line was found.
        cfg[0].compact_plan_commit = false;                    // OLD = the pre-I10 commit
        cfg[1].compact_reuse_tag = true;
        cfg[1].compact_needs_garbage = true;
        cfg[1].compact_plan_commit = true;                     // NEW = shipped + the plan
        // The gate this round asks for: E31 and E31H must differ in EXACTLY
        // reclaim_hysteresis, and C2 and E31 in EXACTLY subblock_reclaim. A switch missing
        // from the field table in switches.hpp would make either assertion pass vacuously --
        // the failure this project has hit before -- so both are registered here, against the
        // one field table every gate shares.
        e25sw::require_only_switches_differ(cfg[0], cfg[1], {"compact_plan_commit"});
        const char* names[3] = {"OLD", "NEW", "ART"};
        // ART is not a Config: the two require_only_switches_differ calls above pin the
        // three Index arms against each other, and the ART arm never reads cfg.
        const int ARMS = 3;
        const int ART_ARM = 2;

        Pipeline plan;
        {
            Index probe(base);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        // A fixed query sequence, timed at each checkpoint. The C3 verdict (E29) was
        // reached on MAINTENANCE time alone: reclaiming records into the primary should
        // also make queries cheaper, and that side was never measured. Without it the
        // payback accounting is one-sided.
        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};
        std::vector<std::string> qs;
        {
            std::mt19937_64 rng(2026092003);
            const std::size_t n = std::min<std::size_t>(16384, N);
            for (std::size_t i = 0; i < n; ++i) qs.push_back(keys[rng() % N].first);
        }

        // Checkpoints as a fraction of N cycles.
        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        // Its own file, so the records produced by the other binaries (long_e31_*.jsonl,
        // long_e31art_*.jsonl, both kept as they are) are never mixed with this one's.
        // APPEND, not truncate: the sweep runs independent processes, each appending its
        // own records, so truncation would leave only the last process's. The driver
        // (run_hyst.py) removes the file before the sweep, as run_e25.py does.
        // The diagnostic build of this same source must not append into the timing
        // file: one paired file cannot hold two binaries. It writes its own, which is
        // also what keeps a reader from mistaking counter records for timings.
#ifdef E25_DIAG
        std::ofstream out(("results/main/long_i10_diag_" + mode + ".jsonl").c_str(),
                          std::ios::app);
#else
        std::ofstream out(("results/main/long_i10_" + mode + ".jsonl").c_str(),
                          std::ios::app);
#endif
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (int r = 0; r < reps; ++r) {
            // Rotate which arm goes first, so no arm is systematically first or last.
            int order[3] = {r % ARMS, (r + 1) % ARMS, (r + 2) % ARMS};
            for (int oi = 0; oi < ARMS; ++oi) {
                const int arm = order[oi];
                const bool is_art = (arm == ART_ARM);
                // Live-key ring: deque front is the next delete target, the new key is
                // appended. The live count is constant at N.
                std::deque<std::string> live;
                std::unordered_set<std::string> live_set;
                Reference ref;
                for (const auto& kv : keys) { live.push_back(kv.first); ref[kv.first] = kv.second; live_set.insert(kv.first); }

                double build_ms = 0;
                std::unique_ptr<Index> s;
                std::unique_ptr<e22art::NativeArt> art;
                if (is_art) {
                    art = std::make_unique<e22art::NativeArt>(keys);
                } else {
                    s = std::make_unique<Index>(cfg[arm]);
#ifdef E25_DIAG
                // The counters are process-global and this loop runs one arm after the
                // other, so without this reset arm C2's figures would be C1's PLUS its
                // own -- which made C2 look like it replayed 35x more records than C1.
                // Reset per arm; the figures are then that arm's own cumulative work.
                validation::e25::counts().reset();
#endif
                    const auto b0 = Clock::now();
                    s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
                    build_ms = ms_between(b0, Clock::now());
                }

                const std::size_t live0 = is_art ? art->size() : s->size();
                std::size_t done = 0;
                double cum = 0;
                std::uint64_t serial = 0;

                for (double frac : CPS) {
                    const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                    if (target <= done) continue;

                    // Reference upkeep and key generation OUTSIDE the timed span.
                    struct Move { std::string victim, fresh; std::uint64_t value; };
                    std::vector<Move> seg;
                    seg.reserve(target - done);
                    for (std::size_t i = done; i < target; ++i) {
                        const std::string victim = live.front();
                        live.pop_front();
                        live_set.erase(victim);
                        // Anchor the new key on an ORIGINAL key, not on the victim. The
                        // first version built `victim + "~r" + serial`, which makes keys
                        // grow without bound as they are replaced again and again -- the
                        // run then measures an ever-longer key distribution rather than
                        // the index's behaviour under replacement. Anchoring keeps key
                        // lengths bounded, which is what "delete and insert a different
                        // new key" means here.
                        // Replace the victim IN ITS OWN PLACE: strip any previous
                        // generated suffix and append a fresh one. The key therefore
                        // keeps its length and its position in the key space, and the
                        // distribution does not drift away from the initial plan.
                        // Two earlier versions failed this: `victim + "~r" + serial`
                        // grew keys without bound, and anchoring on a rotating original
                        // key moved the distribution -- both made the run measure a
                        // changing key population instead of the index's behaviour.
                        std::string base = victim;
                        const auto pos = base.find("~r");
                        if (pos != std::string::npos) base.resize(pos);
                        std::string fresh;
                        if (mode == "grow") {
                            fresh = base + "~r" + std::to_string(serial);
                        } else {
                            // Same length, same anchor: change one byte until the key is
                            // one the reference does not already hold. This keeps the
                            // key-length distribution exactly as planned, so any space
                            // growth is churn and not inflation.
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

                    // E31H (checklist item 9): the maintenance operations are timed one by
                    // one, so the record carries the latency distribution and not only the
                    // segment mean. The reservoir is reserved BEFORE the span, so no
                    // allocation happens inside it. The clock reads are paid by every arm
                    // identically, which is what keeps the arm-to-arm RATIO usable; the
                    // absolute cum_ms is therefore not comparable to a build without the
                    // per-operation timing (long_e31art_*.jsonl).
                    std::vector<std::uint64_t> op_ns;
                    op_ns.reserve(2 * seg.size());
                    const auto a = Clock::now();
                    for (const auto& m : seg) {
                        if (is_art) {
                            const auto t0 = Clock::now();
                            art->erase(m.victim);
                            const auto t1 = Clock::now();
                            art->upsert(m.fresh, m.value);
                            const auto t2 = Clock::now();
                            op_ns.push_back(static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
                            op_ns.push_back(static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()));
                        } else {
                            const auto t0 = Clock::now();
                            s->erase(m.victim);
                            const auto t1 = Clock::now();
                            s->insert(m.fresh, m.value);
                            const auto t2 = Clock::now();
                            op_ns.push_back(static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
                            op_ns.push_back(static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()));
                        }
                    }
                    cum += ms_between(a, Clock::now());
                    done = target;

                    // Sorted once, here, OUTSIDE the timed span.
                    const std::size_t op_samples = op_ns.size();
                    std::uint64_t op_p50 = 0, op_p99 = 0, op_p999 = 0, op_max = 0;
                    if (!op_ns.empty()) {
                        std::sort(op_ns.begin(), op_ns.end());
                        const auto pick = [&op_ns](double q) {
                            return op_ns[std::size_t(q * double(op_ns.size() - 1))];
                        };
                        op_p50 = pick(0.50);
                        op_p99 = pick(0.99);
                        op_p999 = pick(0.999);
                        op_max = op_ns.back();
                    }

                    // The live count is checked against the reference for EVERY arm,
                    // including ART: without it the only evidence that ART kept up
                    // would be the reference's own bookkeeping.
                    if ((is_art ? art->size() : s->size()) != ref.size())
                        throw std::runtime_error("live count drifted from the reference");

                    // Query the state that the segment just produced. Timed here, and
                    // recorded separately from the update span.
                    std::uint64_t q_acc = 0;
                    const auto q0 = Clock::now();
                    for (const auto& k : qs) {
                        const Result r = is_art ? art->find(k) : s->find(k, qmode);
                        q_acc += r.value + r.found;
                    }
                    const double q_ms = ms_between(q0, Clock::now());
                    // Spot-check that the index really holds what the reference says,
                    // so a timing on a silently-nonfunctional workload is impossible.
                    for (std::size_t i = 0; i < seg.size(); i += std::max<std::size_t>(1, seg.size() / 16)) {
                        const Result res = is_art
                            ? art->find(seg[i].fresh)
                            : s->find(seg[i].fresh,
                                Mode{Route::Interval, Prefetch::Off, true, true,
                                     OvSearch::Hybrid, thr, false});
                        if (!res.found || res.value != seg[i].value)
                            throw std::runtime_error("spot check failed after replacement");
                    }

                    // ART carries no structural columns: libart exposes no overflow,
                    // compaction or byte accounting to this adapter. They are written as
                    // 0 and the report states that 0 means "not applicable", never
                    // "zero", for the ART arm.
                    const std::size_t st_overflow = is_art ? 0 : s->overflow_count();
                    const std::size_t st_compactions = is_art ? 0 : s->compaction_count();
                    const std::size_t st_memory = is_art ? 0 : s->memory();
                    const auto mb = is_art ? Index::MemBreakdown{} : s->memory_breakdown();
#ifdef E25_DIAG
                    const auto& C = validation::e25::counts();
                    const std::uint64_t replayed = C.compact_records_replayed;
                    const std::uint64_t copied = C.compact_bytes_copied;
                    const std::uint64_t garbage = C.compact_garbage_before;
                    const std::uint64_t helped = C.compact_helped;
                    const std::uint64_t skipped_gap = C.reclaim_skipped_gap;
                    const std::uint64_t skipped_unchanged = C.reclaim_skipped_unchanged;
#else
                    const std::uint64_t replayed = 0, copied = 0, garbage = 0, helped = 0;
                    const std::uint64_t skipped_gap = 0, skipped_unchanged = 0;
#endif
                    out << "{\"kind\":\"" << kind << "\",\"rep\":" << r
                        << ",\"arm\":\"" << names[arm] << "\",\"run_tag\":\"" << run_tag << "\""
                        << ",\"mode\":\"" << mode << "\",\"frac\":" << frac
                        << ",\"cycles\":" << done
                        << ",\"live\":" << (is_art ? art->size() : s->size())
                        << ",\"live0\":" << live0
                        << ",\"cum_ms\":" << cum
                        << ",\"build_ms\":" << build_ms
                        << ",\"overflow\":" << st_overflow
                        << ",\"compactions\":" << st_compactions
                        << ",\"memory\":" << st_memory
                        << ",\"blocks\":" << mb.blocks
                        << ",\"ov_bytes\":" << mb.overflow
                        << ",\"replayed\":" << replayed
                        << ",\"copied\":" << copied
                        << ",\"garbage\":" << garbage
                        << ",\"query_ms\":" << q_ms
                        << ",\"query_acc\":" << q_acc
                        << ",\"helped\":" << helped
                        // --- appended fields (E31H): new values go at the END, so a reader
                        // that stops at "helped" still parses this record.
                        << ",\"op_samples\":" << op_samples
                        << ",\"op_p50_ns\":" << op_p50
                        << ",\"op_p99_ns\":" << op_p99
                        << ",\"op_p999_ns\":" << op_p999
                        << ",\"op_max_ns\":" << op_max
                        << ",\"reclaim_skipped_gap\":" << skipped_gap
                        << ",\"reclaim_skipped_unchanged\":" << skipped_unchanged
                        << "}" << std::endl;
                }
#ifdef E25_DIAG
                // E31's lesson, applied here: a gate that passes without executing the
                // branch proves nothing. With the hysteresis on, a repeat attempt must have
                // been skipped, or this program measured the E31 arm twice.
                if (!is_art && (cfg[arm].reclaim_hysteresis ||
                                cfg[arm].reclaim_skip_repeat)) {
                    const auto& C = validation::e25::counts();
                    if (C.reclaim_skipped_gap + C.reclaim_skipped_unchanged == 0)
                        throw std::runtime_error("E31H: the hysteresis never skipped a reclaim");
                }
#endif
                std::cerr << kind << " rep" << r << " " << names[arm] << " done" << std::endl;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

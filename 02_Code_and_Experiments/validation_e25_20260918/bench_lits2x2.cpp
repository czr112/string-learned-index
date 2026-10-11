// G3/A1': does WIDENING the reclaim's trigger surface -- without migrating anything --
// reproduce what E37's drain actually did?
//
// Derived from bench_long_art.cpp, which produced results/main/long_e31art_{same,grow}.jsonl
// with five arms. THAT FILE AND ITS BUILD RECORD ARE UNTOUCHED: the record has to keep
// describing the source that produced those records. This program runs four arms inside one
// process, into its own file:
//
//   results/main/long_lits2x2_<mode>.jsonl
//
// FOUR arms:
//   S0    the shipped configuration                        <- the baseline
//   E31   S0 + subblock_reclaim: tightens ONLY the rank-selected sub-block
//   WIDE  E31 + reclaim_widest: then the group's OTHER sub-blocks, dirtiest first, up to a
//         fixed budget. In place only -- nothing is migrated and no overflow record moves.
//   ART   libart, the standing baseline -- NOT a Config switch, so it carries no structural
//         columns (libart exposes no overflow/bytes accounting here).
//
// The single variable between S0 and E31 is `subblock_reclaim`; between E31 and WIDE it is
// `reclaim_widest`. Both are asserted with require_only_switches_differ, so an arm that
// silently became a copy of another fails the run instead of passing a comparison that never
// happened. The E31 arm is kept deliberately: E37's own attribution says the effect came from
// reclaim reaching blocks the insert path never does, and this file is the measurement that
// separates that from the migration.
//
// WHY E31 IS THE RIGHT CONTROL AND NOT S0 ALONE: E31 already contains the rank-selected
// reclaim. Comparing WIDE only against S0 would fold "reclaim exists at all" into "widening
// helps". The pair of ratios (E31/S0 and WIDE/E31) keeps them apart, and no two of them are
// ever multiplied together.

//        its index bytes are reported as 0 with that meaning stated in the report.
//
// The arm order is ROTATED per repetition, as in the file this was derived from: an arm
// appended at the end would always run last, and "always last" is exactly the systematic
// position effect this project already guards against. Four repetitions give each arm four
// different positions.
//
// WORKLOAD DEFINITION, stated because it differs from E26's short-run version. E26's
// replace deletes an original key and inserts a NEW one, which exhausts the original
// key pool after N/4 cycles. For a run out to 200% of N that is impossible, so this
// program keeps a live-key ring: each cycle removes one live key and appends one new
// key. The live count is therefore ~N at every checkpoint, which is what the task
// book required ("活跃记录数基本不变"), and cycles are unbounded.
//
// Reported at each checkpoint: cumulative operation time, overflow count, compaction
// count, and index bytes. With -DE25_DIAG the same source additionally reports the
// compaction work counters (records replayed, bytes copied, garbage before) -- the
// counters stay out of the clean timing build.

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
        base.compact_reuse_tag = true;
        base.compact_needs_garbage = true;
        // The 2x2 cross-backend check, plus libart as the external arm.
        //
        //   arm 0  S0     Context16, the shipped configuration          (M_old, no A1')
        //   arm 1  WIDE   Context16 + subblock_reclaim + reclaim_widest (M_old, A1')
        //   arm 2  S0L    LitsHpt,   otherwise the shipped configuration (M_ref, no A1')
        //   arm 3  WIDEL  LitsHpt   + subblock_reclaim + reclaim_widest (M_ref, A1')
        //
        // G3 established A1's effect under Context16 only. A mapping that groups records
        // differently can change which sub-block the rank-selected candidate lands in, and
        // therefore whether the widened search has anything to find -- so the row has to be
        // re-measured rather than assumed. All four index arms live in one process and one
        // repetition, so the WIDE/S0 ratios in both rows are paired the same way.
        //
        // Every arm-difference is asserted below, so an arm that silently became a copy of
        // another fails here instead of passing a comparison that never happened.
        Config cfg[4] = {base, base, base, base};
        cfg[1].subblock_reclaim = true;                        // A1': rank-selected block first
        cfg[1].reclaim_widest = true;                          //       then the group's others
        cfg[2].model_backend = validation::ModelBackend::LitsHpt;
        cfg[3].model_backend = validation::ModelBackend::LitsHpt;
        cfg[3].subblock_reclaim = true;
        cfg[3].reclaim_widest = true;
        e25sw::require_only_switches_differ(cfg[0], cfg[1], {"subblock_reclaim", "reclaim_widest"});
        e25sw::require_only_switches_differ(cfg[2], cfg[3], {"subblock_reclaim", "reclaim_widest"});
        e25sw::require_only_switches_differ(cfg[0], cfg[2], {"model_backend"});
        e25sw::require_only_switches_differ(cfg[1], cfg[3], {"model_backend"});
        const char* names[5] = {"S0", "WIDE", "S0L", "WIDEL", "ART"};
        // ART is not a Config: the four assertions above pin the four Index arms against each
        // other, and the ART arm never reads cfg.
        const int ARMS = 5;
        const int ART_ARM = 4;

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

        // Its own file, so the four-arm records produced by the earlier binary
        // (long_e31_*.jsonl, kept as they are) are never mixed with this one's. APPEND,
        // not truncate: the sweep runs five independent processes, each appending its
        // own records, so truncation would leave only the last process's. The driver
        // (run_e31art.py) removes the file before the sweep, as run_e25.py does.
        std::ofstream out(("results/main/long_lits2x2_" + mode + ".jsonl").c_str(),
                          std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (int r = 0; r < reps; ++r) {
            // Rotate which arm goes first, so no arm is systematically first or last.
            int order[5] = {r % ARMS, (r + 1) % ARMS, (r + 2) % ARMS,
                             (r + 3) % ARMS, (r + 4) % ARMS};
            for (int oi = 0; oi < ARMS; ++oi) {
                const int arm = order[oi];
                const bool is_art = (arm == ART_ARM);
                // Live-key ring: deque front is the next delete target, the new key is
                // appended. The live count is constant at N.
                std::deque<std::string> live;
                std::unordered_set<std::string> live_set;
                Reference ref;
                for (const auto& kv : keys) { live.push_back(kv.first); ref[kv.first] = kv.second; live_set.insert(kv.first); }

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
                    s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
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

                    const auto a = Clock::now();
                    for (const auto& m : seg) {
                        if (is_art) {
                            art->erase(m.victim);
                            art->upsert(m.fresh, m.value);
                        } else {
                            s->erase(m.victim);
                            s->insert(m.fresh, m.value);
                        }
                    }
                    cum += ms_between(a, Clock::now());
                    done = target;

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
                    // The cross-backend question is whether A1' has anything to widen under
                    // the reference mapping. The three counters answer it directly: how many
                    // blocks the widened search examined, how many it repacked, and how many
                    // of those repacks produced the placement. All three are per-arm, because
                    // counts() is reset at the top of each arm.
                    const std::uint64_t widened = C.reclaim_widened;
                    const std::uint64_t widened_helped = C.reclaim_widened_helped;
                    const std::uint64_t widened_examined = C.reclaim_widened_blocks_examined;
#else
                    const std::uint64_t replayed = 0, copied = 0, garbage = 0, helped = 0;
                    const std::uint64_t widened = 0, widened_helped = 0, widened_examined = 0;
#endif
                    out << "{\"kind\":\"" << kind << "\",\"rep\":" << r
                        << ",\"arm\":\"" << names[arm] << "\",\"run_tag\":\"" << run_tag << "\""
                        << ",\"mode\":\"" << mode << "\",\"frac\":" << frac
                        << ",\"cycles\":" << done
                        << ",\"live\":" << (is_art ? art->size() : s->size())
                        << ",\"live0\":" << live0
                        << ",\"cum_ms\":" << cum
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
                        << ",\"widened\":" << widened
                        << ",\"widened_helped\":" << widened_helped
                        << ",\"widened_examined\":" << widened_examined
                        << "}" << std::endl;
                }
                std::cerr << kind << " rep" << r << " " << names[arm] << " done" << std::endl;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

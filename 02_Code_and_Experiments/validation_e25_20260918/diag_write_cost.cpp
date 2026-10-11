// E3 entry diagnosis: WHERE the write path's time actually goes.
//
// THE PLAN'S QUESTION (OPTIMIZATION_PLAN_V2.md section 6, row E3) is not "can allocations be
// reduced" but "which of these five is the mass": real suffix writes / descriptor moves /
// container growth / short-lived temporaries / output materialisation. Its stated deciding
// diagnostic is: extend E32 section 4's overflow-cost decomposition to the WHOLE write path.
//
// SO THIS PROGRAM MEASURES TWO THINGS AT ONCE, and neither substitutes for the other:
//
//   1. A TIME BUDGET of the write path. The whole Index::insert and Index::erase are clocked,
//      and so is every phase that nests inside them (locate, place_with_ctx, rebuild,
//      repack_subblock, compact, and the overflow container's own lookup/construct/erase).
//      Every one of those NESTS, so each ratio is a SHARE of its parent and the pieces are
//      never added together.
//   2. A WORK CENSUS of the five kinds, in bytes and events: suffix bytes really written
//      (primary E18 + overflow, newly counted), descriptors moved (E18 entry shifts + the
//      overflow shift counters), container growth (the new-peak/capacity-change counters from
//      the C-2 round), short-lived temporaries (std::string constructions), and output
//      materialisation (Region::all()'s records and time).
//
// THE CLOCK ITSELF IS MEASURED. E32 found three pairs of clocks in one program had put their
// own overhead into its residual, so this program calibrates an EMPTY timer (200,000 reads)
// and prints ns per pair. The analyzer subtracts `calls * calib_ns` from each phase, which is
// conservative: a back-to-back read loop is the best case for the clock, so real call sites
// pay no less. The uncorrected figure is printed too, so the correction can be checked.
//
// WHAT THIS IS NOT. No arm is compared against another, no flag is flipped, and no
// before/after is claimed: every figure decomposes THIS build's own write span. Counters and
// clocks locate a candidate; they do not price a fix, and no number here is a speedup.
//
// WORKLOAD: the long replacement load of bench_long / diag_ov_cost, unchanged (same frozen
// configuration, same live-key ring, same 25/50/100/200% checkpoints, modes `same` and
// `grow`), plus a PURE INSERT control. The insert control exists because a replacement
// workload's erase side loads a different set of phases than its insert side, and a budget
// taken on one of them alone would be read as the budget of "writes".
//
// Usage:  diag_write_cost KIND N REPS [TRANCOCSV] [RUNTAG] [MODE]
//         MODE = same | grow | insert
// Output: results/main/write_cost_diag_20261010.jsonl  (appended; one row per checkpoint)

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
#error "diag_write_cost needs -DE25_DIAG: the work census (overflow bytes, shifts, growth) is declared under it, and with the macro off the census column would be zeros that read like 'the write path does no work'."
#endif
#ifndef E30_TIME
#error "diag_write_cost needs -DE30_TIME: the clocks live in the E30 timer block."
#endif
#ifndef E30_WRITE
#error "diag_write_cost needs -DE30_WRITE: the write-path phase clocks (w_insert/w_erase/w_locate/w_place/w_rebuild/w_reclaim) are compiled only under it, so without the macro every phase share would be 0/0."
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

struct Snap {
    e30::Timers t{};
    e25::Counts c{};
    e18::Counts e{};
};
static Snap take() {
    Snap s;
    s.t = e30::timers();
    s.c = e25::counts();
    s.e = e18::counts();
    return s;
}

// One empty timer pair, timed from a different clock. Printed, and subtracted by the
// analyzer: the correction is CONSERVATIVE because a tight loop is the cheapest possible
// place to read the clock.
static double calib_ns() {
    const int K = 200000;
    std::uint64_t acc = 0;
    const auto a = Clock::now();
    for (int i = 0; i < K; ++i) {
        const std::uint64_t t0 = E30_T0();
        acc += E30_T0() - t0;
    }
    const double ms = ms_between(a, Clock::now());
    if (acc == 0) return 0.0;   // clock compiled out; the guard keeps the loop honest
    return ms * 1e6 / double(K);
}

static void emit(std::ostream& out, const std::string& kind, const std::string& mode,
                 const std::string& run_tag, int rep, const char* workload, double frac,
                 std::size_t ops, std::size_t live, double erase_ms, double insert_ms,
                 double calib, const Snap& a, const Snap& b) {
    const auto d = [](std::uint64_t x, std::uint64_t y) { return x - y; };
    out << "{\"t\":\"w\",\"kind\":\"" << kind << "\",\"mode\":\"" << mode
        << "\",\"run_tag\":\"" << run_tag << "\",\"rep\":" << rep
        << ",\"workload\":\"" << workload << "\",\"frac\":" << frac
        << ",\"ops\":" << ops << ",\"live\":" << live
        << ",\"calib_ns\":" << calib
        // driver wall clock of the two passes (the outer denominator)
        << ",\"erase_ms\":" << erase_ms << ",\"insert_ms\":" << insert_ms
        // Index::insert / Index::erase, whole
        << ",\"w_insert_ns\":" << d(b.t.w_insert_ns, a.t.w_insert_ns)
        << ",\"w_insert_calls\":" << d(b.t.w_insert_calls, a.t.w_insert_calls)
        << ",\"w_erase_ns\":" << d(b.t.w_erase_ns, a.t.w_erase_ns)
        << ",\"w_erase_calls\":" << d(b.t.w_erase_calls, a.t.w_erase_calls)
        // nested phases
        << ",\"w_locate_ns\":" << d(b.t.w_locate_ns, a.t.w_locate_ns)
        << ",\"w_locate_calls\":" << d(b.t.w_locate_calls, a.t.w_locate_calls)
        << ",\"w_place_ns\":" << d(b.t.w_place_ns, a.t.w_place_ns)
        << ",\"w_place_calls\":" << d(b.t.w_place_calls, a.t.w_place_calls)
        << ",\"w_rebuild_ns\":" << d(b.t.w_rebuild_ns, a.t.w_rebuild_ns)
        << ",\"w_rebuild_calls\":" << d(b.t.w_rebuild_calls, a.t.w_rebuild_calls)
        << ",\"w_rall_ns\":" << d(b.t.w_rebuild_all_ns, a.t.w_rebuild_all_ns)
        << ",\"w_rall_calls\":" << d(b.t.w_rebuild_all_calls, a.t.w_rebuild_all_calls)
        << ",\"w_rall_records\":" << d(b.t.w_rebuild_all_records, a.t.w_rebuild_all_records)
        << ",\"w_locate_ins_ns\":" << d(b.t.w_locate_ins_ns, a.t.w_locate_ins_ns)
        << ",\"w_locate_ins_calls\":" << d(b.t.w_locate_ins_calls, a.t.w_locate_ins_calls)
        << ",\"w_locate_era_ns\":" << d(b.t.w_locate_era_ns, a.t.w_locate_era_ns)
        << ",\"w_locate_era_calls\":" << d(b.t.w_locate_era_calls, a.t.w_locate_era_calls)
        << ",\"w_findloc_ns\":" << d(b.t.w_findloc_ns, a.t.w_findloc_ns)
        << ",\"w_findloc_calls\":" << d(b.t.w_findloc_calls, a.t.w_findloc_calls)
        << ",\"w_reclaim_ns\":" << d(b.t.w_reclaim_ns, a.t.w_reclaim_ns)
        << ",\"w_reclaim_calls\":" << d(b.t.w_reclaim_calls, a.t.w_reclaim_calls)
        << ",\"erase_ns\":" << d(b.t.erase_ns, a.t.erase_ns)
        << ",\"compact_ns\":" << d(b.t.compact_ns, a.t.compact_ns)
        << ",\"compact_calls\":" << d(b.t.compact_calls, a.t.compact_calls)
        << ",\"ov_insert_ns\":" << d(b.t.ov_insert_ns, a.t.ov_insert_ns)
        << ",\"ov_insert_calls\":" << d(b.t.ov_insert_calls, a.t.ov_insert_calls)
        << ",\"ov_erase_ns\":" << d(b.t.ov_erase_ns, a.t.ov_erase_ns)
        << ",\"ov_erase_calls\":" << d(b.t.ov_erase_calls, a.t.ov_erase_calls)
        << ",\"ov_lookup_ns\":" << d(b.t.ov_lookup_ns, a.t.ov_lookup_ns)
        << ",\"ov_alloc_ns\":" << d(b.t.ov_alloc_ns, a.t.ov_alloc_ns)
        // the five work kinds, in bytes and events
        << ",\"k_suffix_bytes_primary\":" << d(b.e.suffix_bytes_copied, a.e.suffix_bytes_copied)
        << ",\"k_suffix_bytes_overflow\":" << d(b.c.ov_suffix_bytes_written, a.c.ov_suffix_bytes_written)
        << ",\"k_entry_shifts\":" << d(b.e.entry_shifts, a.e.entry_shifts)
        << ",\"k_ov_shift_recs_ins\":" << d(b.c.ov_shifted_records_insert, a.c.ov_shifted_records_insert)
        << ",\"k_ov_shift_bytes_ins\":" << d(b.c.ov_shifted_bytes_insert, a.c.ov_shifted_bytes_insert)
        << ",\"k_ov_shift_recs_era\":" << d(b.c.ov_shifted_records_erase, a.c.ov_shifted_records_erase)
        << ",\"k_ov_shift_bytes_era\":" << d(b.c.ov_shifted_bytes_erase, a.c.ov_shifted_bytes_erase)
        << ",\"k_cap_change_events\":" << d(b.c.ov_cap_change_events, a.c.ov_cap_change_events)
        << ",\"k_cap_change_bytes\":" << d(b.c.ov_cap_change_copy_bytes, a.c.ov_cap_change_copy_bytes)
        << ",\"k_string_allocs\":" << d(b.c.string_allocs, a.c.string_allocs)
        << ",\"k_overflow_inserts\":" << d(b.e.overflow_inserts, a.e.overflow_inserts)
        << ",\"k_insert_attempts\":" << d(b.e.insert_attempts, a.e.insert_attempts)
        << ",\"k_insert_ok\":" << d(b.e.insert_ok, a.e.insert_ok)
        << "}" << std::endl;
}

int main(int argc, char** argv) {
    try {
#ifdef __linux__
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 4)
            throw std::invalid_argument("diag_write_cost KIND N REPS [TRANCOCSV] [RUNTAG] [MODE]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argc > 4 ? argv[4] : "";
        const std::string run_tag = argc > 5 ? argv[5] : "";
        const std::string mode = argc > 6 ? argv[6] : "same";
        if (mode != "same" && mode != "grow" && mode != "insert")
            throw std::invalid_argument("MODE must be same, grow or insert");

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

        // The FROZEN configuration. The budget is the shipped path's budget; nothing is
        // flipped to make a phase look smaller.
        Config cfg = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg);
        std::cerr << "diag_write_cost switches " << e25sw::switches(cfg) << std::endl;

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};
        const double calib = calib_ns();
        std::cerr << "clock calibration: " << calib << " ns per timer pair\n";

        std::ofstream out("results/main/write_cost_diag_20261010.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        for (int r = 0; r < reps; ++r) {
            if (mode == "insert") {
                // ---------------- pure insert control ----------------
                std::vector<Pair> loaded(keys.begin(), keys.begin() + std::ptrdiff_t(N / 2));
                std::vector<Pair> pending(keys.begin() + std::ptrdiff_t(N / 2), keys.end());

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
                Reference ref;
                for (const auto& kv : loaded) ref[kv.first] = kv.second;

                validation::e25::counts().reset();
                validation::e18::counts().reset();
                validation::e30::timers().reset();

                std::size_t done = 0;
                // A SEPARATE checkpoint table, and it is not cosmetic: the replacement table
                // goes to 2.00 (200% of the LIVE SET), while this arm inserts a finite
                // `pending` whose 200% does not exist. Reusing it constructed an iterator
                // range past the end -- the first version of this file did exactly that, and
                // the row count (3 instead of 4) is what gave it away.
                const double INS_CPS[4] = {0.25, 0.50, 0.75, 1.00};
                for (double frac : INS_CPS) {
                    const std::size_t target = static_cast<std::size_t>(double(pending.size()) * frac);
                    if (target <= done) continue;
                    std::vector<Pair> seg(pending.begin() + std::ptrdiff_t(done),
                                          pending.begin() + std::ptrdiff_t(target));
                    for (const auto& kv : seg) {
                        if (ref.find(kv.first) != ref.end()) throw std::runtime_error("insert control: key present");
                        ref[kv.first] = kv.second;
                    }
                    const Snap before = take();
                    const auto ai = Clock::now();
                    for (const auto& kv : seg) s->insert(kv.first, kv.second);
                    const double insert_ms = ms_between(ai, Clock::now());
                    const Snap after = take();
                    done = target;
                    if (s->size() != ref.size()) throw std::runtime_error("insert control: live count drift");
                    for (std::size_t i = 0; i < seg.size(); i += std::max<std::size_t>(1, seg.size() / 16)) {
                        const Result res = s->find(seg[i].first, qmode);
                        if (!res.found || res.value != seg[i].second)
                            throw std::runtime_error("insert control: spot check failed");
                    }
                    emit(out, kind, mode, run_tag, r, "insert", frac, seg.size(), s->size(),
                         0.0, insert_ms, calib, before, after);
                }
                continue;
            }

            // ---------------- replacement ----------------
            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;
            Reference ref;
            for (const auto& kv : keys) {
                live.push_back(kv.first);
                ref[kv.first] = kv.second;
                live_set.insert(kv.first);
            }

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

            // Reset AFTER the load: the constructor places records through the same paths,
            // and every figure below must be the replacement workload's own work.
            validation::e25::counts().reset();
            validation::e18::counts().reset();
            validation::e30::timers().reset();

            std::size_t done = 0;
            std::uint64_t serial = 0;

            for (double frac : CPS) {
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                if (target <= done) continue;

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

                const Snap before = take();
                const auto ae = Clock::now();
                for (const auto& m : seg) s->erase(m.victim);
                const double erase_ms = ms_between(ae, Clock::now());
                const auto ai = Clock::now();
                for (const auto& m : seg) s->insert(m.fresh, m.value);
                const double insert_ms = ms_between(ai, Clock::now());
                const Snap after = take();
                done = target;

                if (s->size() != ref.size())
                    throw std::runtime_error("replacement: live count drifted from the reference");
                for (std::size_t i = 0; i < seg.size(); i += std::max<std::size_t>(1, seg.size() / 16)) {
                    const Result res = s->find(seg[i].fresh, qmode);
                    if (!res.found || res.value != seg[i].value)
                        throw std::runtime_error("replacement: spot check failed");
                }

                emit(out, kind, mode, run_tag, r, "replace", frac, seg.size(), s->size(),
                     erase_ms, insert_ms, calib, before, after);
            }
        }

        std::cout << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\",\"case\":\"done\"}"
                  << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

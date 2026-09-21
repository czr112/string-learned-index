// E35 composition diagnostic: what does the replacement sequence actually delete?
//
// Derived from bench_prefix.cpp. That file and its build record are untouched: they
// produced results/main/prefix_{same,grow}.jsonl, which RESULTS_E35.md cites.
//
// WHY THIS EXISTS. E34 reported that the overflow container's cost appears only once
// replacement exceeds one full pass: the logical shift distance is in the thousands at the
// 100% checkpoint and in the millions at 200%. That was stated as a description -- "the
// cost starts after one full pass" -- and a description is not an explanation. The
// competing explanation is a COMPOSITION effect rather than a threshold inside the
// container: the first pass deletes records that came from the initial bulk load, and the
// second pass begins deleting records that earlier replacements pushed into overflow.
//
// So per deletion, recorded immediately BEFORE the erase so it sees exactly the state the
// erase sees:
//   origin    bulk-loaded, or inserted by an earlier replacement
//   location  primary area, or the overflow container (probed through the public find)
// and per interval, the delta of the existing ov_shifted_records_erase counter, so the
// average displacement per OVERFLOW delete is available and not just the total.
//
// NO TIMING CLAIMS. The probe sits inside the loop the derived program times, so this
// program's millisecond fields describe a build that does extra work and are NOT reported
// anywhere. It is a counting diagnostic.
//
// Arms are E34's/E35's: B (frozen), O (B + overflow_stable_store), ART. Only the two
// Index arms carry composition columns; ART has no overflow container, so the columns are
// reported as 0 for it and mean NOT APPLICABLE.
//
// Workload is unchanged from bench_prefix.cpp: the live-key ring, the same replacement
// anchoring, the same checkpoints.
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
        // Three arms. B and O differ by exactly one switch, so every difference between
        // them is attributable to the overflow organization. B is E34's C2 -- the reviewer
        // specified that only the overflow organization changes, so the baseline is the
        // currently selected configuration.
        Config cfg[2] = {base, base};
        cfg[0].compact_reuse_tag = true;
        cfg[0].compact_needs_garbage = true;                   // C2 = frozen = the baseline
        cfg[1].compact_reuse_tag = true;
        cfg[1].compact_needs_garbage = true;
        cfg[1].overflow_stable_store = true;                   // O1 = frozen + the stable store
        // Registered against the one field table every gate shares. A switch missing from it
        // makes this assertion pass vacuously -- this project has already shipped that bug
        // once, which is why require_only_switches_differ exists.
        e25sw::require_only_switches_differ(cfg[0], cfg[1], {"overflow_stable_store"});
        const char* names[3] = {"B", "O", "ART"};
        // ART is not a Config, so cfg stays two wide and the ART arm is taken by index in
        // the loop below.
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
        std::ofstream out(("results/main/compose_diag_" + mode + ".jsonl").c_str(),
                          std::ios::app);
#else
        std::ofstream out(("results/main/compose_" + mode + ".jsonl").c_str(),
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
                // Provenance, kept in step with the ring: was this live key part of the
                // bulk load, or did an earlier replacement insert it? Without this the
                // "what changes at 100%" question cannot be asked at all.
                std::deque<bool> live_initial;
                std::unordered_set<std::string> live_set;
                Reference ref;
                for (const auto& kv : keys) {
                    live.push_back(kv.first);
                    live_initial.push_back(true);
                    ref[kv.first] = kv.second;
                    live_set.insert(kv.first);
                }

                std::unique_ptr<Index> s;
                std::unique_ptr<e22art::NativeArt> art;
#ifdef E25_DIAG
                // The counters are process-global and this loop runs one arm after the
                // other, so without this reset arm C2's figures would be C1's PLUS its
                // own -- which made C2 look like it replayed 35x more records than C1.
                // Reset per arm; the figures are then that arm's own cumulative work.
                //
                // The reset is OUTSIDE the branch below on purpose. While it sat inside the
                // non-ART branch, the ART arm read whatever the previous arm had left behind
                // and its row carried O1's movement totals -- numbers that look like a
                // measurement of ART and are not one. ART is not an Index, so its counters
                // must read 0.
                validation::e25::counts().reset();
#endif
                if (is_art) {
                    art = std::make_unique<e22art::NativeArt>(keys);
                } else {
                    s = std::make_unique<Index>(cfg[arm]);
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
                    struct Move { std::string victim, fresh; std::uint64_t value; bool initial; };
                    std::vector<Move> seg;
                    seg.reserve(target - done);
                    for (std::size_t i = done; i < target; ++i) {
                        const std::string victim = live.front();
                        const bool victim_initial = live_initial.front();
                        live.pop_front();
                        live_initial.pop_front();
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
                        seg.push_back({victim, fresh, val, victim_initial});
                        live.push_back(fresh);
                        live_initial.push_back(false);
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
                    // del[0] deletes | [1] built from the ring's provenance | [2] inserted
                    // earlier | [3] resolved in OVERFLOW | [4] overflow AND built |
                    // [5] overflow AND inserted | [6] resolved in the primary area
                    std::uint64_t del[7] = {0, 0, 0, 0, 0, 0, 0};
                    const Mode cmode{Route::Interval, Prefetch::Off, true, true,
                                     OvSearch::Hybrid, thr, false};
                    const std::uint64_t shift_before =
                        validation::e25::counts().ov_shifted_records_erase;

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
                            // The probe is immediately before the erase, so it describes the
                            // exact record about to be removed. It sits inside the span the
                            // derived program times, which is why this program reports no
                            // timing: the numbers would describe a build that does extra work.
                            {
                                const Result pr = s->find(m.victim, cmode);
                                ++del[0];
                                ++del[m.initial ? 1 : 2];
                                if (pr.found && pr.source == 2) {
                                    ++del[3];
                                    ++del[m.initial ? 4 : 5];
                                } else {
                                    ++del[6];
                                }
                            }
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
                    const std::uint64_t ov_shift_records_erase = C.ov_shifted_records_erase;
                    const std::uint64_t ov_shift_bytes_erase = C.ov_shifted_bytes_erase;
                    // Counts has no overflow-call field; string_allocs IS that count. The
                    // E32 diagnostic asserts the two agree, because both are incremented at the
                    // single site the overflow insert clock wraps -- so this is the same number, not
                    // a substitute for it.
                    const std::uint64_t ov_insert_calls = C.string_allocs;
                    // The mechanism, in the same record as the timings. Handle moves are
                    // what the ON arm does INSTEAD of moving records; record_moves_stable
                    // must be 0, or the store is not doing its job.
                    const std::uint64_t ov_handle_moves = C.overflow_handle_moves;
                    const std::uint64_t ov_handle_move_bytes = C.overflow_handle_move_bytes;
                    const std::uint64_t ov_record_moves_stable = C.overflow_record_moves_stable;
                    const std::uint64_t ov_record_move_bytes_stable = C.overflow_record_move_bytes_stable;
                    const std::uint64_t ov_store_high_water = C.ov_store_high_water;
                    const std::uint64_t ov_store_live_water = C.ov_store_live_water;
#else
                    const std::uint64_t replayed = 0, copied = 0, garbage = 0, helped = 0;
                    const std::uint64_t skipped_gap = 0, skipped_unchanged = 0;
                    const std::uint64_t ov_shift_records_erase = 0;
                    const std::uint64_t ov_shift_bytes_erase = 0;
                    const std::uint64_t ov_insert_calls = 0;
                    const std::uint64_t ov_handle_moves = 0;
                    const std::uint64_t ov_handle_move_bytes = 0;
                    const std::uint64_t ov_record_moves_stable = 0;
                    const std::uint64_t ov_record_move_bytes_stable = 0;
                    const std::uint64_t ov_store_high_water = 0;
                    const std::uint64_t ov_store_live_water = 0;
#endif
                    out << "{\"phase\":\"maint\",\"kind\":\"" << kind << "\",\"rep\":" << r
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
                        // O1: the movement counters, so the mechanism is in the record.
                        // The two ov_shifted_* counters are the LOGICAL shift distance and
                        // are identical in both arms by design -- both arms face the same
                        // erase positions, so that is the workload being held constant, not
                        // a measurement of what moved. What moved is the next four fields:
                        // the ON arm shifts 4-byte handles, the OFF arm 48-byte records.
                        << ",\"ov_shifted_records_erase\":" << ov_shift_records_erase
                        << ",\"ov_shifted_bytes_erase\":" << ov_shift_bytes_erase
                        << ",\"ov_insert_calls\":" << ov_insert_calls
                        // --- appended fields (E31H): new values go at the END, so a reader
                        // that stops at "helped" still parses this record.
                        << ",\"op_samples\":" << op_samples
                        << ",\"op_p50_ns\":" << op_p50
                        << ",\"op_p99_ns\":" << op_p99
                        << ",\"op_p999_ns\":" << op_p999
                        << ",\"op_max_ns\":" << op_max
                        << ",\"reclaim_skipped_gap\":" << skipped_gap
                        << ",\"reclaim_skipped_unchanged\":" << skipped_unchanged
                        // E35 composition composition. `del` is per-interval; the shift counter delta is
                        // per-interval too, taken from the arm's cumulative counter.
                        << ",\"del_total\":" << del[0]
                        << ",\"del_initial\":" << del[1]
                        << ",\"del_inserted\":" << del[2]
                        << ",\"del_overflow\":" << del[3]
                        << ",\"del_ov_initial\":" << del[4]
                        << ",\"del_ov_inserted\":" << del[5]
                        << ",\"del_primary\":" << del[6]
                        << ",\"shift_delta\":" << (validation::e25::counts().ov_shifted_records_erase - shift_before)
                        << ",\"ov_handle_moves\":" << ov_handle_moves
                        << ",\"ov_handle_move_bytes\":" << ov_handle_move_bytes
                        << ",\"ov_record_moves_stable\":" << ov_record_moves_stable
                        << ",\"ov_record_move_bytes_stable\":" << ov_record_move_bytes_stable
                        << ",\"ov_store_high_water\":" << ov_store_high_water
                        << ",\"ov_store_live_water\":" << ov_store_live_water
                        << "}" << std::endl;
                }
#ifdef E25_DIAG
                // O1's own non-vacuity check: the arm that switches the store on must have
                // moved handles and must NOT have moved records, or this program measured the
                // C2 arm twice. A gate that passes without executing its branch proves
                // nothing -- that lesson is already in check_e31.
                if (!is_art && cfg[arm].overflow_stable_store) {
                    const auto& C = validation::e25::counts();
                    if (C.overflow_handle_moves == 0)
                        throw std::runtime_error("O1: the stable store moved no handle");
                    if (C.overflow_record_moves_stable != 0)
                        throw std::runtime_error("O1: the stable store still moved whole records");
                }

                // E35 composition non-vacuity: the composition columns are only meaningful if deletes
                // really did resolve in the overflow container. A run where every delete hit
                // the primary area would report overflow deletes of 0 -- which reads as "no
                // cost" rather than "not measured". The counter is the arm's cumulative
                // displacement count, so this asserts the workload reached the container at
                // all; the per-interval columns then say WHEN it started to.
                if (!is_art && cfg[arm].overflow_stable_store) {
                    const auto& C2 = validation::e25::counts();
                    if (C2.ov_shifted_records_erase == 0)
                        throw std::runtime_error("E35 composition: no overflow record was ever displaced, so the "
                                                 "composition columns describe nothing");
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

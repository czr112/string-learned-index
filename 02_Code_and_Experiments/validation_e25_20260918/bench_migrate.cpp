// E37 timing: R0 / R1 -- does draining overflow back into the primary area pay?
//
// Derived from bench_tag.cpp. That file and its build record are untouched: they produced
// results/main/tag_{same,grow}.jsonl, which RESULTS_E36.md cites.
//
// FOUR arms, rotated over four repetitions:
//   B    the frozen version                        (context)
//   R0   B + Config::subblock_reclaim              <- THE E37 BASE, no migration
//   R1   R0 + Config::overflow_migrate             <- the base with the drain on
//   ART  libart, for scale; it takes no part in the queue phase.
//
// The base is the reviewer's: E31's sub-block reclaim plus the ORIGINAL overflow container,
// with the stable store and the inline tag deliberately NOT stacked, so a container change
// and a migration effect cannot be confused for one another.
//
// WHAT THE RECORD HAS TO CARRY, and why:
//   migrated_records / migrated_bytes   the migration volume itself, plus migrate_calls and
//                                       migrate_helped (whether the retry that triggered the
//                                       drain then succeeded)
//   overflow / cum_ms                   whether draining reduces what is held, and what it
//                                       costs in maintenance time
//   op_p50..max                         the tail, because a drain is a burst of work
//   memory / ov_bytes                   resident space and its overflow share
//   query_ms and the queue columns      whether the read path changed
//
// AND WHAT IT MUST NOT CLAIM. kMigrateBudget caps the records one call may move. The call's
// WORK is the candidate walk (O(container)), the compaction (O(container)) and the suffix
// copies (O(migrated bytes)) -- so "at most K records" is a cap on MIGRATION COUNT, not a
// bound on maintenance cost. The report says so in those words.
//
// ONE SOURCE, TWO TARGETS:
//   bench_migrate   clean      -- the TIMINGS
//   diag_migrate_b  -DE25_DIAG -- the COUNTERS, including the migration volume. Its timings
//                                 are not reported: counting sits in the hot path and is
//                                 asymmetric between the arms.
//
// Workload, plan, fixed query queues and acceptance fields are bench_tag.cpp's, unchanged.
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
        // Each arm one switch from the last, so every difference between two of them is
        // attributable to exactly one change.
        Config cfg[3] = {base, base, base};
        cfg[0].compact_reuse_tag = true;
        cfg[0].compact_needs_garbage = true;                   // B = frozen = the baseline
        cfg[1].compact_reuse_tag = true;
        cfg[1].compact_needs_garbage = true;
        cfg[1].subblock_reclaim = true;                        // R0 = the E37 base
        cfg[2].compact_reuse_tag = true;
        cfg[2].compact_needs_garbage = true;
        cfg[2].subblock_reclaim = true;
        cfg[2].overflow_migrate = true;                        // R1 = R0 + the drain
        // Registered against the one field table every gate shares. A switch missing from it
        // makes these assertions pass vacuously -- this project has already shipped that bug
        // once, which is why require_only_switches_differ exists. BOTH pairs are asserted:
        // R1 must differ from R0 in the migration switch alone, or R1/R0 measures something
        // else.
        e25sw::require_only_switches_differ(cfg[0], cfg[1], {"subblock_reclaim"});
        e25sw::require_only_switches_differ(cfg[1], cfg[2], {"overflow_migrate"});
        const char* names[4] = {"B", "R0", "R1", "ART"};
        // ART is not a Config, so cfg stays three wide and the ART arm is taken by index in
        // the loop below.
        const int ARMS = 4;
        const int ART_ARM = 3;

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
        std::ofstream out(("results/main/migr_diag_" + mode + ".jsonl").c_str(),
                          std::ios::app);
#else
        std::ofstream out(("results/main/migr_" + mode + ".jsonl").c_str(),
                          std::ios::app);
#endif
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (int r = 0; r < reps; ++r) {
            // Rotate which arm goes first, so no arm is systematically first or last.
            // Four arms over four repetitions means every arm occupies every position
            // exactly once, so no arm is systematically first (coldest) or last (warmest).
            int order[4] = {r % ARMS, (r + 1) % ARMS, (r + 2) % ARMS, (r + 3) % ARMS};
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

                    // ---- E35: the fixed query queues -------------------------------------
                    // Skipped for ART, which has no overflow container: main / overflow /
                    // miss is not a distinction libart has, and inventing a different queue
                    // for it would put two definitions in one column.
                    if (!is_art) {
                        const Mode qm{Route::Interval, Prefetch::Off, true, true,
                                      OvSearch::Hybrid, thr, false};
                        const std::size_t QMAX = 2048;
                        const int QRUNS = 3;

                        // Candidates are arm-independent BY CONSTRUCTION: a spread through the
                        // live ring for the hits, and live keys with a marker appended for the
                        // misses -- kept only where the reference confirms they do not exist.
                        std::vector<std::string> hit_cand;
                        {
                            const std::size_t step = std::max<std::size_t>(1, live.size() / 8192);
                            for (std::size_t i = 0; i < live.size(); i += step)
                                hit_cand.push_back(live[i]);
                        }
                        std::vector<std::string> miss_cand;
                        for (const auto& k : hit_cand) {
                            std::string z = k + "~absent";
                            if (ref.find(z) == ref.end()) miss_cand.push_back(z);
                        }

                        std::vector<std::string> q[3];   // 0 primary hit, 1 overflow hit, 2 miss
                        const char* qn[3] = {"main", "overflow", "miss"};
                        const Index& ix = *s;

                        for (const auto& k : hit_cand) {
                            const Result rr = ix.find(k, qm);
                            // The probe is checked against the reference before it is used to
                            // build a queue: a queue built from a silently-wrong probe would
                            // time the wrong keys and say so nowhere.
                            if (!rr.found || rr.value != ref.at(k))
                                throw std::runtime_error("queue probe disagrees with the reference");
                            const int c = (rr.source == 2) ? 1 : 0;
                            if (q[c].size() < QMAX) q[c].push_back(k);
                        }
                        for (const auto& k : miss_cand) {
                            if (ix.find(k, qm).found)
                                throw std::runtime_error("a supposedly-absent probe key was found");
                            if (q[2].size() < QMAX) q[2].push_back(k);
                        }

                        for (int c = 0; c < 3; ++c) {
                            if (q[c].empty()) {
                                std::cerr << kind << " " << mode << " frac=" << frac << " arm="
                                          << names[arm] << " queue=" << qn[c] << " EMPTY\n";
                                continue;
                            }

                            // FNV-1a over the queue's keys. The analysis compares this between
                            // the B and O runs: equal hashes mean the two arms were timed on
                            // the SAME keys, which is what makes the per-queue ratio paired.
                            std::uint64_t qh = 1469598103934665603ULL;
                            for (const auto& k : q[c])
                                for (unsigned char ch : k) { qh ^= ch; qh *= 1099511628211ULL; }

                            std::vector<double> runs;
                            std::uint64_t acc = 0;

                            // Declared OUTSIDE the diagnostic guard: the clean build emits the
                            // same record shape with these at zero, because it does not count.
                            // A record whose keys appear only in one build would make the two
                            // builds' outputs mutually unparseable.
                            std::array<std::uint64_t, 13> cbase{}, cdiff{};

#ifdef E25_DIAG
                            // SNAPSHOT, not reset. Resetting here erased this arm's
                            // maintenance counters, which the arm-level non-vacuity check
                            // below still reads -- and that check then reported "the stable
                            // store moved no handle" for an arm that had moved millions.
                            // Every counter taken here is a monotone accumulator, so a
                            // difference is the same quantity a reset would have produced.
                            auto snapshot = [](const validation::e25::Counts& C) {
                                return std::array<std::uint64_t, 13>{
                                    C.ov_queries_bin, C.ov_queries_linear,
                                    C.ov_bin_cmps, C.ov_bin_decided4, C.ov_bin_decided8,
                                    C.ov_bin_short4, C.ov_bin_lcp_bytes,
                                    C.ov_linear_recs, C.ov_linear_tag_reject,
                                    C.ov_linear_tag_pass, C.ov_linear_reject8,
                                    // E36: entries examined, and rejected before any store
                                    // access. These are the reviewer's questions 1 and 2;
                                    // ov_linear_recs at index 7 above is question 3.
                                    C.ov_tag_entries, C.ov_tag_reject_pre};
                            };

                            cbase = snapshot(validation::e25::counts());
#endif

                            for (int rr = 0; rr < QRUNS; ++rr) {
                                const auto q0 = Clock::now();
                                for (const auto& k : q[c]) acc += ix.find(k, qm).value;
                                const auto q1 = Clock::now();
                                runs.push_back(ms_between(q0, q1));
                            }

#ifdef E25_DIAG
                            {
                                const auto cnow = snapshot(validation::e25::counts());
                                for (std::size_t i = 0; i < cnow.size(); ++i)
                                    cdiff[i] = cnow[i] - cbase[i];
                            }
#endif

                            std::sort(runs.begin(), runs.end());
                            const double med = runs[runs.size() / 2];
                            const double spread = runs.front() > 0 ? runs.back() / runs.front() : 0.0;

                            out << "{\"phase\":\"queue\",\"kind\":\"" << kind
                                << "\",\"mode\":\"" << mode << "\",\"rep\":" << r
                                << ",\"arm\":\"" << names[arm] << "\",\"run_tag\":\"" << run_tag
                                << "\",\"frac\":" << frac
                                << ",\"queue\":\"" << qn[c] << "\""
                                << ",\"keys\":" << q[c].size()
                                << ",\"runs\":" << QRUNS
                                << ",\"ms\":" << med
                                << ",\"ns_per_query\":" << (med * 1e6 / double(q[c].size()))
                                << ",\"spread\":" << spread
                                << ",\"queue_hash\":" << qh
                                // The accumulator keeps the queries from being optimised away,
                                // and it is EMITTED so that B and O can be required to produce
                                // the same sum on the same queue -- a second, independent check
                                // that the switch did not change any answer.
                                << ",\"queue_acc\":" << acc
                                << ",\"cand_hit\":" << hit_cand.size()
                                << ",\"cand_miss\":" << miss_cand.size()
                                // The binary path, which turned out to be the MINORITY path:
                                // containers average about ten records, under the frozen
                                // Hybrid cutoff, so most overflow queries take the linear scan.
                                << ",\"ov_bin_q\":" << cdiff[0]
                                << ",\"ov_linear_q\":" << cdiff[1]
                                << ",\"ov_bin_cmps\":" << cdiff[2]
                                << ",\"ov_decided4\":" << cdiff[3]
                                << ",\"ov_decided8\":" << cdiff[4]
                                << ",\"ov_short4\":" << cdiff[5]
                                << ",\"ov_lcp_bytes\":" << cdiff[6]
                                // The linear path: records examined, how many the TAG already
                                // rejected, and of the survivors how many an 8-byte prefix
                                // would settle on top of the tag.
                                << ",\"ov_lin_recs\":" << cdiff[7]
                                << ",\"ov_lin_tag_rej\":" << cdiff[8]
                                << ",\"ov_lin_tag_pass\":" << cdiff[9]
                                << ",\"ov_lin_reject8\":" << cdiff[10]
                                << ",\"ov_tag_entries\":" << cdiff[11]
                                << ",\"ov_tag_reject_pre\":" << cdiff[12]
                                << "}" << std::endl;
                        }
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
                    const std::uint64_t mig_calls = C.migrate_calls;
                    const std::uint64_t mig_records = C.migrated_records;
                    const std::uint64_t mig_bytes = C.migrated_bytes;
                    const std::uint64_t mig_helped = C.migrate_helped;
                    // E37: the drain repacks the blocks it targets, so R1 reclaims blocks the
                    // insert path would not have visited. These two columns are how much of
                    // R1's effect is "reclaim more often" rather than "move records back".
                    const std::uint64_t mig_rejected = C.migrate_rejected;
                    const std::uint64_t reclaims = C.subblock_reclaims;
                    const std::uint64_t reclaim_helped = C.subblock_reclaim_helped;
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
                    const std::uint64_t mig_calls = 0;
                    const std::uint64_t mig_records = 0;
                    const std::uint64_t mig_bytes = 0;
                    const std::uint64_t mig_helped = 0;
                    const std::uint64_t mig_rejected = 0;
                    const std::uint64_t reclaims = 0;
                    const std::uint64_t reclaim_helped = 0;
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
                        << ",\"ov_handle_moves\":" << ov_handle_moves
                        << ",\"ov_handle_move_bytes\":" << ov_handle_move_bytes
                        << ",\"ov_record_moves_stable\":" << ov_record_moves_stable
                        << ",\"ov_record_move_bytes_stable\":" << ov_record_move_bytes_stable
                        << ",\"ov_store_high_water\":" << ov_store_high_water
                        << ",\"ov_store_live_water\":" << ov_store_live_water
                        // E37: the drain's own volume, cumulative over the arm. These are the
                        // "迁回量" row; `helped` is the narrower question of whether the retry
                        // that triggered the drain then succeeded.
                        << ",\"migrate_calls\":" << mig_calls
                        << ",\"migrated_records\":" << mig_records
                        << ",\"migrated_bytes\":" << mig_bytes
                        << ",\"migrate_helped\":" << mig_helped
                        << ",\"migrate_rejected\":" << mig_rejected
                        << ",\"subblock_reclaims\":" << reclaims
                        << ",\"subblock_reclaim_helped\":" << reclaim_helped
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

                // E36: the tag arm must have actually rejected candidates from the entry
                // alone, or the arm ran the plain stable code and the comparison measures
                // nothing. A gate that passes without executing its branch proves nothing.
#ifdef E25_DIAG
                // E37: the arm that turns the drain on must have drained something, or this
                // program compared R1 with R0 under another name. A gate that passes without
                // executing its branch proves nothing -- that lesson is in check_e31, and it
                // applies to the benchmark too.
                if (!is_art && cfg[arm].overflow_migrate) {
                    const auto& C4 = validation::e25::counts();
                    if (C4.migrate_calls == 0)
                        throw std::runtime_error("R1: the drain was never attempted");
                    if (C4.migrated_records == 0)
                        throw std::runtime_error("R1: the drain moved no record");
                }
#endif

                // E35 non-vacuity: the queue phase must actually have entered the overflow
                // container, or "0 comparisons" would read as "nothing to decide" rather than
                // "nothing measured". The assertion is on ENTERING overflow, not on entering
                // the BINARY path: with containers averaging about ten records, the frozen
                // Hybrid cutoff sends most queries down the linear scan, so a zero binary
                // count is a finding and not a failure. The counters exist only in the
                // diagnostic build, which is the build that reports this.
#ifdef E25_DIAG
                if (!is_art && cfg[arm].overflow_stable_store) {
                    const auto& C2 = validation::e25::counts();
                    if (C2.ov_queries_bin + C2.ov_queries_linear == 0)
                        throw std::runtime_error("E35: the queue phase never entered the overflow "
                                                 "container, so the prefix statistic is empty");
                    if (C2.ov_linear_recs == 0)
                        throw std::runtime_error("E35: the linear scan examined no record, so the "
                                                 "tag/prefix statistic is empty");
                }
#endif
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

// E38 / P3: the three-arm model comparison -- M0, the LITS backend, and ART.
//
// Derived from bench_tag.cpp. That file and its build record are untouched: it produced
// results/main/tag_*.jsonl, which RESULTS_E36.md cites.
//
// THE ARMS.
//
//   M0     the frozen reference model. The baseline.
//   LITS   the same storage, the same switches, the same region boundaries and G -- with
//          Config::model_backend = LitsHpt. This is the arm the round is about.
//   ART    the same-round external baseline, rebuilt here rather than carried over from
//          E21/E25, because a ratio across rounds is not a same-round measurement.
//
// THE LITS ARM IS A MIXED ARM AND IS NAMED THAT WAY. Regions whose calibration fails take the
// Context16 fallback (task book section 3.4), so the arm is "HPT + Context16 region fallback"
// and the record carries the coverage that says how much of it was which -- measured over the
// index as it stands AT THE CHECKPOINT, not accumulated over every region ever constructed.
//
// WHAT DIFFERS BETWEEN THE ARMS, AND WHAT DOES NOT.
//
//   differs      the group mapping, and therefore the demand profile, the capacity table, the
//                sub-block layout, the overflow content and the compaction counts
//   does not     the input, the region boundaries, the group COUNT, the main-block budget, the
//                switch set, and the capacity-planning algorithm
//
// Each arm gets its OWN plan, built from its OWN demand profile. Applying the reference model's
// plan to the LITS arm would fund a layout the LITS mapping never asked for, and every result
// would then be a statement about that mismatch instead of about the model.
//
// WHAT THIS MEASURES. Build, static point query and resident space are the first three
// questions; the mapping, the fingerprint bytes, the sub-block visits and the overflow probes
// are separate diagnostic builds. Every ratio is taken WITHIN one repetition, never across
// processes or rounds.

// E36:
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
        Config cfg[2] = {base, base};
        cfg[0].compact_reuse_tag = true;
        cfg[0].compact_needs_garbage = true;                   // M0 = frozen = the baseline
        cfg[1].compact_reuse_tag = true;
        cfg[1].compact_needs_garbage = true;
        cfg[1].model_backend = validation::ModelBackend::LitsHpt;   // LITS = M0 + the backend
        // Registered against the one field table every gate shares. A switch missing from it
        // makes these assertions pass vacuously -- this project has already shipped that bug
        // once, which is why require_only_switches_differ exists. BOTH pairs are asserted:
        // T must differ from O in the tag switch alone, or T/O measures something else.
        // The arm difference is DECLARED and asserted. A switch missing from the shared field
        // table makes this pass vacuously -- this project has shipped that bug once (E30), which
        // is why the table exists and why a new switch must be added to it in the same commit.
        e25sw::require_only_switches_differ(cfg[0], cfg[1], {"model_backend"});
        const char* names[3] = {"M0", "LITS", "ART"};
        // ART is not a Config, so cfg stays three wide and the ART arm is taken by index in
        // the loop below.
        // Which arm carries the LITS backend. Named rather than spelled `arm == 1` at
        // every use, so a reordering of the arms cannot silently move the label.
        const int LITS_ARM = 1;
        const int ARMS = 3;
        const int ART_ARM = 2;

        // ONE PLAN PER ARM, from that arm's own demand profile.
        //
        // The mapping decides the demand; the demand decides the capacity table. Handing the
        // reference model's plan to the LITS arm would fund a layout the LITS mapping never
        // asked for, and the comparison would then be about that mismatch rather than about the
        // model. The region boundaries, the group COUNT and the planning ALGORITHM stay the
        // same across arms -- only the measured demand differs, which is the experiment.
        Pipeline plan[2];
        for (int a = 0; a < 2; ++a) {
            Index probe(cfg[a]);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan[a] = make_plan(demand);
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
        std::ofstream out(("results/main/lits_adapter_diag_" + mode + ".jsonl").c_str(),
                          std::ios::app);
#else
        std::ofstream out(("results/main/lits_adapter_" + mode + ".jsonl").c_str(),
                          std::ios::app);
#endif
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (int r = 0; r < reps; ++r) {
            // Rotate which arm goes first, so no arm is systematically first or last.
            //
            // THREE arms over THREE repetitions puts every arm in every position exactly
            // once. The array was four wide when this file was derived from a four-arm
            // benchmark; the loop below reads only the first ARMS entries, so the fourth
            // was never used -- but it was a duplicate of the first, and the comment still
            // described a four-arm rotation.
            //
            // POSITION BALANCE IS NOT ORDER INDEPENDENCE. It removes the systematic
            // advantage of going first, not every carry-over between arms; the driver
            // reports the order it produced so a reader can see what was actually run.
            static_assert(ARMS == 3, "the rotation below is written for three arms");
            int order[ARMS] = {r % ARMS, (r + 1) % ARMS, (r + 2) % ARMS};
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
                const bool is_lits_arm = !is_art && arm == LITS_ARM;

                // THE FULL BUILD, TIMED ON ITS OWN.
                //
                // `cum_ms` is the cumulative maintenance up to a checkpoint, so it contains the
                // build PLUS the replacement cycles that got there -- reporting it as the build
                // time would fold two quantities together. The first round of P3 asks for the
                // build by itself, so it is measured by itself.
                //
                // For the LITS arm this window contains the shared HPT's training, because
                // Index::bulk_load_with_plan calls ensure_lits_shared. That is deliberate: the
                // model's training cost is part of what building with this backend costs, and
                // hoisting it outside the timer would hide it.
                //
                // All three arms are timed the same way, which is the rule the reviewer set for
                // anything that changes how the arms co-reside.
                double build_ms = 0.0;
                {
                    const auto t_build = Clock::now();

                    if (is_art) {
                        art = std::make_unique<e22art::NativeArt>(keys);
                    } else {
                        s = std::make_unique<Index>(cfg[arm]);
                        s->bulk_load_with_plan(keys, sg_of(plan[arm].s_alloc), bb_of(plan[arm].s_alloc), plan[arm].s_caps);
                    }

                    build_ms = ms_between(t_build, Clock::now());
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

                        // THE QUERY SHARE OF THE FALLBACK, counted OUTSIDE the timed span.
                        //
                        // The region and record shares follow from the index's own state. This one
                        // does not: which queries LAND in a fallback region depends on the query
                        // sequence, so it has to be counted while the sequence is in hand. Only
                        // the LITS arm has anything to report here -- on M0 every region is
                        // reference-mapped by definition, and counting it would just print 100%.
                        std::uint64_t q_fallback[3] = {0, 0, 0};

                        if (is_lits_arm) {
                            for (int c = 0; c < 3; ++c) {
                                for (const auto& k : q[c]) {
                                    if (!ix.region(ix.which_region(k)).uses_lits()) ++q_fallback[c];
                                }
                            }
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
                                << ",\"lits_fallback_queries\":" << q_fallback[c]
                                << ",\"lits_queries\":" << q[c].size()
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
                    // The LITS coverage OF THIS STATE, walked over the live regions. Not the
                    // cumulative construction counter: a round that quotes the cumulative figure
                    // beside a timed state is quoting the wrong denominator, and the two differ by
                    // a factor of two on this workload.
                    const validation::Index::LitsCoverage lc =
                        is_art ? validation::Index::LitsCoverage{} : s->lits_coverage();

                    out << "{\"phase\":\"maint\",\"kind\":\"" << kind << "\",\"rep\":" << r
                        << ",\"arm\":\"" << names[arm] << "\",\"run_tag\":\"" << run_tag << "\""
                        << ",\"mode\":\"" << mode << "\",\"frac\":" << frac
                        << ",\"cycles\":" << done
                        << ",\"live\":" << (is_art ? art->size() : s->size())
                        << ",\"live0\":" << live0
                        << ",\"cum_ms\":" << cum
                        // The build on its own, distinct from the cumulative figure below it.
                        // Emitted on every checkpoint record rather than only the first, so the
                        // analysis pairs records field-for-field without a special case.
                        << ",\"build_ms\":" << build_ms
                        << ",\"overflow\":" << st_overflow
                        << ",\"compactions\":" << st_compactions
                        << ",\"memory\":" << st_memory
                        << ",\"lits_regions\":" << lc.regions
                        << ",\"lits_regions_mapped\":" << lc.regions_lits
                        << ",\"lits_records\":" << lc.records
                        << ",\"lits_records_mapped\":" << lc.records_lits
                        << ",\"lits_reason_admission\":" << lc.reason_admission
                        << ",\"lits_reason_degenerate\":" << lc.reason_degenerate
                        << ",\"lits_reason_boundary\":" << lc.reason_boundary
                        << ",\"lits_reason_unsupported\":" << lc.reason_unsupported
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
                if (!is_art && cfg[arm].overflow_inline_tag) {
                    const auto& C3 = validation::e25::counts();
                    if (C3.ov_tag_reject_pre == 0)
                        throw std::runtime_error("T: no candidate was ever rejected by an entry's tag");
                    if (C3.ov_tag_entries == 0)
                        throw std::runtime_error("T: the linear scan examined no entry");
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

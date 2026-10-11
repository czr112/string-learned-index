// Four-arm COMBINATION benchmark: does the widened sub-block reclaim keep its benefit when
// the compact region routing is also on, and vice versa -- on the SAME index state, the SAME
// workload and the SAME query sequences?
//
// ARMS. B = the shipped configuration. W = B + subblock_reclaim + reclaim_widest (the
// accepted WIDE pair; the pair is NOT re-split into its two switches here). C = B +
// compact_region_routing. WC = both. Every pair's exact switch difference is asserted with
// require_only_switches_differ, so an arm that silently became a copy of another fails the
// run instead of passing a comparison that never happened. Ratios are reported directly
// (W/B, C/B, WC/W, WC/C, WC/B) and never multiplied out of historical round figures.
//
// ONE BACKEND PER RUN. G5 showed the WIDE benefit is strongly backend-mapped, so Context16
// and LitsHpt are separate invocations of this same binary and no ratio is ever pooled
// across them.
//
// CHECKPOINTS: static (0) and 25 / 50 / 100 / 200% of N replacement cycles with the live
// count held constant -- the same shape G3/E26/E31/E37 used, so these numbers can be read
// against theirs.
//
// THE QUERY SETS ARE PRE-BUILT AND SHARED. The replacement schedule is pure data (it does
// not read or write any index), so the live-key population at every checkpoint is identical
// across arms and repetitions. This program builds it ONCE, generates the hit and miss probe
// sets and the scan starts from it, and records their hashes so the analyzer can check that
// all four arms really queried the same keys rather than assume it.
//
// TIME FIRST, VERIFY SECOND -- the opposite order of bench_routing, and deliberately.
// The digest directory is rebuilt lazily by the FIRST locate() after a structural change.
// This round's plan requires that rebuild to be CHARGED, not warmed away: so the first pass
// over the probes is the timed pass, and the verifying pass follows it. A mismatch is
// counted, never thrown: the run still produces the record saying what was wrong, and the
// analyzer refuses every cell whose mismatch count is nonzero. For the same reason the scan
// phases are timed before they are verified, and the hit phase runs before the miss phase,
// so the rebuild lands in ONE identified field instead of being smeared.
//
// The counters (E25_DIAG build) are reset per arm-run: they are process-global and the arms
// run one after the other, so without the reset a later arm would inherit an earlier arm's
// figures. The clean timing build compiles them out entirely.
//
// Usage:  bench_combo KIND N REPS [TRANCOCSV] [RUNTAG] [MODE] [BACKEND] [OUTSTEM]
//         MODE: same | grow        BACKEND: ctx16 | lits

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

using namespace validation;
using namespace validation::e9alloc;
using Clock = std::chrono::steady_clock;
using Reference = std::map<std::string, std::uint64_t, Less>;

namespace {

struct Arm {
    const char* name;
    bool wide;
    bool routing;
    bool ordered;   // scan_ordered_start (candidate S)
};

#ifdef E25_ARMS_S
// The scan-entry round's arm set: B / W / S / W+S. Same binary, same schedule, one index
// state per arm; S is the ordered start advance on top of each configuration. The C/WC
// arms are NOT part of this round's comparison and are replaced wholesale rather than
// pooled, so a ratio can never silently mix a navigation arm into an S table.
const Arm ARMS[4] = {
    {"B", false, false, false},
    {"W", true, false, false},
    {"S", false, false, true},
    {"WS", true, false, true},
};
#elif defined(E25_ARMS_H)
// The candidate-H round arm set: W / WH / W / WH. Four SLOTS, two configurations: the
// framework rotates which arm goes first, so the duplicates balance WHERE each
// configuration sat in the run order (the plan's W0/W1/H0/H1 scheme). The analyzer pairs
// WH against W within the same process and repetition; the duplicate slots are a position
// device and are never pooled as an independent configuration.
const Arm ARMS[4] = {
    {"W", true, false, false},
    {"WH", true, false, false},
    {"W", true, false, false},
    {"WH", true, false, false},
};
#else
const Arm ARMS[4] = {
    {"B", false, false, false},
    {"W", true, false, false},
    {"C", false, true, false},
    {"WC", true, true, false},
};
#endif

constexpr int kArms = 4;

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
            .count());
}

// unsigned-byte order, the order the contract and the index both use
bool ubyte_less(std::string_view a, std::string_view b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) {
                                            return static_cast<unsigned char>(x) <
                                                   static_cast<unsigned char>(y);
                                        });
}

struct Probe {
    std::string key;
    bool expect_hit = false;
    std::uint64_t expect_value = 0;
};

std::uint64_t hash_probes(const std::vector<Probe>& probes) {
    std::uint64_t h = 1469598103934665603ULL;

    for (const auto& p : probes) {
        for (unsigned char c : p.key) {
            h = (h ^ c) * 1099511628211ULL;
        }

        h = (h ^ (p.expect_hit ? 1u : 0u)) * 1099511628211ULL;
    }

    return h;
}

struct Phase {
    std::uint64_t ns = 0;
    std::uint64_t checksum = 0;
    std::uint64_t probes = 0;
    std::uint64_t expected_hits = 0;
    std::uint64_t observed_hits = 0;
    std::uint64_t mismatches = 0;
};

// ---------------------------------------------------------------------------------
// Scan-entry round (2026-10-10): PER-CALL counter deltas.
//
// The round before this one read each checkpoint's counters CUMULATIVELY, so the
// maintenance work, every earlier checkpoint, the timed passes and the verifying pass
// were all mixed into one number. That can show that a count differs; it cannot say
// which scan paid it. Here every scan(start, limit) call is bracketed by a snapshot and
// the delta becomes its own JSONL row -- including WHICH start it was. The rows go to
// their own file, because the main .jsonl schema is read by earlier analyzers and must
// not gain or lose a field underneath them.
//
// The context type exists in every build so the call sites stay uniform; the snapshot
// and emission code is E25_DIAG-only, and so are the counters it reads. E25_INC is a
// no-op outside the diagnostic build, so the timed loop is that same loop.
// ---------------------------------------------------------------------------------
struct ScanCounterSnapshot {
    std::uint64_t adv_p_cmps = 0, adv_p_skipped = 0, adv_p_subblocks = 0;
    std::uint64_t adv_o_cmps = 0, adv_o_skipped = 0;
    std::uint64_t out_primary = 0, out_overflow = 0, out_bytes = 0;
    std::uint64_t regions = 0, groups = 0, groups_empty = 0, blocks_skipped = 0;
    std::uint64_t merge_cmps = 0, result_allocs = 0, start_locates = 0;
    std::uint64_t cursor_starts = 0, ov_cursor_starts = 0, canonical = 0;
};

struct ScanWorkCtx {
    std::ostream* out = nullptr;          // null = no rows are written (timing build)
    std::string kind, run_tag, mode, backend, arm;
    bool wide = false, routing = false;
    long long rep = 0, position = 0;
    std::uint64_t n = 0, thr = 0;
    double frac = 0.0;
};

#ifdef E25_DIAG
ScanCounterSnapshot scan_snapshot() {
    const auto& c = validation::e25::counts();
    ScanCounterSnapshot s;
    s.adv_p_cmps = c.scan_adv_p_cmps;
    s.adv_p_skipped = c.scan_adv_p_skipped;
    s.adv_p_subblocks = c.scan_adv_p_subblocks;
    s.adv_o_cmps = c.scan_adv_ov_cmps;
    s.adv_o_skipped = c.scan_adv_ov_skipped;
    s.out_primary = c.scan_out_primary;
    s.out_overflow = c.scan_out_overflow;
    s.out_bytes = c.scan_out_bytes;
    s.regions = c.scan_regions;
    s.groups = c.scan_groups;
    s.groups_empty = c.scan_groups_empty;
    s.blocks_skipped = c.scan_blocks_skipped;
    s.merge_cmps = c.scan_merge_cmps;
    s.result_allocs = c.scan_result_allocs;
    s.start_locates = c.scan_start_locates;
    s.cursor_starts = c.scan_cursor_starts;
    s.ov_cursor_starts = c.scan_ov_cursor_starts;
    s.canonical = c.scan_canonical_group;
    return s;
}

ScanCounterSnapshot scan_delta(const ScanCounterSnapshot& b, const ScanCounterSnapshot& a) {
    ScanCounterSnapshot d;
    d.adv_p_cmps = a.adv_p_cmps - b.adv_p_cmps;
    d.adv_p_skipped = a.adv_p_skipped - b.adv_p_skipped;
    d.adv_p_subblocks = a.adv_p_subblocks - b.adv_p_subblocks;
    d.adv_o_cmps = a.adv_o_cmps - b.adv_o_cmps;
    d.adv_o_skipped = a.adv_o_skipped - b.adv_o_skipped;
    d.out_primary = a.out_primary - b.out_primary;
    d.out_overflow = a.out_overflow - b.out_overflow;
    d.out_bytes = a.out_bytes - b.out_bytes;
    d.regions = a.regions - b.regions;
    d.groups = a.groups - b.groups;
    d.groups_empty = a.groups_empty - b.groups_empty;
    d.blocks_skipped = a.blocks_skipped - b.blocks_skipped;
    d.merge_cmps = a.merge_cmps - b.merge_cmps;
    d.result_allocs = a.result_allocs - b.result_allocs;
    d.start_locates = a.start_locates - b.start_locates;
    d.cursor_starts = a.cursor_starts - b.cursor_starts;
    d.ov_cursor_starts = a.ov_cursor_starts - b.ov_cursor_starts;
    d.canonical = a.canonical - b.canonical;
    return d;
}

std::uint64_t fnv1a64(std::string_view s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ULL;
    return h;
}

void emit_scan_work(const ScanWorkCtx& ctx, std::size_t limit, const char* phase, int pass,
                    long long start_idx, const std::string& probe_id, std::string_view start,
                    std::size_t rows, std::size_t expect_rows, bool bytes_checked,
                    std::uint64_t bytes_mismatch, const ScanCounterSnapshot& d) {
    if (!ctx.out) return;
    std::ostream& o = *ctx.out;
    o << "{\"kind\":\"" << ctx.kind << "\",\"run_tag\":\"" << ctx.run_tag
      << "\",\"mode\":\"" << ctx.mode << "\",\"backend\":\"" << ctx.backend
      << "\",\"arm\":\"" << ctx.arm << "\",\"wide\":" << (ctx.wide ? "true" : "false")
      << ",\"routing\":" << (ctx.routing ? "true" : "false") << ",\"rep\":" << ctx.rep
      << ",\"position\":" << ctx.position << ",\"n\":" << ctx.n << ",\"thr\":" << ctx.thr
      << ",\"frac\":" << ctx.frac << ",\"limit\":" << limit << ",\"phase\":\"" << phase
      << "\",\"pass\":" << pass << ",\"probe_id\":\"" << probe_id << "\",\"start_idx\":"
      << start_idx << ",\"start_len\":" << start.size() << ",\"start_fnv\":" << fnv1a64(start)
      << ",\"rows\":" << rows << ",\"expect_rows\":" << expect_rows
      << ",\"bytes_checked\":" << (bytes_checked ? "true" : "false")
      << ",\"bytes_mismatch\":" << bytes_mismatch
      << ",\"adv_p_cmps\":" << d.adv_p_cmps << ",\"adv_p_skipped\":" << d.adv_p_skipped
      << ",\"adv_p_subblocks\":" << d.adv_p_subblocks << ",\"adv_o_cmps\":" << d.adv_o_cmps
      << ",\"adv_o_skipped\":" << d.adv_o_skipped << ",\"out_primary\":" << d.out_primary
      << ",\"out_overflow\":" << d.out_overflow << ",\"out_bytes\":" << d.out_bytes
      << ",\"scan_regions\":" << d.regions << ",\"scan_groups\":" << d.groups
      << ",\"scan_groups_empty\":" << d.groups_empty
      << ",\"scan_blocks_skipped\":" << d.blocks_skipped
      << ",\"scan_merge_cmps\":" << d.merge_cmps
      << ",\"scan_result_allocs\":" << d.result_allocs
      << ",\"scan_start_locates\":" << d.start_locates
      << ",\"scan_cursor_starts\":" << d.cursor_starts
      << ",\"scan_ov_cursor_starts\":" << d.ov_cursor_starts
      << ",\"scan_canonical\":" << d.canonical << "}" << std::endl;
}
#endif

// Time first, then verify -- see the header note on why the order is inverted relative to
// bench_routing. The verifying pass runs outside every timed span so a mismatch cannot slow
// the measurement.
Phase run_phase(const Index& index, const std::vector<Probe>& probes, const Mode& mode,
                int replay) {
    Phase p;
    p.probes = probes.size();

    for (const auto& probe : probes) {
        if (probe.expect_hit) ++p.expected_hits;
    }

    const std::uint64_t t0 = now_ns();

    for (int rep = 0; rep < replay; ++rep) {
        for (const auto& probe : probes) {
            const Result r = index.find(probe.key, mode);
            p.checksum += r.found ? (r.value + 1) : 0x9E3779B97F4A7C15ULL;
        }
    }

    p.ns = now_ns() - t0;

    for (const auto& probe : probes) {
        const Result r = index.find(probe.key, mode);

        if (r.found != probe.expect_hit) {
            ++p.mismatches;
        } else if (probe.expect_hit && r.value != probe.expect_value) {
            ++p.mismatches;
        }

        if (r.found) ++p.observed_hits;
    }

    return p;
}

struct ScanPhase {
    std::uint64_t ns = 0;
    std::uint64_t starts = 0;
    std::uint64_t rows = 0;
    std::uint64_t mismatches = 0;
};

// Scan: timed first (one recorded pass's rows), verified against the reference second, row
// by row. The reference is the same std::map the probes came from, so "the scan returned
// something" is never accepted as "the scan returned the right rows".
//
// Scan-entry round: when the diagnostic sink is live, EVERY call is bracketed by counter
// snapshots and written out as its own row -- both timed passes, the verifying pass, and
// the boundary probes. The probes run after the verification and were never part of the
// timed queue the earlier rounds replayed, so their readings keep their meaning. All of
// the snapshot code is compiled out of every timing build.
ScanPhase run_scan(const Index& index, const std::vector<std::string>& starts, std::size_t limit,
                   int replay, const std::vector<Pair>& live_pairs, ScanWorkCtx* work,
                   const std::vector<std::pair<std::string, std::string>>& probes) {
#ifndef E25_DIAG
    (void)work;
    (void)probes;
#endif
    ScanPhase p;
    p.starts = starts.size();

#ifdef E25_DIAG
    const auto expect_rows_of = [&live_pairs, limit](std::string_view s) {
        const auto it = std::lower_bound(live_pairs.begin(), live_pairs.end(), s,
                                         [](const Pair& x, std::string_view y) {
                                             return compare(x.first, y) < 0;
                                         });
        return std::min<std::size_t>(limit, std::size_t(live_pairs.end() - it));
    };
#endif

    const std::uint64_t t0 = now_ns();
    std::size_t rows = 0;

    for (int rep = 0; rep < replay; ++rep) {
        std::size_t acc = 0;

        for (std::size_t j = 0; j < starts.size(); ++j) {
            const std::string& s = starts[j];
#ifdef E25_DIAG
            const ScanCounterSnapshot before = scan_snapshot();
#endif
            const auto out = index.scan(s, limit);
#ifdef E25_DIAG
            if (work && work->out) {
                const ScanCounterSnapshot d = scan_delta(before, scan_snapshot());
                emit_scan_work(*work, limit, "timed", rep, static_cast<long long>(j), "", s,
                               out.size(), expect_rows_of(s), false, 0, d);
            }
#endif
            acc += out.size();
        }

        rows = acc;
    }

    p.ns = now_ns() - t0;
    p.rows = rows;

    for (std::size_t j = 0; j < starts.size(); ++j) {
        const std::string& s = starts[j];
#ifdef E25_DIAG
        const ScanCounterSnapshot before = scan_snapshot();
#endif
        const auto out = index.scan(s, limit);
        auto it = std::lower_bound(live_pairs.begin(), live_pairs.end(), s,
                                   [](const Pair& x, std::string_view y) {
                                       return compare(x.first, y) < 0;
                                   });
        const std::size_t expect =
            std::min<std::size_t>(limit, std::size_t(live_pairs.end() - it));

        if (out.size() != expect) {
            ++p.mismatches;
        } else {
            std::size_t i = 0;

            for (auto t = it; t != live_pairs.end() && i < out.size(); ++t, ++i) {
                if (out[i].first != t->first || out[i].second != t->second) {
                    ++p.mismatches;
                    break;
                }
            }
        }

#ifdef E25_DIAG
        if (work && work->out) {
            const ScanCounterSnapshot d = scan_delta(before, scan_snapshot());
            std::size_t produced = 0;
            for (const auto& kv : out) produced += kv.first.size();
            emit_scan_work(*work, limit, "verify", 0, static_cast<long long>(j), "", s,
                           out.size(), expect, true, d.out_bytes == produced ? 0ULL : 1ULL, d);
        }
#endif
    }

#ifdef E25_DIAG
    if (work && work->out) {
        for (const auto& pr : probes) {
            const std::string& key = pr.second;
            const ScanCounterSnapshot before = scan_snapshot();
            const auto out = index.scan(key, limit);
            const ScanCounterSnapshot d = scan_delta(before, scan_snapshot());
            const std::size_t expect = expect_rows_of(key);
            std::size_t produced = 0;
            for (const auto& kv : out) produced += kv.first.size();

            // Probes are verified like the timed queue is: counts against the reference,
            // then row by row on the answers. A failure lands in p.mismatches, which the
            // analyzer already refuses to accept as nonzero.
            bool ok = out.size() == expect;

            if (ok) {
                const auto it = std::lower_bound(live_pairs.begin(), live_pairs.end(), key,
                                                 [](const Pair& x, std::string_view y) {
                                                     return compare(x.first, y) < 0;
                                                 });
                std::size_t i = 0;

                for (auto t = it; t != live_pairs.end() && i < out.size(); ++t, ++i) {
                    if (out[i].first != t->first || out[i].second != t->second) {
                        ok = false;
                        break;
                    }
                }
            }

            if (!ok) ++p.mismatches;

            emit_scan_work(*work, limit, "probe", 0, -1, pr.first, key, out.size(), expect, true,
                           d.out_bytes == produced ? 0ULL : 1ULL, d);
        }
    }
#endif

    return p;
}

#ifdef E25_SCAN_CONSUME
// ---------------------------------------------------------------------------------
// Scan-entry round, stage B2: the ONE bounded distinguishing experiment.
//
// Runs the SAME start/limit queue through two consumption modes on the SAME index
// state, alternating which mode goes first, and emits one row per timed call:
//
//   mat   -- index.scan(start, limit): the shipped path, result strings built.
//   cons  -- index.scan_consume(start, limit, sink): identical traversal, rows
//            delivered as string_views, no result string built.
//
// The question it answers: does the W/B gap live in the traversal or in building the
// rows? It is a discriminator, not a candidate: neither the difference between the
// two modes nor their absolute times are claimed as an improvement. Answers are
// verified row by row against the reference table in an untimed pass, and the same
// verify pass records a row checksum so the analyzer can prove both arms and both
// modes returned the same rows.
// ---------------------------------------------------------------------------------
template <class CheckpointT>
void run_scan_consume(const Index& index, const CheckpointT& cp, int rep, int position,
                      const Arm& arm, const std::string& kind, const std::string& run_tag,
                      const std::string& mode, const std::string& backend_name,
                      std::uint64_t N, std::uint64_t thr, std::ostream& out) {
    const std::size_t kLimits[3] = {16, 256, 4096};
    const int kPasses = 3;
    std::uint64_t acc_rows = 0, acc_bytes = 0;

    for (std::size_t li = 0; li < 3; ++li) {
        const std::size_t limit = kLimits[li];

        for (int p = 0; p < kPasses; ++p) {
            for (std::size_t si = 0; si < cp.scan_starts.size(); ++si) {
                const std::string& start = cp.scan_starts[si];
                // Alternate which mode is timed first, per start and pass, so a slow
                // first call (cache warmth, allocator state) cannot favour one mode.
                const bool mat_first = ((si + std::size_t(p)) % 2 == 0);

                std::uint64_t mat_ns = 0, cons_ns = 0;
                std::size_t mat_rows = 0, cons_rows = 0;

                for (int which = 0; which < 2; ++which) {
                    const bool do_mat = (which == 0) == mat_first;

                    if (do_mat) {
                        const std::uint64_t t0 = now_ns();
                        const auto rows = index.scan(start, limit);
                        mat_ns = now_ns() - t0;
                        mat_rows = rows.size();
                        acc_rows += mat_rows;
                    } else {
                        Region::ConsumeSink sink;
                        sink.timed_fast = true;
                        const std::uint64_t t0 = now_ns();
                        index.scan_consume(start, limit, sink);
                        cons_ns = now_ns() - t0;
                        cons_rows = static_cast<std::size_t>(sink.rows);
                        acc_bytes += sink.bytes;
                    }
                }

                // run_tag identifies the PROCESS: three processes append to this stem, and
                // the analyzer pairs W against B within one process and one repetition --
                // without it the pairing would be an assumption instead of a check.
                out << "{\"kind\":\"" << kind << "\",\"run_tag\":\"" << run_tag
                    << "\",\"mode\":\"" << mode
                    << "\",\"backend\":\"" << backend_name << "\",\"arm\":\"" << arm.name
                    << "\",\"rep\":" << rep << ",\"position\":" << position
                    << ",\"n\":" << N << ",\"thr\":" << thr << ",\"frac\":" << cp.frac
                    << ",\"limit\":" << limit << ",\"start_idx\":" << si
                    << ",\"pass\":" << p << ",\"mode_name\":\"mat\",\"ns\":" << mat_ns
                    << ",\"rows\":" << mat_rows << "}" << std::endl;
                out << "{\"kind\":\"" << kind << "\",\"run_tag\":\"" << run_tag
                    << "\",\"mode\":\"" << mode
                    << "\",\"backend\":\"" << backend_name << "\",\"arm\":\"" << arm.name
                    << "\",\"rep\":" << rep << ",\"position\":" << position
                    << ",\"n\":" << N << ",\"thr\":" << thr << ",\"frac\":" << cp.frac
                    << ",\"limit\":" << limit << ",\"start_idx\":" << si
                    << ",\"pass\":" << p << ",\"mode_name\":\"cons\",\"ns\":" << cons_ns
                    << ",\"rows\":" << cons_rows << "}" << std::endl;
            }
        }

        // ------------------------------------------------ untimed verification
        for (std::size_t si = 0; si < cp.scan_starts.size(); ++si) {
            const std::string& start = cp.scan_starts[si];
            const auto out_mat = index.scan(start, limit);

            std::vector<Pair> captured;
            Region::ConsumeSink vs;
            vs.capture = &captured;
            index.scan_consume(start, limit, vs);

            const auto it = std::lower_bound(cp.live_pairs.begin(), cp.live_pairs.end(), start,
                                             [](const Pair& x, std::string_view y) {
                                                 return compare(x.first, y) < 0;
                                             });
            const std::size_t expect =
                std::min<std::size_t>(limit, std::size_t(cp.live_pairs.end() - it));

            std::uint64_t mismatches = 0;
            std::uint64_t hash = 1469598103934665603ULL;

            if (out_mat.size() != expect || captured.size() != expect) {
                ++mismatches;
            } else {
                std::size_t i = 0;

                for (auto t = it; t != cp.live_pairs.end() && i < expect; ++t, ++i) {
                    if (out_mat[i].first != t->first || out_mat[i].second != t->second ||
                        captured[i].first != t->first || captured[i].second != t->second) {
                        ++mismatches;
                        break;
                    }

                    for (unsigned char c : t->first) hash = (hash ^ c) * 1099511628211ULL;
                    hash = (hash ^ t->second) * 1099511628211ULL;
                }
            }

            out << "{\"kind\":\"" << kind << "\",\"run_tag\":\"" << run_tag
                << "\",\"mode\":\"" << mode
                << "\",\"backend\":\"" << backend_name << "\",\"arm\":\"" << arm.name
                << "\",\"rep\":" << rep << ",\"position\":" << position
                << ",\"n\":" << N << ",\"thr\":" << thr << ",\"frac\":" << cp.frac
                << ",\"limit\":" << limit << ",\"start_idx\":" << si
                << ",\"mode_name\":\"verify\",\"rows\":" << out_mat.size()
                << ",\"expect_rows\":" << expect
                << ",\"rows_cons\":" << captured.size()
                << ",\"mismatches\":" << mismatches
                << ",\"hash\":" << hash << "}" << std::endl;
        }
    }

    std::cerr << "  scanconsume " << arm.name << " frac=" << cp.frac << " rows=" << acc_rows
              << " cons_bytes=" << acc_bytes << std::endl;
}
#endif

struct Pipeline {
    std::vector<Alloc> s_alloc;
    std::vector<std::vector<std::uint16_t>> s_caps;
};

Pipeline make_plan(const std::vector<std::vector<GroupStats>>& demand) {
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

std::vector<std::vector<std::uint16_t>> sg_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint16_t>> o;

    for (const auto& a : v) o.push_back(a.sg);

    return o;
}

std::vector<std::vector<std::uint32_t>> bb_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint32_t>> o;

    for (const auto& a : v) o.push_back(a.bb);

    return o;
}

void assemble_arms(Config cfg[4], std::uint32_t thr, ModelBackend backend) {
    for (int a = 0; a < kArms; ++a) {
        cfg[a] = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg[a]);
        cfg[a].model_backend = backend;
    }

#ifdef E25_ARMS_S
    cfg[1].subblock_reclaim = true;
    cfg[1].reclaim_widest = true;
    cfg[2].scan_ordered_start = true;
    cfg[3].subblock_reclaim = true;
    cfg[3].reclaim_widest = true;
    cfg[3].scan_ordered_start = true;

    e25sw::require_only_switches_differ(cfg[0], cfg[2], {"scan_ordered_start"});
    e25sw::require_only_switches_differ(cfg[0], cfg[1], {"subblock_reclaim", "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[1], cfg[3], {"scan_ordered_start"});
    e25sw::require_only_switches_differ(cfg[2], cfg[3], {"subblock_reclaim", "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[0], cfg[3],
                                        {"scan_ordered_start", "subblock_reclaim",
                                         "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[1], cfg[2],
                                        {"scan_ordered_start", "subblock_reclaim",
                                         "reclaim_widest"});
#elif defined(E25_ARMS_H)
    // W / WH / W / WH: the ONE varying field is primary_hole_reuse, asserted below.
    cfg[0].subblock_reclaim = true;
    cfg[0].reclaim_widest = true;
    cfg[1].subblock_reclaim = true;
    cfg[1].reclaim_widest = true;
    cfg[1].primary_hole_reuse = true;
    cfg[2].subblock_reclaim = true;
    cfg[2].reclaim_widest = true;
    cfg[3].subblock_reclaim = true;
    cfg[3].reclaim_widest = true;
    cfg[3].primary_hole_reuse = true;

    e25sw::require_only_switches_differ(cfg[0], cfg[1], {"primary_hole_reuse"});
    e25sw::require_only_switches_differ(cfg[2], cfg[3], {"primary_hole_reuse"});
    e25sw::require_only_switches_differ(cfg[0], cfg[2], {});
    e25sw::require_only_switches_differ(cfg[1], cfg[3], {});
#else
    cfg[1].subblock_reclaim = true;
    cfg[1].reclaim_widest = true;
    cfg[2].compact_region_routing = true;
    cfg[3].subblock_reclaim = true;
    cfg[3].reclaim_widest = true;
    cfg[3].compact_region_routing = true;

    e25sw::require_only_switches_differ(cfg[0], cfg[2], {"compact_region_routing"});
    e25sw::require_only_switches_differ(cfg[0], cfg[1], {"subblock_reclaim", "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[1], cfg[3], {"compact_region_routing"});
    e25sw::require_only_switches_differ(cfg[2], cfg[3], {"subblock_reclaim", "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[0], cfg[3],
                                        {"compact_region_routing", "subblock_reclaim",
                                         "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[1], cfg[2],
                                        {"compact_region_routing", "subblock_reclaim",
                                         "reclaim_widest"});
#endif
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 4)
            throw std::invalid_argument(
                "bench_combo KIND N REPS [TRANCOCSV] [RUNTAG] [MODE] [BACKEND] [OUTSTEM]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argc > 4 ? argv[4] : "";
        const std::string run_tag = argc > 5 ? argv[5] : "";
        const std::string mode = argc > 6 ? argv[6] : "grow";
        const std::string backend_arg = argc > 7 ? argv[7] : "ctx16";
        const std::string out_stem = argc > 8 ? argv[8] : ("combo_" + mode);

        if (mode != "grow" && mode != "same")
            throw std::invalid_argument("MODE must be grow or same");

        if (backend_arg != "ctx16" && backend_arg != "lits")
            throw std::invalid_argument("BACKEND must be ctx16 or lits");

        const bool grow = (mode == "grow");
        const ModelBackend backend =
            backend_arg == "lits" ? ModelBackend::LitsHpt : ModelBackend::Context16;
        const char* backend_name = backend == ModelBackend::LitsHpt ? "LitsHpt" : "Context16";

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

        Config cfg[kArms];
        assemble_arms(cfg, thr, backend);

        // One build plan, made once with the B configuration and reused for every arm-run:
        // the capacity table is an INPUT to the comparison, not part of any arm, so an arm
        // that derived its own would silently compare different data layouts.
        Pipeline plan;
        {
            Index probe(cfg[0]);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;

            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());

            plan = make_plan(demand);
        }

        const auto sg = sg_of(plan.s_alloc);
        const auto bb = bb_of(plan.s_alloc);

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        // ------------------------------------------------ the shared workload schedule
        // Pure data: which key is deleted and which key replaces it. The index is never
        // consulted here, so the schedule -- and therefore every checkpoint's live-key
        // population, probe set and scan starts -- is identical for all four arms and all
        // repetitions. The hashes recorded with every row let the analyzer CHECK that
        // instead of assuming it.
        struct Move {
            std::string victim, fresh;
            std::uint64_t value;
        };

        struct Checkpoint {
            double frac = 0;
            std::vector<Move> seg;
            std::vector<Probe> hit_probes, miss_probes;
            std::vector<Pair> live_pairs;
            std::vector<std::string> scan_starts;
            // Scan-entry round: boundary probes. Diagnostic builds only -- the vector is
            // empty and the argument is ignored in a timing build.
            std::vector<std::pair<std::string, std::string>> scan_probes;
            std::size_t live = 0;
            std::uint64_t hit_hash = 0, miss_hash = 0;
        };

        const std::size_t kHit = 4096, kMiss = 4096;
        const int kReplayQ = 24;
        const int kReplayS = 2;
        const double FRACS[5] = {0.00, 0.25, 0.50, 1.00, 2.00};

        std::vector<Checkpoint> checkpoints;
        {
            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;
            Reference ref;

            for (const auto& kv : keys) {
                live.push_back(kv.first);
                live_set.insert(kv.first);
                ref[kv.first] = kv.second;
            }

            std::uint64_t serial = 0;
            std::size_t done = 0;

            for (double frac : FRACS) {
                Checkpoint cp;
                cp.frac = frac;
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);

                for (std::size_t i = done; i < target; ++i) {
                    const std::string victim = live.front();
                    live.pop_front();
                    live_set.erase(victim);

                    std::string base = victim;
                    const auto pos = base.find("~r");

                    if (pos != std::string::npos) base.resize(pos);

                    std::string fresh;

                    if (grow) {
                        fresh = base + "~r" + std::to_string(serial);
                    } else {
                        fresh = base;
                        bool ok = false;

                        for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                            fresh.back() = static_cast<char>('a' + ((serial + t) % 26));

                            if (fresh != victim && ref.find(fresh) == ref.end() &&
                                live_set.find(fresh) == live_set.end()) {
                                ok = true;
                                break;
                            }
                        }

                        if (!ok)
                            throw std::runtime_error(
                                "same-length replacement exhausted its alphabet");
                    }

                    const std::uint64_t val = 0x7000ULL + serial;
                    ++serial;
                    cp.seg.push_back({victim, fresh, val});
                    live.push_back(fresh);
                    live_set.insert(fresh);
                }

                for (const auto& m : cp.seg) {
                    if (ref.erase(m.victim) != 1)
                        throw std::runtime_error("erase target absent");

                    if (ref.find(m.fresh) != ref.end())
                        throw std::runtime_error("insert target present");

                    ref[m.fresh] = m.value;
                }

                cp.live = ref.size();

                std::vector<Pair> live_pairs;
                live_pairs.reserve(live.size());

                for (const auto& k : live) live_pairs.emplace_back(k, ref.at(k));

                std::sort(live_pairs.begin(), live_pairs.end(), [](const Pair& x, const Pair& y) {
                    return compare(x.first, y.first) < 0;
                });

                std::mt19937_64 rng(20261010ULL + static_cast<std::uint64_t>(frac * 1000.0));

                for (std::size_t i = 0; i < std::min(kHit, live_pairs.size()); ++i) {
                    const Pair& kv = live_pairs[rng() % live_pairs.size()];
                    cp.hit_probes.push_back(Probe{kv.first, true, kv.second});
                }

                for (std::size_t i = 0; i < std::min(kMiss, live_pairs.size()); ++i) {
                    std::string k = live_pairs[rng() % live_pairs.size()].first;
                    k.push_back('\0');
                    k.push_back(static_cast<char>('a' + (rng() % 26)));

                    while (ref.find(k) != ref.end())
                        k.push_back(static_cast<char>('a' + (rng() % 26)));

                    cp.miss_probes.push_back(Probe{k, false, 0});
                }

                cp.hit_hash = hash_probes(cp.hit_probes);
                cp.miss_hash = hash_probes(cp.miss_probes);

                // Scan starts: the empty string -- a scan from the beginning -- plus an even
                // sample of the live keys. The same starts, in the same order, at every
                // checkpoint and for every arm.
                cp.scan_starts.push_back(std::string());

                for (int i = 0; i < 16 && !live_pairs.empty(); ++i)
                    cp.scan_starts.push_back(
                        live_pairs[(std::size_t(i) * live_pairs.size()) / 16].first);

#ifdef E25_DIAG
                // Boundary probes for the scan-entry diagnosis. NOT part of the timed
                // queue -- the earlier rounds' scan readings must stay comparable -- so
                // they run after the verification pass and are written as phase="probe".
                // Every candidate is checked against the live population rather than
                // assumed absent.
                const auto absent_after = [&live_pairs](std::string base) {
                    const auto present = [&live_pairs](const std::string& cand) {
                        const auto it = std::lower_bound(live_pairs.begin(), live_pairs.end(),
                                                         cand,
                                                         [](const Pair& x, const std::string& y) {
                                                             return compare(x.first, y) < 0;
                                                         });
                        return it != live_pairs.end() && it->first == cand;
                    };

                    for (int t = 0; t < 96; ++t) {
                        std::string cand = base;
                        cand.push_back('\0');
                        cand.push_back(static_cast<char>('a' + (t % 26)));

                        if (!present(cand)) return cand;
                    }

                    return base;   // 26 letters against one container: cannot be reached
                };

                if (!live_pairs.empty()) {
                    cp.scan_probes.emplace_back(
                        "absent_mid", absent_after(live_pairs[live_pairs.size() / 2].first));
                    cp.scan_probes.emplace_back("after_last",
                                                absent_after(live_pairs.back().first));
                    cp.scan_probes.emplace_back("first_key", live_pairs.front().first);

                    if (!live_pairs.front().first.empty())
                        cp.scan_probes.emplace_back(
                            "before_first", live_pairs.front().first.substr(
                                               0, live_pairs.front().first.size() - 1));
                }
#endif

                cp.live_pairs = std::move(live_pairs);

                checkpoints.push_back(std::move(cp));
                done = target;
            }
        }

        // ------------------------------------------------------------- the timed sweep
        // Its own file per stem; APPEND, not truncate -- the sweep runs five independent
        // processes, each appending its own records, and the driver archives the working
        // file after every child so an interruption keeps what was collected.
        std::ofstream out(("results/main/" + out_stem + ".jsonl").c_str(), std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        // Scan-entry round: the per-call rows go to their own file (see run_scan). It is
        // appended the same way the main file is; the driver archives it after each child
        // and refuses to reuse an output stem. Outside -DE25_DIAG the sink stays null and
        // no row is ever written.
        ScanWorkCtx work_ctx;
#ifdef E25_SCAN_CONSUME
        std::ofstream consume_out(("results/main/" + out_stem + "_scanconsume.jsonl").c_str(),
                                  std::ios::app);
        consume_out.exceptions(std::ios::badbit | std::ios::failbit);
        consume_out << std::setprecision(10);
#endif
#ifdef E25_DIAG
        std::ofstream work_out(("results/main/" + out_stem + "_scanwork.jsonl").c_str(),
                               std::ios::app);
        work_out.exceptions(std::ios::badbit | std::ios::failbit);
        work_out << std::setprecision(10);
        work_ctx.out = &work_out;
        work_ctx.kind = kind;
        work_ctx.run_tag = run_tag;
        work_ctx.mode = mode;
        work_ctx.backend = backend_name;
        work_ctx.n = N;
        work_ctx.thr = thr;
#endif

        for (int r = 0; r < reps; ++r) {
            // Rotate which arm goes first, so no arm is systematically first or last.
            const int order[kArms] = {r % kArms, (r + 1) % kArms, (r + 2) % kArms,
                                      (r + 3) % kArms};

            for (int oi = 0; oi < kArms; ++oi) {
                const int arm = order[oi];

#ifdef E25_DIAG
                work_ctx.arm = ARMS[arm].name;
                work_ctx.wide = ARMS[arm].wide;
                work_ctx.routing = ARMS[arm].routing;
                work_ctx.rep = r;
                work_ctx.position = oi;
#endif

                const std::uint64_t t_build0 = now_ns();
                auto s = std::make_unique<Index>(cfg[arm]);
#ifdef E25_DIAG
                // The counters are process-global and the arms run one after the other, so
                // without this reset a later arm would inherit an earlier arm's figures.
                validation::e25::counts().reset();
#endif
                s->bulk_load_with_plan(keys, sg, bb, plan.s_caps);
                const std::uint64_t build_ns = now_ns() - t_build0;

                std::uint64_t cum_ns = 0;

                for (const auto& cp : checkpoints) {
                    // The maintenance segment, timed on its own. The cumulative figure is
                    // the sum of the segments, not a second measurement.
                    const std::uint64_t t0 = now_ns();

                    // Interleaved, one replacement at a time: the same shape bench_widest
                    // and the gate use. Batching the erases first (an earlier version of
                    // this device) left the primary blocks full of holes and made the
                    // widening branch unreachable on mixed/skew -- the gate now proves the
                    // branch runs, on THIS shape, before this program is timed.
                    for (const auto& mv : cp.seg) {
                        s->erase(mv.victim);
                        s->insert(mv.fresh, mv.value);
                    }

                    const std::uint64_t seg_ns = now_ns() - t0;
                    cum_ns += seg_ns;

                    if (s->size() != cp.live)
                        throw std::runtime_error("live count drifted from the schedule");

                    // Hit phase FIRST: the lazy directory rebuild (on the C and WC arms) is
                    // charged to hit_ns on the checkpoint that caused it, and to no other
                    // field. The verifying pass is inside run_phase, after the timed span.
                    const Phase hit = run_phase(*s, cp.hit_probes, qmode, kReplayQ);
                    const Phase miss = run_phase(*s, cp.miss_probes, qmode, kReplayQ);

#ifdef E25_DIAG
                    work_ctx.frac = cp.frac;
#endif
                    const ScanPhase s16 =
                        run_scan(*s, cp.scan_starts, 16, kReplayS, cp.live_pairs, &work_ctx,
                                 cp.scan_probes);
                    const ScanPhase s256 =
                        run_scan(*s, cp.scan_starts, 256, kReplayS, cp.live_pairs, &work_ctx,
                                 cp.scan_probes);
                    const ScanPhase s4096 =
                        run_scan(*s, cp.scan_starts, 4096, kReplayS, cp.live_pairs, &work_ctx,
                                 cp.scan_probes);

#ifdef E25_SCAN_CONSUME
                    // Stage B2 discriminator: B and W only. C/WC add nothing here (the
                    // routing touches locate, not the row path) and halve the extra cost.
                    if (std::string(ARMS[arm].name) == "B" || std::string(ARMS[arm].name) == "W")
                        run_scan_consume(*s, cp, r, oi, ARMS[arm], kind, run_tag, mode,
                                         backend_name, N, thr, consume_out);
#endif

                    const auto mb = s->memory_breakdown();
                    const std::size_t index_bytes =
                        mb.blocks + mb.model + mb.prefix + mb.overflow + mb.tables +
                        mb.directory + mb.regions_self + mb.fences + mb.self;

#ifdef E25_DIAG
                    // SCAN WORK COUNTERS. The round's scan readings (grow/tranco up to 3.6x
                    // slower under WIDE) cannot be explained without knowing whether the
                    // SAME rows were reached through more groups, more skipped sub-blocks or
                    // more merge comparisons. These counters answer exactly that, and they
                    // cost nothing here -- this is the diagnostic twin, never a timing build.
                    const auto& C = validation::e25::counts();
                    const std::uint64_t reclaims = C.subblock_reclaims;
                    const std::uint64_t widened = C.reclaim_widened;
                    const std::uint64_t sc_regions = C.scan_regions;
                    const std::uint64_t sc_groups = C.scan_groups;
                    const std::uint64_t sc_groups_empty = C.scan_groups_empty;
                    const std::uint64_t sc_blocks_skipped = C.scan_blocks_skipped;
                    const std::uint64_t sc_merge_cmps = C.scan_merge_cmps;
                    const std::uint64_t sc_result_allocs = C.scan_result_allocs;
                    const std::uint64_t sc_start_locates = C.scan_start_locates;
                    const std::uint64_t sc_cursor_starts = C.scan_cursor_starts;
                    const std::uint64_t sc_ov_cursor_starts = C.scan_ov_cursor_starts;
                    const std::uint64_t sc_canonical = C.scan_canonical_group;
#else
                    const std::uint64_t reclaims = 0, widened = 0;
                    const std::uint64_t sc_regions = 0, sc_groups = 0, sc_groups_empty = 0;
                    const std::uint64_t sc_blocks_skipped = 0, sc_merge_cmps = 0;
                    const std::uint64_t sc_result_allocs = 0, sc_start_locates = 0;
                    const std::uint64_t sc_cursor_starts = 0, sc_ov_cursor_starts = 0;
                    const std::uint64_t sc_canonical = 0;
#endif

                    out << "{\"kind\":\"" << kind << "\",\"run_tag\":\"" << run_tag
                        << "\",\"mode\":\"" << mode << "\",\"backend\":\"" << backend_name
                        << "\",\"arm\":\"" << ARMS[arm].name << "\",\"wide\":"
                        << (ARMS[arm].wide ? "true" : "false") << ",\"routing\":"
                        << (ARMS[arm].routing ? "true" : "false") << ",\"ordered\":"
                        << (ARMS[arm].ordered ? "true" : "false") << ",\"rep\":" << r
                        << ",\"position\":" << oi << ",\"n\":" << N << ",\"thr\":" << thr
                        << ",\"frac\":" << cp.frac << ",\"cycles\":" << cp.seg.size()
                        << ",\"live\":" << cp.live << ",\"build_ns\":" << build_ns
                        << ",\"seg_ns\":" << seg_ns << ",\"cum_ns\":" << cum_ns
                        << ",\"hit_ns\":" << hit.ns << ",\"hit_probes\":" << hit.probes
                        << ",\"hit_expected\":" << hit.expected_hits
                        << ",\"hit_observed\":" << hit.observed_hits
                        << ",\"hit_mismatch\":" << hit.mismatches
                        << ",\"hit_checksum\":" << hit.checksum
                        << ",\"hit_hash\":" << cp.hit_hash
                        << ",\"miss_ns\":" << miss.ns << ",\"miss_probes\":" << miss.probes
                        << ",\"miss_observed\":" << miss.observed_hits
                        << ",\"miss_mismatch\":" << miss.mismatches
                        << ",\"miss_checksum\":" << miss.checksum
                        << ",\"miss_hash\":" << cp.miss_hash
                        << ",\"scan16_ns\":" << s16.ns << ",\"scan16_rows\":" << s16.rows
                        << ",\"scan16_mismatch\":" << s16.mismatches
                        << ",\"scan256_ns\":" << s256.ns << ",\"scan256_rows\":" << s256.rows
                        << ",\"scan256_mismatch\":" << s256.mismatches
                        << ",\"scan4096_ns\":" << s4096.ns << ",\"scan4096_rows\":"
                        << s4096.rows << ",\"scan4096_mismatch\":" << s4096.mismatches
                        << ",\"overflow\":" << s->overflow_count()
                        << ",\"compactions\":" << s->compaction_count()
                        << ",\"subblock_reclaims\":" << reclaims
                        << ",\"reclaim_widened\":" << widened
                        << ",\"scan_regions\":" << sc_regions
                        << ",\"scan_groups\":" << sc_groups
                        << ",\"scan_groups_empty\":" << sc_groups_empty
                        << ",\"scan_blocks_skipped\":" << sc_blocks_skipped
                        << ",\"scan_merge_cmps\":" << sc_merge_cmps
                        << ",\"scan_result_allocs\":" << sc_result_allocs
                        << ",\"scan_start_locates\":" << sc_start_locates
                        << ",\"scan_cursor_starts\":" << sc_cursor_starts
                        << ",\"scan_ov_cursor_starts\":" << sc_ov_cursor_starts
                        << ",\"scan_canonical\":" << sc_canonical
                        << ",\"blocks\":" << mb.blocks
                        << ",\"model\":" << mb.model << ",\"prefix\":" << mb.prefix
                        << ",\"overflow_bytes\":" << mb.overflow << ",\"tables\":" << mb.tables
                        << ",\"directory\":" << mb.directory
                        << ",\"regions_self\":" << mb.regions_self << ",\"fences\":" << mb.fences
                        << ",\"self\":" << mb.self << ",\"index_bytes\":" << index_bytes
                        << ",\"routing_bytes\":" << s->routing_bytes()
                        << ",\"routing_regions\":" << s->routing_regions()
                        << ",\"regions\":" << s->region_count() << "}" << std::endl;
                }

                std::cerr << kind << " " << backend_name << " " << mode << " rep" << r << " "
                          << ARMS[arm].name << " done" << std::endl;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
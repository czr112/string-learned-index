// M4 entry diagnosis (2026-10-11): prove the deployable work reduction of the ordered-segment
// merge BEFORE any prototype exists. Counts only, no timing.
//
// FROZEN APPARATUS (docs/NEXT_ACTION_PLAN_20261011_MERGE.md sections 3-4, and the routing plan
// section 6 for this round's backend/identity precedence): Context16, mode grow, tranco,
// N=262,144, checkpoints 100% and 200% of the replacement schedule, scan(start, limit=256) over
// the original 17 starts (empty + 16 equidistant live keys). C/W/S are ON and equal on both
// sides of the eventual A/B; M3/H/Q/stable-overflow and every other candidate stay OFF.
//
// WHAT IS COUNTED, per real scan call (counter deltas, never cumulative):
//   C_old        the shipped path's cross-side compares (scan_merge_cmps) -- what the merge pays
//   C_candidate  the shadow's per-row compares + ALL boundary proofs (success and failure)
//   seg_rows     rows of the actual output that belong to a proven segment (r>=4, opposite side
//                present, strict end-key < overflow-head proof, actually emitted)
// and the plan's two entry conditions are evaluated per checkpoint on the 17-start totals:
//   seg_rows / rows >= 0.50      and      1 - C_candidate / C_old >= 0.30,  C_old > 0.
//
// The shadow is Region::diag_scan_merge_shadow (diagnostic-only; see index.hpp). This device
// checks, per call: the real scan's rows equal the ordered reference; the shadow's rows equal
// the real rows; shadow.merge_cmps + shadow.seg_rows equals the MEASURED C_old; probes split
// into ok/fail; every successful segment is >= 4 rows; and W actually fired during the schedule.
// Any violation exits non-zero: the numbers are not reported from a drifting device.
//
// Usage: diag_merge_entry KIND N [TRANCOCSV] [OUTSTEM]

#ifndef E25_DIAG
#error "diag_merge_entry needs -DE25_DIAG (the shadow and the scan counters are diagnostic-only)"
#endif

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
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;
using Reference = std::map<std::string, std::uint64_t, Less>;

namespace {

struct ScanDelta {
    std::uint64_t merge_cmps = 0;
    std::uint64_t out_primary = 0, out_overflow = 0;
    std::uint64_t regions = 0, groups = 0, cursor_starts = 0;
};

ScanDelta snapshot_counts() {
    const auto& c = ::validation::e25::counts();
    ScanDelta d;
    d.merge_cmps = c.scan_merge_cmps;
    d.out_primary = c.scan_out_primary;
    d.out_overflow = c.scan_out_overflow;
    d.regions = c.scan_regions;
    d.groups = c.scan_groups;
    d.cursor_starts = c.scan_cursor_starts;
    return d;
}

ScanDelta delta(const ScanDelta& a, const ScanDelta& b) {
    ScanDelta d;
    d.merge_cmps = b.merge_cmps - a.merge_cmps;
    d.out_primary = b.out_primary - a.out_primary;
    d.out_overflow = b.out_overflow - a.out_overflow;
    d.regions = b.regions - a.regions;
    d.groups = b.groups - a.groups;
    d.cursor_starts = b.cursor_starts - a.cursor_starts;
    return d;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_merge_entry KIND N [TRANCOCSV] [OUTSTEM]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string out_stem = argc > 4 ? argv[4] : ("merge_entry_" + kind);

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        std::uint32_t thr = 32;
        {
            const std::string frozen_rel =
                "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing: " + frozen_rel);
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        // The entry device's ONE configuration: pinned frozen release + C/W/S. The assertion
        // with the pinned baseline keeps this from silently growing a fourth difference.
        Config cfg = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg);
        const Config pinned = cfg;
        cfg.subblock_reclaim = true;
        cfg.reclaim_widest = true;
        cfg.compact_region_routing = true;
        cfg.scan_ordered_start = true;
        e25sw::require_only_switches_differ(
            pinned, cfg,
            {"subblock_reclaim", "reclaim_widest", "compact_region_routing", "scan_ordered_start"});
        if (cfg.model_backend != ModelBackend::Context16)
            throw std::runtime_error("the entry device is Context16 per the frozen plan");

        // One build plan, made once, reused for the run: an input, not part of any arm.
        std::vector<std::vector<std::uint16_t>> sg, caps;
        std::vector<std::vector<std::uint32_t>> bb;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            for (std::size_t i = 0; i < probe.region_count(); ++i) {
                const auto& gs = probe.region(i).group_stats();
                const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
                const Alloc d = demand_single(gs, budget);
                StrictLog log;
                std::vector<std::uint16_t> c;
                const Alloc strict = demand_strict(gs, d, log, c);
                sg.push_back(strict.sg);
                bb.push_back(strict.bb);
                caps.push_back(std::move(c));
            }
        }

        {
            std::ifstream existing("results/main/" + out_stem + ".jsonl");
            if (existing.good())
                throw std::runtime_error("refusing to overwrite existing output: results/main/" +
                                         out_stem + ".jsonl");
        }
        std::ofstream out(("results/main/" + out_stem + ".jsonl").c_str(), std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        // Shared schedule: identical to bench_combo's grow mode (data only; the index is never
        // consulted while the moves are derived).
        const double FRACS[5] = {0.00, 0.25, 0.50, 1.00, 2.00};
        const std::size_t kLimit = 256;

        std::deque<std::string> live;
        std::unordered_set<std::string> live_set;
        Reference ref;
        for (const auto& kv : keys) {
            live.push_back(kv.first);
            live_set.insert(kv.first);
            ref[kv.first] = kv.second;
        }

        auto s = std::make_unique<Index>(cfg);
        s->bulk_load_with_plan(keys, sg, bb, caps);

        std::uint64_t serial = 0;
        std::size_t done = 0;
        bool failed = false;
        std::uint64_t prev_reclaims = ::validation::e25::counts().subblock_reclaims;
        std::uint64_t prev_widened = ::validation::e25::counts().reclaim_widened;

        for (double frac : FRACS) {
            struct Move {
                std::string victim, fresh;
                std::uint64_t value;
            };
            std::vector<Move> seg;
            const std::size_t target = static_cast<std::size_t>(double(N) * frac);
            for (std::size_t i = done; i < target; ++i) {
                const std::string victim = live.front();
                live.pop_front();
                live_set.erase(victim);

                std::string base = victim;
                const auto pos = base.find("~r");
                if (pos != std::string::npos) base.resize(pos);
                std::string fresh = base + "~r" + std::to_string(serial);
                while (!live_set.insert(fresh).second) fresh += "x";

                const std::uint64_t val = 0x7000ULL + serial;
                ++serial;
                seg.push_back({victim, fresh, val});
                live.push_back(fresh);
            }
            for (const auto& m : seg) {
                if (ref.erase(m.victim) != 1) throw std::runtime_error("erase target absent");
                if (ref.find(m.fresh) != ref.end()) throw std::runtime_error("insert target present");
                ref[m.fresh] = m.value;
            }
            // Interleaved, one replacement at a time -- bench_combo's documented shape:
            // batching the erases first leaves the primary blocks full of holes and makes
            // the widening branch unreachable, which is a DIFFERENT structure from the one
            // this entry is defined on. (The first draft of this device batched; it was
            // caught by comparing C_old against the recorded combo_se run, 2116 at 100%.)
            for (const auto& m : seg) {
                s->erase(m.victim);
                s->insert(m.fresh, m.value);
            }
            done = target;

            const std::uint64_t recl = ::validation::e25::counts().subblock_reclaims - prev_reclaims;
            const std::uint64_t wide = ::validation::e25::counts().reclaim_widened - prev_widened;
            prev_reclaims = ::validation::e25::counts().subblock_reclaims;
            prev_widened = ::validation::e25::counts().reclaim_widened;
            // The plan requires W's branches to have really fired on this workload; the
            // cumulative counts are asserted at the target checkpoints below.

            if (frac < 1.0 || frac > 2.0) continue;   // only the two target checkpoints are judged

            if (s->size() != ref.size())
                throw std::runtime_error("checkpoint live population diverged from the reference");
            if (::validation::e25::counts().subblock_reclaims +
                    ::validation::e25::counts().reclaim_widened ==
                0)
                throw std::runtime_error(
                    "W's reclaim branches never fired on this workload; the entry state would not "
                    "be the one the plan froze");

            std::vector<Pair> live_pairs;
            live_pairs.reserve(live.size());
            for (const auto& k : live) live_pairs.emplace_back(k, ref.at(k));
            std::sort(live_pairs.begin(), live_pairs.end(),
                      [](const Pair& x, const Pair& y) { return compare(x.first, y.first) < 0; });

            std::vector<std::string> starts;
            starts.push_back(std::string());
            for (int i = 0; i < 16 && !live_pairs.empty(); ++i)
                starts.push_back(live_pairs[(std::size_t(i) * live_pairs.size()) / 16].first);

            std::uint64_t rows_t = 0, seg_t = 0, probes_t = 0, ok_t = 0, fail_t = 0;
            std::uint64_t fail_eq_t = 0;
            std::uint64_t old_t = 0, shadow_t = 0;
            std::uint64_t bytes_t = 0, fail_bytes_t = 0, switches_t = 0;
            std::vector<std::uint64_t> lens_all;

            for (std::size_t j = 0; j < starts.size(); ++j) {
                const std::string& start = starts[j];

                ::validation::e25::counts().reset();
                const ScanDelta b0 = snapshot_counts();
                const auto real = s->scan(start, kLimit);
                const ScanDelta d = delta(b0, snapshot_counts());

                std::vector<Pair> sh;
                const MergeShadowCounts sc = s->diag_scan_merge_shadow(start, kLimit, sh);

                const auto it = std::lower_bound(
                    live_pairs.begin(), live_pairs.end(), start,
                    [](const Pair& x, const std::string& y) { return compare(x.first, y) < 0; });
                const std::size_t expect =
                    std::min<std::size_t>(kLimit, std::size_t(live_pairs.end() - it));

                std::uint64_t checks = 0, failures = 0;
                auto require = [&](bool ok) {
                    ++checks;
                    if (!ok) { ++failures; failed = true; }
                };

                require(real.size() == expect);
                require(sh.size() == real.size());
                require(d.out_primary + d.out_overflow == real.size());
                {
                    std::size_t i = 0;
                    bool rows_ok = true;
                    for (auto t = it; t != live_pairs.end() && i < real.size(); ++t, ++i)
                        if (real[i].first != t->first || real[i].second != t->second) rows_ok = false;
                    require(rows_ok);
                    bool shadow_ok = true, shadow_ref_ok = true;
                    i = 0;
                    for (auto t = it; t != live_pairs.end() && i < sh.size(); ++t, ++i)
                        if (sh[i].first != t->first || sh[i].second != t->second) shadow_ref_ok = false;
                    for (std::size_t k = 0; k < real.size() && k < sh.size(); ++k)
                        if (sh[k] != real[k]) shadow_ok = false;
                    require(shadow_ok);
                    require(shadow_ref_ok);
                }
                // The avoid-identity: what the shadow's segments saved must be exactly the
                // difference between the measured shipped merge compares and the shadow's own.
                require(sc.merge_cmps + sc.seg_rows == d.merge_cmps);
                require(sc.probes == sc.probe_ok + sc.probe_fail);
                require(sc.seg_rows >= 4ull * sc.probe_ok);
                require(sc.outputs == real.size());
                require(d.cursor_starts > 0 || expect == 0);

                rows_t += real.size();
                seg_t += sc.seg_rows;
                probes_t += sc.probes;
                ok_t += sc.probe_ok;
                fail_t += sc.probe_fail;
                fail_eq_t += sc.probe_fail_equal;
                old_t += d.merge_cmps;
                shadow_t += sc.merge_cmps;
                bytes_t += sc.probe_bytes;
                fail_bytes_t += sc.probe_fail_bytes;
                switches_t += sc.side_switches;
                for (std::uint32_t L : sc.seg_lens) lens_all.push_back(L);

                out << "{\"device\":\"diag_merge_entry\",\"kind\":\"" << kind << "\",\"n\":" << N
                    << ",\"thr\":" << thr << ",\"frac\":" << frac << ",\"phase\":\"call\""
                    << ",\"start_idx\":" << j << ",\"start_len\":" << start.size()
                    << ",\"limit\":" << kLimit << ",\"expected_rows\":" << expect
                    << ",\"rows\":" << real.size() << ",\"old_merge_cmps\":" << d.merge_cmps
                    << ",\"shadow_merge_cmps\":" << sc.merge_cmps
                    << ",\"probes\":" << sc.probes << ",\"probe_ok\":" << sc.probe_ok
                    << ",\"probe_fail\":" << sc.probe_fail
                    << ",\"probe_fail_equal\":" << sc.probe_fail_equal
                    << ",\"probe_bytes\":" << sc.probe_bytes
                    << ",\"probe_fail_bytes\":" << sc.probe_fail_bytes
                    << ",\"seg_rows\":" << sc.seg_rows << ",\"seg_rows_max\":" << sc.seg_rows_max
                    << ",\"skip_short\":" << sc.skip_short << ",\"skip_no_ov\":" << sc.skip_no_ov
                    << ",\"side_switches\":" << sc.side_switches << ",\"checks\":" << checks
                    << ",\"failures\":" << failures << "}" << std::endl;
            }

            const std::uint64_t cand_t = shadow_t + probes_t;
            const double rows_ratio = rows_t ? double(seg_t) / double(rows_t) : 0.0;
            const double cmp_ratio = old_t ? 1.0 - double(cand_t) / double(old_t) : 0.0;
            const bool pass_rows = rows_t > 0 && rows_ratio >= 0.50;
            const bool pass_cmp = old_t > 0 && cmp_ratio >= 0.30;
            const bool pass = pass_rows && pass_cmp;

            std::sort(lens_all.begin(), lens_all.end());
            const auto pct = [&lens_all](double q) -> std::uint64_t {
                if (lens_all.empty()) return 0;
                return lens_all[std::size_t(q * double(lens_all.size() - 1))];
            };
            const std::uint64_t lens_p50 = pct(0.50), lens_p99 = pct(0.99);
            const std::uint64_t lens_max = lens_all.empty() ? 0 : lens_all.back();

            const auto m = s->memory_breakdown();
            out << "{\"device\":\"diag_merge_entry\",\"kind\":\"" << kind << "\",\"n\":" << N
                << ",\"thr\":" << thr << ",\"frac\":" << frac << ",\"phase\":\"checkpoint\""
                << ",\"live\":" << ref.size() << ",\"regions\":" << s->region_count()
                << ",\"starts\":" << starts.size() << ",\"limit\":" << kLimit
                << ",\"rows\":" << rows_t << ",\"seg_rows\":" << seg_t
                << ",\"old_merge_cmps\":" << old_t << ",\"shadow_merge_cmps\":" << shadow_t
                << ",\"cand_cmps\":" << cand_t << ",\"probes\":" << probes_t
                << ",\"probe_ok\":" << ok_t << ",\"probe_fail\":" << fail_t
                << ",\"probe_fail_equal\":" << fail_eq_t
                << ",\"probe_bytes\":" << bytes_t << ",\"probe_fail_bytes\":" << fail_bytes_t
                << ",\"side_switches\":" << switches_t << ",\"seg_len_p50\":" << lens_p50
                << ",\"seg_len_p99\":" << lens_p99 << ",\"seg_len_max\":" << lens_max
                << ",\"rows_ratio\":" << rows_ratio << ",\"cmp_ratio\":" << cmp_ratio
                << ",\"pass_rows\":" << (pass_rows ? 1 : 0)
                << ",\"pass_cmp\":" << (pass_cmp ? 1 : 0) << ",\"pass\":" << (pass ? 1 : 0)
                << ",\"w_subblock_reclaims\":" << recl << ",\"w_reclaim_widened\":" << wide
                << ",\"index_bytes\":"
                << (m.blocks + m.model + m.prefix + m.overflow + m.tables + m.directory +
                    m.regions_self + m.fences + m.self)
                << ",\"model_bytes\":" << m.model << "}" << std::endl;

            std::cout << "checkpoint frac=" << frac << ": rows=" << rows_t << " seg_rows=" << seg_t
                      << " (" << (100.0 * rows_ratio) << "%)"
                      << " C_old=" << old_t << " C_cand=" << cand_t
                      << " (reduction " << (100.0 * cmp_ratio) << "%)"
                      << " probes ok/fail(eq)=" << ok_t << "/" << fail_t << "(" << fail_eq_t << ")"
                      << " -> " << (pass ? "ENTRY PASS" : "ENTRY FAIL") << std::endl;
        }

        if (failed) {
            std::cerr << "FAILED: device assertions did not hold" << std::endl;
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
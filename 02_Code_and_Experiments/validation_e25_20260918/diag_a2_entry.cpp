// A2 entry diagnosis: of the insert events that STILL go to overflow after A1' has done
// everything it can, how many are cases where the group has room but the boundary between
// two adjacent sub-blocks is in the wrong place?
//
// WHAT THIS ROUND MUST GET RIGHT, per the design review (A2_ENTRY_DESIGN.md):
//
//  1. THE POPULATION. "An insert event that finally goes to overflow" has no single
//     referent in this code: put_overflow has five call sites and only two of them are
//     incremental insert terminals. So the audit is NOT hooked into put_overflow. The
//     driver inserts, reads the result back, and audits only when the record came out of
//     the overflow container -- one row per event, construction placements and drain
//     re-stores excluded BY CONSTRUCTION, and their counts reported rather than assumed 0.
//
//  2. TWO WORKLOADS, NEVER ONE NUMBER. Long replacement is exactly the load on which A1'
//     already removed the problem (residual 748/950/1,924), so a fraction measured there
//     describes a hardened tail, not the structure. The plan already asks for a pure-insert
//     control and P1 never ran it. X is reported PER (workload, corpus).
//
//  3. THE COST UNIT. There is no incremental directory update, so any M1-preserving
//     boundary move forces one full rebuild_dir on the touched group -- measured as that
//     group's NON-EMPTY sub-block count, not as "entries changed".
//
// READ-ONLY. It calls no mutator and adds no Config switch. COUNTS LOCATE CANDIDATES; THEY
// DO NOT CONVERT TO TIME, and this program takes no timings.
//
// Output: results/main/a2_entry_<kind>.jsonl, one record per overflow event, plus a summary.
//
// Usage:  diag_a2_entry KIND N [TRANCOCSV]

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
#error "diag_a2_entry.cpp needs -DE25_DIAG: the audit hook is diagnostic-only"
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

struct Tally {
    std::uint64_t events = 0;
    std::uint64_t slots_only = 0, bytes_only = 0, both = 0, neither = 0;
    std::uint64_t group_ok = 0;        // group totals sufficed
    std::uint64_t f4_fits = 0;         // an adjacent boundary move would have made it fit
    std::uint64_t moved_total = 0, bytes_total = 0, dir_total = 0;
    std::uint64_t moved_max = 0, bytes_max = 0, dir_max = 0;
};

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_a2_entry KIND N [TRANCOCSV]");
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
            std::stringstream ss; ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        Config cfg = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg);

        // A1' must be ON: the population is "events that survive reclaim", which is only
        // defined relative to these switches. Recorded, not assumed.
        cfg.subblock_reclaim = true;
        cfg.reclaim_widest = true;

        if (cfg.overflow_migrate != false)
            throw std::runtime_error(
                "the drain re-store bucket is only provably empty with overflow_migrate off");

        Pipeline plan;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        const Mode qm{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        std::ofstream out(("results/main/a2_entry_" + kind + ".jsonl").c_str(), std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        const auto emit = [&](const char* workload, const char* state, const std::string& key,
                              const Region::A2Audit& a, Tally& t) {
            if (!a.valid) return;

            ++t.events;

            if (a.refuse_slots && a.refuse_bytes) ++t.both;
            else if (a.refuse_slots) ++t.slots_only;
            else if (a.refuse_bytes) ++t.bytes_only;
            else ++t.neither;

            if (a.group_slots_ok && a.group_bytes_ok) ++t.group_ok;

            if (a.adjacent_move_fits) {
                ++t.f4_fits;
                t.moved_total += a.moved_records;
                t.bytes_total += a.copied_bytes;
                t.dir_total += a.dir_entries_rebuilt;
                t.moved_max = std::max(t.moved_max, a.moved_records);
                t.bytes_max = std::max(t.bytes_max, a.copied_bytes);
                t.dir_max = std::max(t.dir_max, a.dir_entries_rebuilt);
            }

            out << "{\"kind\":\"" << kind << "\",\"workload\":\"" << workload
                << "\",\"state\":\"" << state << "\""
                << ",\"key\":\"" << key << "\""
                << ",\"group\":" << a.group
                << ",\"subblocks\":" << a.subblocks
                << ",\"blk\":" << a.blk
                << ",\"refuse_slots\":" << (a.refuse_slots ? "true" : "false")
                << ",\"refuse_bytes\":" << (a.refuse_bytes ? "true" : "false")
                << ",\"group_slots_ok\":" << (a.group_slots_ok ? "true" : "false")
                << ",\"group_bytes_ok\":" << (a.group_bytes_ok ? "true" : "false")
                << ",\"adjacent_move_fits\":" << (a.adjacent_move_fits ? "true" : "false")
                << ",\"moved_records\":" << a.moved_records
                << ",\"copied_bytes\":" << a.copied_bytes
                << ",\"dir_entries_rebuilt\":" << a.dir_entries_rebuilt
                << "}" << std::endl;
        };

        std::size_t serial = 0;
        std::unordered_set<std::string> live_set;

        for (const auto& kv : keys) live_set.insert(kv.first);

        const auto fresh_key = [&](const std::string& base) {
            std::string f = base + "~m" + std::to_string(serial++);
            while (!live_set.insert(f).second) f += "x";
            return f;
        };

        const char* STATES[4] = {"pct25", "pct50", "pct100", "pct200"};

        // ---------------- W1: long replacement to 200% ----------------
        Tally w1;
        std::uint64_t build_overflow = 0;

        {
            auto idx = std::make_unique<Index>(cfg);
            idx->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

            // The EXCLUDED build bucket, reported rather than assumed zero.
            build_overflow = idx->overflow_count();

            std::deque<std::string> live;
            for (const auto& kv : keys) live.push_back(kv.first);

            const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
            std::size_t done = 0;

            for (int si = 0; si < 4; ++si) {
                const std::size_t target = static_cast<std::size_t>(double(N) * CPS[si]);

                for (std::size_t i = done; i < target; ++i) {
                    const std::string victim = live.front();
                    live.pop_front();
                    live_set.erase(victim);

                    std::string base = victim;
                    const auto pos = base.find("~m");
                    if (pos != std::string::npos) base.resize(pos);

                    const std::string fresh = fresh_key(base);
                    const std::uint64_t val = 0xD00DULL + i;

                    idx->erase(victim);
                    idx->insert(fresh, val);
                    live.push_back(fresh);

                    const Result r = idx->find(fresh, qm);
                    if (r.source == 2) emit("replace", STATES[si], fresh, idx->diag_a2(fresh), w1);
                }

                done = target;
            }
        }

        // ---------------- W2: pure insert (no erase) ----------------
        // The control the plan asks for and P1 never ran. There is no garbage here, so
        // reclaim_widen fails for a structurally different reason -- which is exactly why
        // one workload cannot answer the entry question.
        Tally w2;
        std::uint64_t build_overflow_w2 = 0;

        {
            serial = 0;
            live_set.clear();
            for (const auto& kv : keys) live_set.insert(kv.first);

            auto idx = std::make_unique<Index>(cfg);
            idx->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            build_overflow_w2 = idx->overflow_count();

            for (std::size_t i = 0; i < N; ++i) {
                std::string base = keys[i].first;
                const auto pos = base.find("~m");
                if (pos != std::string::npos) base.resize(pos);

                const std::string fresh = fresh_key(base);
                idx->insert(fresh, 0xBEEFULL + i);

                const Result r = idx->find(fresh, qm);
                if (r.source == 2) emit("insert", "single", fresh, idx->diag_a2(fresh), w2);
            }
        }

        // ---------------- summary ----------------
        const auto report = [&](const char* name, const Tally& t) {
            std::cout << "{\"kind\":\"" << kind << "\",\"workload\":\"" << name << "\","
                      << "\"events\":" << t.events
                      << ",\"slots_only\":" << t.slots_only
                      << ",\"bytes_only\":" << t.bytes_only
                      << ",\"both\":" << t.both
                      << ",\"neither\":" << t.neither
                      << ",\"group_totals_ok\":" << t.group_ok
                      << ",\"f4_fits\":" << t.f4_fits
                      << ",\"moved_total\":" << t.moved_total
                      << ",\"moved_max\":" << t.moved_max
                      << ",\"copied_bytes_total\":" << t.bytes_total
                      << ",\"copied_bytes_max\":" << t.bytes_max
                      << ",\"dir_entries_total\":" << t.dir_total
                      << ",\"dir_entries_max\":" << t.dir_max
                      << "}" << std::endl;

            if (t.events) {
                std::cout << "      F4 share = " << (100.0 * double(t.f4_fits) / double(t.events))
                          << "%   (F4 " << t.f4_fits << " / events " << t.events << ")" << std::endl;
                std::cout << "      mean over F4-true: moved " << (t.f4_fits ? double(t.moved_total) / double(t.f4_fits) : 0.0)
                          << " records, " << (t.f4_fits ? double(t.bytes_total) / double(t.f4_fits) : 0.0)
                          << " bytes, " << (t.f4_fits ? double(t.dir_total) / double(t.f4_fits) : 0.0)
                          << " directory entries" << std::endl;
            } else {
                std::cout << "      no overflow events: shares NOT COMPUTED" << std::endl;
            }
        };

        std::cout << "{\"kind\":\"" << kind << "\",\"case\":\"excluded_buckets\","
                  << "\"build_placements_w1\":" << build_overflow
                  << ",\"build_placements_w2\":" << build_overflow_w2
                  << ",\"drain_restores\":0"
                  << ",\"note\":\"excluded by construction, not because they are few\"}"
                  << std::endl;

        report("replace", w1);
        report("insert", w2);

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

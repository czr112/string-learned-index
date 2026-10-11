// I10 (2026-09-23 review): does the fixed commit phase actually stop being interruptible?
//
// THE REVIEW'S QUESTION was whether compact()'s commit phase can be interrupted by a REAL
// throwing operation -- an allocation, a copy, the directory construction. G1 measured the
// consequence (a mid-commit failure loses records and desynchronises `size`) but not the
// cause. I10_COMMIT_PHASE_AUDIT.md located it: compact() rewrote the whole group and THEN
// called rebuild_dir(), which does dir.clear() and then push_back's freshly built
// std::string boundaries. A bad_alloc there leaves the records re-placed and the boundary
// directory PARTIAL -- and locate_subblock binary searches that directory, so the damage is
// a silently mis-routed lookup, not a visible lost record.
//
// A GATE THAT ONLY RE-RUNS THE OLD INJECTION WOULD CERTIFY NOTHING, because the old
// injection throws from inside the replay loop, where no real operation can throw. So this
// gate injects at the site that actually throws -- the directory construction -- and runs it
// under BOTH designs:
//
//   PASS 1  plan OFF, injection at the directory rebuild (point 2, post-replay)
//           -> the index MUST come back changed. This is the defect, asserted, so the
//              gate cannot pass by the injection quietly failing to fire.
//   PASS 2  plan ON,  injection at the directory construction (point 3, in prepare)
//           -> the index MUST come back byte-identical, and the scan MUST still match the
//              reference record for record. This is the fix.
//   PASS 3  plan ON, no injection -> the plan must not CHANGE the outcome: full state and
//           full content must equal the plan-OFF run's.
//
// PASS 2 is the one that would be vacuous if the two injection points were not genuinely
// different code paths at genuinely different times: point 3 is reached while the group is
// still untouched, point 2 only after it has been rewritten. The gate checks the timing
// difference rather than assuming it -- see the `records_changed` columns.
//
// PASS 1 is reported as a MEASUREMENT (the pre-fix behaviour is the finding, and a fix does
// not retroactively make the old arm atomic); PASS 2 is ASSERTED.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

#ifndef E25_DIAG
#error "check_i10_commit.cpp needs -DE25_DIAG: the injection hooks are diagnostic-only"
#endif

struct Snapshot {
    std::size_t size = 0;
    std::size_t region_count = 0;
    std::size_t memory = 0;
    std::size_t overflow = 0;
    std::size_t compactions = 0;
    Index::MemBreakdown mb{};
    std::vector<std::size_t> prefixes;
    std::vector<std::vector<std::size_t>> offsets;
    std::vector<std::vector<std::uint16_t>> slots;
    std::vector<std::vector<std::uint32_t>> blocks;
    std::vector<std::vector<std::size_t>> group_fingerprints;
};

static Snapshot snapshot(const Index& idx) {
    Snapshot s;
    s.size = idx.size();
    s.region_count = idx.region_count();
    s.memory = idx.memory();
    s.overflow = idx.overflow_count();
    s.compactions = idx.compaction_count();
    s.mb = idx.memory_breakdown();

    for (std::size_t r = 0; r < idx.region_count(); ++r) {
        const Region& reg = idx.region(r);
        s.prefixes.push_back(reg.prefix_length());
        s.offsets.push_back(reg.offset_table());
        s.slots.push_back(reg.slot_table());
        s.blocks.push_back(reg.block_table());

        std::vector<std::size_t> per_group;
        for (const auto& g : reg.group_stats())
            per_group.push_back(g.primary_entries + g.overflow_entries +
                                g.primary_bytes + g.overflow_bytes +
                                g.free_bytes + g.garbage_bytes);
        s.group_fingerprints.push_back(std::move(per_group));
    }

    return s;
}

static bool same(const Snapshot& a, const Snapshot& b) {
    return a.size == b.size && a.region_count == b.region_count &&
           a.memory == b.memory && a.overflow == b.overflow && a.compactions == b.compactions &&
           a.mb.blocks == b.mb.blocks && a.mb.model == b.mb.model &&
           a.mb.prefix == b.mb.prefix && a.mb.overflow == b.mb.overflow &&
           a.mb.tables == b.mb.tables && a.mb.directory == b.mb.directory &&
           a.mb.self == b.mb.self &&
           a.prefixes == b.prefixes && a.offsets == b.offsets && a.slots == b.slots &&
           a.blocks == b.blocks && a.group_fingerprints == b.group_fingerprints;
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
        if (argc < 3) throw std::invalid_argument("check_i10_commit KIND N [TRANCOCSV]");
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

        Pipeline plan;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        auto mk = [&](bool plan_commit) {
            Config c = cfg;
            c.compact_plan_commit = plan_commit;
            auto s = std::make_unique<Index>(c);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            return s;
        };

        // A replacement sequence: a bare insert run never reaches compact() on this design
        // (measured: 131,072 appends produced zero compactions). Same load as G1 used.
        struct Op { int kind; std::string key; std::uint64_t value; };
        std::vector<Op> ops;
        for (std::size_t j = 0; j < N; ++j) {
            ops.push_back({0, keys[j].first, 0});
            ops.push_back({1, keys[j].first + "~m" + std::to_string(j), 0xD00DULL + j});
        }

        auto apply = [](Index& idx, const Op& o) {
            if (o.kind == 0) return idx.erase(o.key);
            return idx.insert(o.key, o.value);
        };

        // The operation that triggers the FIRST compaction, found with injection off.
        std::size_t k = ops.size();
        {
            auto idx = mk(false);
            Region::inject_at = -1;
            for (std::size_t j = 0; j < ops.size(); ++j) {
                apply(*idx, ops[j]);
                if (idx->compaction_count() > 0) { k = j; break; }
            }
        }

        if (k >= ops.size())
            throw std::runtime_error("no operation triggered a compaction");

        Reference ref;
        for (const auto& kv : keys) ref[kv.first] = kv.second;

        auto run_to_k = [&](bool plan_commit, Reference& r) {
            auto idx = mk(plan_commit);
            Region::inject_at = -1;
            for (std::size_t j = 0; j < k; ++j) {
                apply(*idx, ops[j]);
                if (ops[j].kind == 0) {
                    if (r.erase(ops[j].key) != 1) throw std::runtime_error("erase of an absent key");
                } else {
                    r[ops[j].key] = ops[j].value;
                }
            }
            return idx;
        };

        std::uint64_t comparisons = 0;
        std::size_t p1_changed = 0, p1_lost = 0, p1_scan_mismatch = 0;
        std::size_t p5_changed = 0, p5_lost = 0;

        // ---- PASS 1: the DEFECT -- plan off, inject at the post-replay directory rebuild
        //
        // RUN TWICE, because WHERE in the rebuild the failure lands decides the damage.
        // At the first sub-block the directory is left EMPTY, and has_dir() reports false
        // for an empty directory, so lookups fall back and still answer correctly -- the
        // loss is the directory, not correctness. From the second sub-block on the
        // directory is PARTIAL BUT NON-EMPTY, has_dir() accepts it, and locate_subblock
        // binary searches a truncated key list -- THAT is the case that can mis-route.
        // A gate that only tested the first would have certified the milder failure mode.
        for (long long where : {0LL, 1LL}) {
            Reference r;
            for (const auto& kv : keys) r[kv.first] = kv.second;
            auto idx = run_to_k(false, r);

            const Snapshot before = snapshot(*idx);

            Region::inject_at = where;
            Region::inject_point = 2;
            Region::inject_seen = 0;
            Region::injected = 0;

            bool fired = false;
            try {
                apply(*idx, ops[k]);
            } catch (const std::bad_alloc&) {
                fired = true;
            }
            Region::inject_at = -1;

            if (!fired)
                throw std::runtime_error("PASS 1: the directory-rebuild injection never fired");

            const Snapshot after = snapshot(*idx);
            p1_changed = same(before, after) ? 0 : 1;

            if (!p1_changed)
                throw std::runtime_error(
                    "PASS 1: the pre-fix design came back unchanged -- either the injection "
                    "point moved, or the defect is gone and this gate is now vacuous");

            // The damage is reported, not asserted: the pre-fix behaviour IS the finding.
            const auto full = idx->scan(std::string_view{}, idx->size());
            if (full.size() != r.size()) p1_lost = 1;

            for (const auto& p : full) {
                const auto it = r.find(p.first);
                if (it == r.end() || it->second != p.second) { p1_scan_mismatch = 1; break; }
            }

            // A partial boundary directory shows up as a MIS-ROUTED LOOKUP, and a scan does
            // not go through locate_subblock -- so every live key is queried here. Without
            // this sweep the gate could report "changed, but nothing observable", which is
            // the one outcome that would leave the audit's mechanism claim untested.
            std::size_t p1_misrouted = 0;
            const Mode qm{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

            for (const auto& kv : r) {
                const Result q = idx->find(kv.first, qm);
                if (!q.found || q.value != kv.second) ++p1_misrouted;
            }

            std::cout << "{\"case\":\"P1_pre_fix_directory_rebuild\",\"plan\":0,"
                      << "\"inject_point\":2,\"inject_at\":" << where << ","
                      << "\"injection_fired\":true,"
                      << "\"index_changed\":" << (p1_changed ? "true" : "false") << ","
                      << "\"record_count_changed\":" << (p1_lost ? "true" : "false") << ","
                      << "\"scan_differs_from_reference\":"
                      << (p1_scan_mismatch ? "true" : "false") << ","
                      << "\"misrouted_lookups\":" << p1_misrouted << ","
                      << "\"size\":" << idx->size() << ",\"reference_size\":" << r.size()
                      << "}" << std::endl;
        }

        // ---- PASS 2: the FIX -- plan on, inject at the same construction, now in prepare
        {
            Reference r;
            for (const auto& kv : keys) r[kv.first] = kv.second;
            auto idx = run_to_k(true, r);

            const Snapshot before = snapshot(*idx);

            Region::inject_at = 0;
            Region::inject_point = 3;
            Region::inject_seen = 0;
            Region::injected = 0;

            bool fired = false;
            try {
                apply(*idx, ops[k]);
            } catch (const std::bad_alloc&) {
                fired = true;
            }
            Region::inject_at = -1;

            if (!fired)
                throw std::runtime_error(
                    "PASS 2: the prepare-phase directory injection never fired, so the gate "
                    "verified nothing about the fixed commit phase");

            if (!same(before, snapshot(*idx)))
                throw std::runtime_error(
                    "PASS 2: a failure DURING THE PLAN changed the index -- the commit phase "
                    "still has work that happens after the point of no return");

            const auto full = idx->scan(std::string_view{}, idx->size());
            if (full.size() != r.size())
                throw std::runtime_error("PASS 2: the plan failure changed the record count");

            for (const auto& p : full) {
                const auto it = r.find(p.first);
                if (it == r.end() || it->second != p.second)
                    throw std::runtime_error("PASS 2: the plan failure changed the content");
                ++comparisons;
            }

            // Same sweep as PASS 1, so "unchanged" means unchanged on the path that the
            // pre-fix design could damage, not merely on the path the snapshot can see.
            const Mode qm2{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

            for (const auto& kv : r) {
                const Result q = idx->find(kv.first, qm2);
                if (!q.found || q.value != kv.second)
                    throw std::runtime_error("PASS 2: a lookup after the plan failure mis-routed");
                ++comparisons;
            }

            std::cout << "{\"case\":\"P2_fixed_plan_failure\",\"plan\":1,"
                      << "\"inject_point\":3,\"injection_fired\":true,"
                      << "\"index_unchanged\":true,\"records\":" << r.size() << "}"
                      << std::endl;
        }

        // ---- PASS 3: the plan must not CHANGE the outcome -----------------------------
        {
            Reference r1, r2;
            for (const auto& kv : keys) { r1[kv.first] = kv.second; r2[kv.first] = kv.second; }

            auto off = run_to_k(false, r1);

            // The plan's rejection counter is read across the NEW arm's run: a gate that
            // claims "the plan never rejects" must actually measure it, or the claim is
            // indistinguishable from the counter never being wired up.
            validation::e25::counts().reset();

            auto on = run_to_k(true, r2);

            const std::uint64_t plan_rejected = validation::e25::counts().compact_plan_rejected;
            const std::uint64_t plan_violation = validation::e25::counts().compact_plan_violation;

            if (plan_violation != 0)
                throw std::runtime_error(
                    "PASS 3: append_planned's precondition broke -- plan_replay's arithmetic "
                    "has drifted from insert_into's");

            if (!same(snapshot(*off), snapshot(*on)))
                throw std::runtime_error(
                    "PASS 3: enabling the plan changed the resulting state, so it is not a "
                    "behaviour-preserving restructure");

            const auto a = off->scan(std::string_view{}, off->size());
            const auto b = on->scan(std::string_view{}, on->size());

            if (a.size() != b.size() || a.size() != r1.size())
                throw std::runtime_error("PASS 3: the two arms returned different record counts");

            for (std::size_t i = 0; i < a.size(); ++i) {
                if (a[i] != b[i]) throw std::runtime_error("PASS 3: scan outputs differ");
                ++comparisons;
            }

            std::cout << "{\"case\":\"P3_plan_is_behaviour_preserving\",\"records\":" << a.size()
                      << ",\"snapshots_equal\":true,\"scans_equal\":true"
                      << ",\"plan_rejected\":" << plan_rejected
                      << ",\"plan_violation\":" << plan_violation << "}" << std::endl;
        }

        // ---- PASS 4: PREPARE injection, plan on (continuity with G1's D result) ---------
        {
            Reference r;
            for (const auto& kv : keys) r[kv.first] = kv.second;
            auto idx = run_to_k(true, r);

            const Snapshot before = snapshot(*idx);

            Region::inject_at = 0;
            Region::inject_point = 0;
            Region::inject_seen = 0;
            Region::injected = 0;

            bool fired = false;
            try {
                apply(*idx, ops[k]);
            } catch (const std::bad_alloc&) {
                fired = true;
            }
            Region::inject_at = -1;

            if (!fired) throw std::runtime_error("PASS 4: the PREPARE injection never fired");

            if (!same(before, snapshot(*idx)))
                throw std::runtime_error("PASS 4: a PREPARE-phase failure changed the index");

            std::cout << "{\"case\":\"P4_prepare_failure_atomic\",\"plan\":1,"
                      << "\"injection_fired\":true,\"index_unchanged\":true}" << std::endl;
        }

        // ---- PASS 5: mid-replay injection, plan on -- REPORTED, NOT ASSERTED -----------
        // This is an interruption that CANNOT HAPPEN for real under the fix: the replay loop
        // now contains no allocation and no fallible call. It is reported so the difference
        // between "cannot be interrupted" and "would be atomic if it were" stays visible.
        {
            Reference r;
            for (const auto& kv : keys) r[kv.first] = kv.second;
            auto idx = run_to_k(true, r);

            const Snapshot before = snapshot(*idx);

            Region::inject_at = 0;
            Region::inject_point = 1;
            Region::inject_seen = 0;
            Region::injected = 0;

            bool fired = false;
            try {
                apply(*idx, ops[k]);
            } catch (const std::bad_alloc&) {
                fired = true;
            }
            Region::inject_at = -1;

            const Snapshot after = snapshot(*idx);
            p5_changed = same(before, after) ? 0 : 1;

            const auto full = idx->scan(std::string_view{}, idx->size());
            if (full.size() != r.size()) p5_lost = 1;

            std::cout << "{\"case\":\"P5_mid_replay_injection_reported\",\"plan\":1,"
                      << "\"inject_point\":1,\"injection_fired\":"
                      << (fired ? "true" : "false") << ","
                      << "\"index_changed\":" << (p5_changed ? "true" : "false") << ","
                      << "\"record_count_changed\":" << (p5_lost ? "true" : "false") << ","
                      << "\"note\":\"no real operation in the replay loop can throw\""
                      << "}" << std::endl;
        }

        std::cout << kind << " I10 commit gate done, " << comparisons
                  << " element comparisons; config " << e25sw::switches(cfg) << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

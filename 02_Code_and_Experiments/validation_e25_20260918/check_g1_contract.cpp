// G1 contract additions: three original-task requirements that NO gate asserted.
//
// The G0 audit (PRIOR_WORK_AUDIT.md) found these three as "有实现、无验收":
//
//   A. I11  只改值不改变键布局与空间计数
//        The existing gates either compare CONTENT (check_lits_index's update_only) or
//        compare two CONFIGURATIONS (check_freeze's update_only). Neither compares one
//        index against ITSELF before and after a pure value update, which is what the
//        requirement says: the layout and the space counters must not move.
//
//   B. I12  n = 0
//        Every scan gate uses count sets {1,2,16} or {16,128,1024}. Zero is named in the
//        acceptance list and was never exercised; only the code (index.hpp:3812) says it
//        returns empty. This part also pins count > size.
//
//   C. I1   缺席新键：查询路径的组号 == 插入路径的组号
//        check_scan_contract Part 1a asserts this for keys that are IN the index. For an
//        ABSENT key nothing asserted it, and the two paths are not the same call:
//        Region::canonical_group uses model_.predict<false>(..., config_.canonical, ...)
//        while Region::predict_for uses model_.predict<Hash>(..., mode.route, ...).
//        Different function, different template argument, different route. A query mode
//        whose route differs from the placement route would send a lookup to the wrong
//        group with every other gate still green.
//
// This file is DERIVED, not a modification: it adds a target and touches nothing else, so
// every existing build record keeps its identity. Built with -DE25_DIAG because part C
// reads the query-path group through Index::diag_where, which is diagnostic-only.

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
#include <new>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef E25_DIAG
#error "check_g1_contract must be built with -DE25_DIAG: part C asks the query path for a key's group, and that accessor is diagnostic-only. Build it the way tools_build.py's EXTRA table does."
#endif

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

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

// ---------------------------------------------------------------------------
// A whole-index layout snapshot. Deliberately EVERYTHING that a pure value update
// could plausibly disturb: the two space counters, the memory decomposition, and the
// three tables plus the per-group geometry that describe the physical layout.
// ---------------------------------------------------------------------------

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

        // Group geometry, one number per group: entries + bytes are what the placement
        // rules trade against each other, so both are folded in.
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
    if (a.size != b.size || a.region_count != b.region_count) return false;
    if (a.memory != b.memory || a.overflow != b.overflow || a.compactions != b.compactions)
        return false;
    if (a.mb.blocks != b.mb.blocks || a.mb.model != b.mb.model || a.mb.prefix != b.mb.prefix ||
        a.mb.overflow != b.mb.overflow || a.mb.tables != b.mb.tables ||
        a.mb.directory != b.mb.directory || a.mb.self != b.mb.self)
        return false;
    if (a.prefixes != b.prefixes) return false;
    if (a.offsets != b.offsets) return false;
    if (a.slots != b.slots) return false;
    if (a.blocks != b.blocks) return false;
    if (a.group_fingerprints != b.group_fingerprints) return false;
    return true;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_g1_contract KIND N [TRANCOCSV]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();
        if (N < 64) throw std::runtime_error("check_g1_contract needs at least 64 keys");

        // The frozen Hybrid cutoff lives in E7's recorded file, as in check_freeze.
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

        // The shipped configuration, built from the pinning helper so that a Config
        // default flip cannot move this gate's condition underneath it.
        Config cfg = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg);
        std::cerr << "arms shipped[" << e25sw::switches(cfg) << "]" << std::endl;

        Pipeline plan;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        auto mk = [&]() {
            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            return s;
        };

        std::uint64_t comparisons = 0;

        // ---- A: a pure value update must not move the layout ----------------------
        std::size_t a_updated = 0;
        {
            auto idx = mk();
            const Snapshot before = snapshot(*idx);

            std::vector<std::size_t> order(N);
            for (std::size_t i = 0; i < N; ++i) order[i] = i;
            std::mt19937_64 rng(2026092301);
            std::shuffle(order.begin(), order.end(), rng);

            const std::size_t howmany = std::min<std::size_t>(N, 4096);

            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;

            for (std::size_t j = 0; j < howmany; ++j) {
                const std::string& k = keys[order[j]].first;
                const std::uint64_t v = 0x9E3779B97F4A7C15ULL + j;

                if (!idx->update(k, v)) throw std::runtime_error("A: update of a live key failed");
                ref[k] = v;
                ++a_updated;
            }

            const Snapshot after = snapshot(*idx);

            if (!same(before, after))
                throw std::runtime_error("A: a pure value update moved the layout or the space counters");

            // Non-vacuity: the updates must have been real updates of real keys, and the
            // values must actually be the new ones -- an "unchanged snapshot" is only
            // interesting if something changed.
            for (std::size_t j = 0; j < howmany; ++j) {
                const std::string& k = keys[order[j]].first;
                const auto r = idx->find(k, query_mode);
                if (!r.found) throw std::runtime_error("A: an updated key is no longer found");
                if (r.value != ref[k]) throw std::runtime_error("A: the value did not take");
                ++comparisons;
            }
            if (a_updated == 0) throw std::runtime_error("A: no update ran, nothing was tested");

            std::size_t groups_total = 0;
            for (const auto& v : before.group_fingerprints) groups_total += v.size();

            std::cout << "{\"case\":\"A_value_update_layout\",\"keys\":" << N
                      << ",\"updates\":" << a_updated
                      << ",\"regions\":" << before.region_count
                      << ",\"groups_total\":" << groups_total
                      << ",\"layout_unchanged\":true}" << std::endl;
        }

        // ---- B: n = 0 returns empty; n > size returns the whole tail ---------------
        std::size_t b_starts = 0, b_zero = 0, b_over = 0;
        {
            auto idx = mk();
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;

            std::vector<std::string> starts;
            starts.push_back(keys.front().first);
            starts.push_back(keys.back().first);
            starts.push_back(keys[N / 2].first);
            starts.push_back(keys.front().first + "\x01");
            starts.push_back(keys.back().first + "\x01");   // past the last key: empty tail
            starts.push_back(std::string{});                // empty start: the whole index

            std::mt19937_64 rng(2026092302);
            for (int i = 0; i < 8; ++i)
                starts.push_back(keys[rng() % N].first + "~absent");

            for (const std::string& s : starts) {
                ++b_starts;

                const auto zero = idx->scan(s, 0);
                if (!zero.empty())
                    throw std::runtime_error("B: scan(start, 0) returned records");
                ++b_zero;

                const std::size_t big = idx->size() + 1 + std::size_t(rng() % 64);
                const auto all = idx->scan(s, big);

                auto it = ref.lower_bound(s);
                std::vector<Pair> want(it, ref.end());

                if (all.size() != want.size())
                    throw std::runtime_error("B: scan(start, >size) length != reference lower_bound tail");

                for (std::size_t i = 0; i < all.size(); ++i) {
                    if (all[i].first != want[i].first || all[i].second != want[i].second)
                        throw std::runtime_error("B: scan(start, >size) disagrees with the reference");
                    ++comparisons;
                }
                ++b_over;
            }

            if (b_zero == 0 || b_over == 0)
                throw std::runtime_error("B: nothing was exercised");

            std::cout << "{\"case\":\"B_scan_zero_and_over\",\"keys\":" << N
                      << ",\"starts\":" << b_starts
                      << ",\"zero_scans\":" << b_zero
                      << ",\"oversized_scans\":" << b_over << "}" << std::endl;
        }

        // ---- C: an ABSENT key's query group is the group its insertion uses --------
        // TWO query modes, because the placement route and the query route are NOT the
        // same call: Region::insert maps through config_.canonical (Route::Full by
        // default) while the project's canonical query mode is Route::Interval. A lookup
        // that routes differently from the placement would land in another group with
        // every other gate still green -- which is exactly what this part measures.
        std::size_t c_probed = 0, c_placed = 0, c_skipped = 0, c_overflow_placed = 0;
        {
            const std::pair<const char*, Mode> modes[] = {
                {"interval", query_mode},
                {"full", Mode{Route::Full, Prefetch::Off, false, false}},
            };

            for (const auto& mm : modes) {
                const Mode qmode = mm.second;
                auto idx = mk();

                Reference ref;
                for (const auto& kv : keys) ref[kv.first] = kv.second;

                std::mt19937_64 rng(2026092303);
                const std::size_t howmany = std::min<std::size_t>(N, 1024);

                std::size_t m_probed = 0, m_placed = 0, m_skipped = 0, m_overflow = 0;
                std::size_t m_same_length = 0, m_stretched = 0, m_unprobeable = 0;

                for (std::size_t j = 0; j < howmany; ++j) {
                    const std::string& base = keys[rng() % N].first;

                    // TWO probe shapes, deliberately, because they exercise DIFFERENT
                    // placement paths. A key that is LONGER than every live key of its
                    // group sorts after the last of them and lands in the last sub-block
                    // -- which is exactly where a loaded group has no room, so it goes to
                    // overflow. A probe of the SAME LENGTH, one character away in sort
                    // order, falls in the interior, where a sub-block may still have room.
                    // Using only the first shape would make this whole part an
                    // overflow-path test without ever saying so.
                    std::string k;

                    if ((j % 2) == 0) {
                        // same length, adjacent in sort order
                        if (!base.empty()) {
                            for (const char c : {'0', '1', '9', 'a', 'm', 'z'}) {
                                std::string cand = base;
                                if (cand.back() == c) continue;
                                cand.back() = c;
                                if (ref.find(cand) == ref.end()) { k = cand; break; }
                            }
                        }
                        if (!k.empty()) ++m_same_length; else ++m_unprobeable;
                    } else {
                        k = base + "~" + mm.first + std::to_string(j);
                        ++m_stretched;
                    }

                    if (k.empty()) continue;
                    if (ref.find(k) != ref.end())
                        throw std::runtime_error("C: the probe key is already live");

                    const auto where = idx->diag_where(k);
                    const std::size_t r = where.first;
                    const std::uint32_t g_query = where.second;

                    if (r >= idx->region_count())
                        throw std::runtime_error("C: region out of range");
                    if (g_query >= idx->region(r).groups())
                        throw std::runtime_error("C: group out of range");

                    const std::vector<GroupStats> before_g = idx->region(r).group_stats();
                    const std::size_t before_size = idx->region(r).size();
                    const std::size_t before_regions = idx->region_count();
                    ++m_probed;

                    const bool inserted = idx->insert(k, 0xC0FFEEULL + j);
                    if (!inserted)
                        throw std::runtime_error("C: insertion of an absent key reported no insert");

                    ref[k] = 0xC0FFEEULL + j;

                    // An insert may rebuild or split the region, which renumbers and
                    // re-lays out its groups, making the observation below meaningless.
                    // Those are SKIPPED AND COUNTED -- never quietly counted as passes.
                    if (idx->region_count() != before_regions ||
                        idx->region(r).size() != before_size + 1) {
                        ++m_skipped;
                        continue;
                    }

                    const std::vector<GroupStats> after_g = idx->region(r).group_stats();
                    if (after_g.size() != before_g.size())
                        throw std::runtime_error("C: the group count changed without a rebuild");

                    const auto grew = [&](std::uint32_t g) {
                        return (after_g[g].primary_entries + after_g[g].overflow_entries) -
                               (before_g[g].primary_entries + before_g[g].overflow_entries);
                    };

                    std::size_t gained = 0;
                    std::uint32_t who = 0;

                    for (std::uint32_t g = 0; g < after_g.size(); ++g) {
                        const std::size_t d = grew(g);
                        if (d == 0) continue;
                        if (d != 1)
                            throw std::runtime_error("C: one insert made a group gain more than one record");
                        ++gained;
                        who = g;
                    }

                    if (gained != 1)
                        throw std::runtime_error("C: exactly one group should have gained the record");

                    if (who != g_query)
                        throw std::runtime_error(
                            "C: the record was placed in a group other than the one the QUERY path returns");

                    if (after_g[who].overflow_entries > before_g[who].overflow_entries) ++m_overflow;
                    ++m_placed;
                    ++comparisons;
                }

                if (m_probed == 0 || m_placed == 0)
                    throw std::runtime_error(std::string("C: mode ") + mm.first +
                                             " placed and observed nothing");

                if (m_same_length == 0 || m_stretched == 0)
                    throw std::runtime_error(std::string("C: mode ") + mm.first +
                                             " did not exercise BOTH probe shapes, so the "
                                             "primary and the overflow placement paths are "
                                             "not both covered");

                // The skipped ones are still checked for CONTENT, so an insert that
                // rebuilt a region is not left unverified -- it is just not evidence for
                // group identity.
                const auto full = idx->scan(std::string_view{}, idx->size());
                if (full.size() != ref.size())
                    throw std::runtime_error("C: size != reference after the probes");

                for (const auto& p : full) {
                    const auto it = ref.find(p.first);
                    if (it == ref.end() || it->second != p.second)
                        throw std::runtime_error("C: disagrees with the reference");
                    ++comparisons;
                }

                std::cout << "{\"case\":\"C_absent_key_group\",\"mode\":\"" << mm.first
                          << "\",\"keys\":" << N
                          << ",\"probed\":" << m_probed
                          << ",\"placed_and_observed\":" << m_placed
                          << ",\"skipped_after_rebuild\":" << m_skipped
                          << ",\"same_length_probes\":" << m_same_length
                          << ",\"stretched_probes\":" << m_stretched
                          << ",\"unprobeable\":" << m_unprobeable
                          << ",\"placed_into_overflow\":" << m_overflow
                          << ",\"placed_into_primary\":" << (m_placed - m_overflow)
                          << "}" << std::endl;

                c_probed += m_probed;
                c_placed += m_placed;
                c_skipped += m_skipped;
                c_overflow_placed += m_overflow;
            }
        }

        // ---- D: maintenance failure atomicity (I10) --------------------------------
        // Task book section 6.3 asks for "准备—验证—提交". This design has no temporary
        // buffer -- E31 traded it for speed -- so the question is what a failure at each of
        // the two points actually leaves behind. Both are INJECTED, not hoped for:
        // Region::inject_at / inject_point are diagnostic-only hooks in index.hpp.
        std::size_t d_prepare_fired = 0, d_commit_fired = 0;
        {
            // The workload is a REPLACEMENT sequence, not a bare insert run: on this
            // design a plain insert of a longer key lands in the group's last sub-block
            // and goes to overflow without ever reaching compact(). Measured on the audit
            // run: 131,072 appends produced ZERO compactions. Erase-then-insert is what
            // reaches the compaction path (it is the load every earlier round used for
            // exactly this reason), so that is what the injection drives.
            struct Op { int kind; std::string key; std::uint64_t value; };

            std::vector<Op> ops;
            for (std::size_t j = 0; j < N; ++j) {
                ops.push_back({0, keys[j].first, 0});                      // erase a live key
                ops.push_back({1, keys[j].first + "~m" + std::to_string(j),
                               0xD00DULL + j});                            // insert a fresh one
            }

            // PASS A -- with injection OFF, find the operation that triggers the FIRST
            // compaction. Two passes rather than one because the snapshot must be taken
            // immediately before THAT operation, and taking one per operation is far too
            // expensive at these group counts.
            std::size_t k = ops.size();
            {
                auto idx = mk();
                Region::inject_at = -1;

                for (std::size_t j = 0; j < ops.size(); ++j) {
                    if (ops[j].kind == 0) idx->erase(ops[j].key);
                    else idx->insert(ops[j].key, ops[j].value);

                    if (idx->compaction_count() > 0) { k = j; break; }
                }
            }

            if (k >= ops.size())
                throw std::runtime_error(
                    "D: no operation triggered a compaction, so there is nothing to inject into");
            if (ops[k].kind != 1)
                throw std::runtime_error("D: the compaction fired on an erase, which cannot be");

            auto apply = [](Index& idx, const Op& o) {
                if (o.kind == 0) return idx.erase(o.key);
                return idx.insert(o.key, o.value);
            };

            auto apply_ref = [](Reference& ref, const Op& o) {
                if (o.kind == 0) {
                    if (ref.erase(o.key) != 1) throw std::runtime_error("D: erase of an absent key");
                } else {
                    if (ref.find(o.key) != ref.end()) throw std::runtime_error("D: insert of a live key");
                    ref[o.key] = o.value;
                }
            };

            // PASS B -- the PREPARE point. The claim is the strong one: a maintenance that
            // fails while it still costs only its own scratch must leave the index exactly
            // as it was, and the attempted key absent.
            {
                auto idx = mk();

                Reference ref;
                for (const auto& kv : keys) ref[kv.first] = kv.second;

                for (std::size_t j = 0; j < k; ++j) {
                    apply(*idx, ops[j]);
                    apply_ref(ref, ops[j]);
                }

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

                if (!fired)
                    throw std::runtime_error("D: the PREPARE injection never fired");
                ++d_prepare_fired;

                if (!same(before, snapshot(*idx)))
                    throw std::runtime_error(
                        "D: a PREPARE-phase maintenance failure changed the index");

                const auto full = idx->scan(std::string_view{}, idx->size());
                if (full.size() != ref.size())
                    throw std::runtime_error("D: a PREPARE failure changed the record count");

                for (const auto& p : full) {
                    const auto it = ref.find(p.first);
                    if (it == ref.end() || it->second != p.second)
                        throw std::runtime_error("D: a PREPARE failure changed the content");
                    ++comparisons;
                }

                std::cout << "{\"case\":\"D_prepare_failure_atomic\",\"keys\":" << N
                          << ",\"ops_before_injection\":" << k
                          << ",\"injection_fired\":true"
                          << ",\"index_unchanged\":true"
                          << ",\"records\":" << ref.size() << "}" << std::endl;
            }

            // PASS C -- the COMMIT point. This one is MEASURED, NOT ASSERTED. The in-place
            // replay has no rollback, so what a half-finished commit leaves behind IS the
            // finding; the gate reports it rather than asserting a claim it cannot support.
            {
                auto idx = mk();

                Reference ref;
                for (const auto& kv : keys) ref[kv.first] = kv.second;

                for (std::size_t j = 0; j < k; ++j) {
                    apply(*idx, ops[j]);
                    apply_ref(ref, ops[j]);
                }

                const std::size_t records_before = idx->size();

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

                if (!fired)
                    throw std::runtime_error("D: the COMMIT injection never fired");
                ++d_commit_fired;

                std::size_t still_found = 0, lost = 0;

                for (const auto& kv : ref) {
                    const auto r = idx->find(kv.first, query_mode);
                    if (!r.found || r.value != kv.second) ++lost;
                    else ++still_found;
                }

                std::cout << "{\"case\":\"D_commit_failure_measured\",\"keys\":" << N
                          << ",\"ops_before_injection\":" << k
                          << ",\"injection_fired\":true"
                          << ",\"records_before\":" << records_before
                          << ",\"size_after\":" << idx->size()
                          << ",\"reference_size\":" << ref.size()
                          << ",\"still_found\":" << still_found
                          << ",\"records_lost\":" << lost
                          // I10 (2026-09-23): under the safe path this injection is NOT a
                          // reachable failure -- it throws from inside the replay loop, where
                          // nothing can really throw any more. Reported so the number above
                          // cannot be read as "the shipped configuration still loses 24".
                          << ",\"plan_commit\":" << (cfg.compact_plan_commit ? 1 : 0)
                          << ",\"injection_reachable_in_shipped\":"
                          << (cfg.compact_plan_commit ? "false" : "true")
                          << ",\"note\":\"the commit-point injection interrupts the replay "
                             "loop, which no real operation can do once the plan is on\""
                          << "}" << std::endl;
            }
        }

        std::cerr << kind << " G1 contract gate done, " << comparisons
                  << " element comparisons; C: probed " << c_probed
                  << ", observed " << c_placed << ", skipped " << c_skipped
                  << ", into overflow " << c_overflow_placed
                  << ", into primary " << (c_placed - c_overflow_placed)
                  << "; D: prepare injections " << d_prepare_fired
                  << ", commit injections " << d_commit_fired << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

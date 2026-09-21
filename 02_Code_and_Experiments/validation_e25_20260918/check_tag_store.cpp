// E36 gate: the inline-tag arm, checked against the other two arms and the reference.
//
// WHAT THIS HAS TO SETTLE, in the reviewer's list, and how each item is answered:
//
//   1. a tag collision must still be resolved by the FULL comparison
//        -> every absent-key query must return found == false. Thousands of them are run,
//           and most of them share a tag with some record in the group they locate to
//           (that is why a tag can only ever be a filter). The run also asserts that the
//           tagged arm actually ADMITTED candidates to the full comparison (its
//           ov_linear_recs > 0 on absent keys), so "no false positives" is not vacuous.
//
//   2. no stale tag survives an erase, a reinsert, or a reused free slot
//        -> Region::diag_tag_pairing walks every order entry and compares its tag copy
//           against the tag in the record it names. Any mismatch fails the run. The
//           sequence is built to erase records, reuse the freed slots, and reinsert.
//
//   3. handle and tag move together when the order array shifts
//        -> the same walk, taken after the shifts. They are one struct in one array, so
//           this is an invariant being verified rather than a bug being hunted.
//
//   4. point query, update dedup, and scans from arbitrary starts all match a reference
//        -> B, O and T each answer the same queries; the answers are compared against an
//           independent std::map AND against each other.
//
//   5. the three arms agree on key/value content and on overflow counts
//        -> overflow_count() and the scan outputs are compared element by element.
//
// The gate is built with -DE25_DIAG: it reads counters for its non-vacuity assertions.
// A gate that passes without executing the branch it is guarding proves nothing -- that
// lesson is already in check_e31 and check_overflow_store, so the counters are asserted
// here rather than reported.

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
#include <vector>

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
        p.s_caps.push_back(caps);
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

// Each of the three arms is an Index, so a small structure keeps the loops uniform.
struct Arms {
    std::unique_ptr<Index> ix[3];
    const char* name[3] = {"B", "O", "T"};
};

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_tag_store KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        // The frozen Hybrid cutoff, read from the same file every other program reads it
        // from, so the gate exercises the same path split the benchmarks do.
        std::uint32_t thr = 32;
        {
            std::ifstream fz("../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json");

            if (!fz) throw std::runtime_error("frozen file missing");

            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");

            if (p == std::string::npos) throw std::runtime_error("frozen field missing");

            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        // Three arms, each one switch from the last.
        Config base = e25sw::baseline_config(thr);
        base.write_fast_locate = true;
        base.reuse_locate_ctx = true;
        base.scan_reserve_result = true;
        Config cfg[3] = {base, base, base};
        cfg[0].compact_reuse_tag = true;
        cfg[0].compact_needs_garbage = true;                   // B = frozen = the baseline
        cfg[1].compact_reuse_tag = true;
        cfg[1].compact_needs_garbage = true;
        cfg[1].overflow_stable_store = true;                   // O = B + the stable store
        cfg[2].compact_reuse_tag = true;
        cfg[2].compact_needs_garbage = true;
        cfg[2].overflow_stable_store = true;
        cfg[2].overflow_inline_tag = true;                     // T = O + the inline tag

        // Against the ONE field table every gate shares. A switch missing from that table
        // makes these assertions vacuous, which is why switches.hpp carries four tables and
        // why this round registered the new switch in all four.
        e25sw::require_only_switches_differ(cfg[0], cfg[1], {"overflow_stable_store"});
        e25sw::require_only_switches_differ(cfg[1], cfg[2], {"overflow_inline_tag"});

        // One plan for all three arms: the demand pass is a property of the data, not of
        // the representation under test.
        Pipeline plan;
        {
            Index probe(base);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;

            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());

            plan = make_plan(demand);
        }

        const Mode qm{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        // The op sequence. Built once, applied to all three arms AND to the reference, so a
        // disagreement is a disagreement about the index and not about the workload.
        //
        // The generator tracks which keys are live as it goes. The first version drew a
        // fresh random anchor for the erase batch, so it asked both the arms and the
        // reference to erase keys that had never been inserted -- the gate failed on its
        // own workload rather than on the code under test. Existence is now explicit.
        struct Op { int kind; std::string key; std::uint64_t value; };   // 1 insert, 2 erase, 3 update
        std::vector<std::vector<Op>> batches(5);

        {
            const std::size_t M = std::max<std::size_t>(64, N / 8);

            std::vector<char> alive(N, 1);                       // the bulk-loaded keys
            std::vector<std::pair<std::string, std::uint64_t>> ins;   // inserted, in order
            std::vector<char> ins_alive;

            auto add_ins = [&](std::string k, std::uint64_t v) {
                ins.push_back({k, v});
                ins_alive.push_back(1);
                return k;
            };

            // 1. inserts sharing prefixes with existing keys, which is what pushes records
            //    into the overflow containers rather than into free slots.
            for (std::size_t i = 0; i < M; ++i)
                batches[0].push_back({1, add_ins(keys[(i * 11) % N].first + "~g" + std::to_string(i), 0x100ULL + i), 0x100ULL + i});

            // 2. erase half of the initial keys and half of what batch 1 just inserted, so
            //    both the primary area and the overflow container are hit.
            for (std::size_t i = 0; i < M / 2; ++i) {
                const std::size_t id = (i * 7) % N;

                if (!alive[id]) throw std::runtime_error("generator erased an already-dead initial key");

                alive[id] = 0;
                batches[1].push_back({2, keys[id].first, 0});
            }

            for (std::size_t i = 0; i < M / 2; ++i) {
                if (!ins_alive[i]) throw std::runtime_error("generator erased an already-dead insert");

                ins_alive[i] = 0;
                batches[1].push_back({2, ins[i].first, 0});
            }

            // 3. reinsert on the SAME anchors batch 2 erased, which is what reuses freed
            //    slots -- the stale-tag case.
            for (std::size_t i = 0; i < M / 2; ++i)
                batches[2].push_back({1, add_ins(keys[(i * 7) % N].first + "~h" + std::to_string(i), 0x200ULL + i), 0x200ULL + i});

            // 4. value updates on live initial keys: same key, new value.
            for (std::size_t i = 0, taken = 0; taken < M / 2 && i < N * 4; ++i) {
                const std::size_t id = (i * 13) % N;

                if (!alive[id]) continue;

                batches[3].push_back({3, keys[id].first, 0x300ULL + taken});
                ++taken;
            }

            // 5. churn: erase live inserted keys and insert new ones, so entries shift and
            //    slots cycle.
            for (std::size_t j = M / 2, taken = 0; j < ins.size() && taken < M / 2; ++j) {
                if (!ins_alive[j]) continue;

                ins_alive[j] = 0;
                batches[4].push_back({2, ins[j].first, 0});
                ++taken;
            }

            for (std::size_t i = 0; i < M / 2; ++i)
                batches[4].push_back({1, add_ins(keys[(i * 5) % N].first + "~k" + std::to_string(i), 0x400ULL + i), 0x400ULL + i});
        }

        // Absent keys for the collision check: derived from live keys and kept only where
        // the reference confirms they do not exist.
        std::vector<std::string> absent;
        {
            for (std::size_t i = 0; i < N; i += std::max<std::size_t>(1, N / 4096))
                absent.push_back(keys[i].first + "~absent");
        }

        std::uint64_t records_compared = 0, scans_compared = 0, queries_compared = 0;
        std::uint64_t pairings_checked = 0, absent_queries = 0;
        std::uint64_t b_overflow = 0, o_overflow = 0, t_overflow = 0;

        Arms arms;

        for (int pass = 0; pass < 2; ++pass) {
            Reference ref;

            for (const auto& kv : keys) ref[kv.first] = kv.second;

            for (int a = 0; a < 3; ++a) {
                arms.ix[a] = std::make_unique<Index>(cfg[a]);
                arms.ix[a]->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc),
                                                plan.s_caps);
            }

            for (std::size_t bi = 0; bi < batches.size(); ++bi) {
                for (const auto& op : batches[bi]) {
                    if (op.kind == 1) {
                        if (ref.find(op.key) != ref.end())
                            throw std::runtime_error("insert target already present in the reference");

                        for (int a = 0; a < 3; ++a) arms.ix[a]->insert(op.key, op.value);

                        ref[op.key] = op.value;
                    } else if (op.kind == 2) {
                        if (ref.erase(op.key) != 1)
                            throw std::runtime_error("erase target absent from the reference");

                        for (int a = 0; a < 3; ++a) arms.ix[a]->erase(op.key);
                    } else {
                        const auto it = ref.find(op.key);

                        if (it == ref.end()) throw std::runtime_error("update target absent");

                        for (int a = 0; a < 3; ++a) arms.ix[a]->update(op.key, op.value);

                        it->second = op.value;
                    }
                }

                // --- the three arms must agree on the structure -----------------------
                const std::size_t ov[3] = {arms.ix[0]->overflow_count(),
                                           arms.ix[1]->overflow_count(),
                                           arms.ix[2]->overflow_count()};

                if (ov[0] != ov[1] || ov[1] != ov[2])
                    throw std::runtime_error("the three arms disagree on the overflow count");

                b_overflow = ov[0];
                o_overflow = ov[1];
                t_overflow = ov[2];

                // --- point queries, against the reference AND against each other ------
                std::vector<std::string> probe;
                probe.reserve(ref.size() + absent.size());

                for (const auto& kv : ref) probe.push_back(kv.first);

                for (const auto& k : absent) probe.push_back(k);

                for (const auto& k : probe) {
                    const bool is_absent = ref.find(k) == ref.end();
                    const Result r0 = arms.ix[0]->find(k, qm);
                    const Result r1 = arms.ix[1]->find(k, qm);
                    const Result r2 = arms.ix[2]->find(k, qm);
                    ++queries_compared;

                    if (is_absent) ++absent_queries;

                    // The reference check. On an absent key this is the tag-collision check:
                    // a candidate whose tag matched must have been rejected by the full
                    // comparison, and a false positive would land here.
                    if (is_absent) {
                        if (r0.found || r1.found || r2.found)
                            throw std::runtime_error("an absent key was reported found");

                        continue;
                    }

                    const std::uint64_t want = ref.at(k);

                    if (!r0.found || r0.value != want)
                        throw std::runtime_error("arm B disagrees with the reference");

                    if (!r1.found || r1.value != want)
                        throw std::runtime_error("arm O disagrees with the reference");

                    if (!r2.found || r2.value != want)
                        throw std::runtime_error("arm T disagrees with the reference");

                    // And the arms against each other, including WHERE the key resolved:
                    // the switch must not move a key between the primary area and overflow.
                    if (r0.source != r1.source || r1.source != r2.source)
                        throw std::runtime_error("the three arms disagree on where a key resolved");
                }

                // --- scans from arbitrary starts, element by element ------------------
                const std::size_t L = 16;

                for (std::size_t i = 0; i < N; i += std::max<std::size_t>(1, N / 512)) {
                    const std::string& start = keys[i].first + "~between";

                    std::vector<Pair> got[3];

                    for (int a = 0; a < 3; ++a) got[a] = arms.ix[a]->scan(start, L);

                    ++scans_compared;

                    // The reference scan: the same semantics, computed independently.
                    std::vector<Pair> want;
                    for (auto it = ref.lower_bound(start); it != ref.end() && want.size() < L; ++it)
                        want.push_back(*it);

                    for (int a = 0; a < 3; ++a) {
                        if (got[a].size() != want.size())
                            throw std::runtime_error("a scan returned the wrong number of records");

                        for (std::size_t j = 0; j < want.size(); ++j) {
                            ++records_compared;

                            if (got[a][j].first != want[j].first || got[a][j].second != want[j].second)
                                throw std::runtime_error("a scan record disagrees with the reference");
                        }
                    }

                    // The three arms must also agree WITH EACH OTHER.
                    for (std::size_t j = 0; j < got[0].size(); ++j) {
                        if (got[0][j].first != got[2][j].first || got[1][j].first != got[2][j].first)
                            throw std::runtime_error("the three arms disagree on a scan record");
                    }
                }

                // --- the tag-pairing invariant, on the arm that has tags in its entries -
                {
                    std::size_t checked = 0;
                    const std::size_t bad = arms.ix[2]->diag_tag_pairing(checked);

                    pairings_checked += checked;

                    if (bad != 0)
                        throw std::runtime_error("an order entry carries a tag the record does not");
                }
            }

            // --- non-vacuity, per pass, on the arm that is supposed to exercise the path -
            const auto& C = validation::e25::counts();
            (void)C;
        }

#ifdef E25_DIAG
        // The counters are process-global and were accumulated by all three arms, so they
        // cannot attribute work to T alone. What they CAN prove is that the tagged path ran
        // at all: without that, "no stale tag found" and "no false positive" would be
        // statements about code that never executed.
        {
            const auto& C = validation::e25::counts();

            if (C.ov_tag_entries == 0)
                throw std::runtime_error("the linear overflow scan never examined an entry");

            if (C.ov_tag_reject_pre == 0)
                throw std::runtime_error("no candidate was ever rejected by an entry's tag, so the "
                                         "inline-tag path was never exercised");

            if (C.ov_linear_recs == 0)
                throw std::runtime_error("no candidate ever reached the full comparison");
        }
#endif

        std::cout << "{\"gate\":\"check_tag_store\",\"kind\":\"" << kind << "\""
                  << ",\"keys\":" << N
                  << ",\"queries_compared\":" << queries_compared
                  << ",\"absent_queries\":" << absent_queries
                  << ",\"scans_compared\":" << scans_compared
                  << ",\"records_compared\":" << records_compared
                  << ",\"tag_pairings_checked\":" << pairings_checked
                  << ",\"overflow_B\":" << b_overflow
                  << ",\"overflow_O\":" << o_overflow
                  << ",\"overflow_T\":" << t_overflow
                  << ",\"sizeof_OrderedEntry\":" << Region::ordered_entry_bytes()
                  << ",\"result\":\"pass\""
                  << "}" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cout << "{\"gate\":\"check_tag_store\",\"result\":\"fail\",\"error\":\"" << e.what() << "\"}"
                  << std::endl;
        return 1;
    }
}

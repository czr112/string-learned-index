// E25 gate: do V1 and V2 build and maintain the SAME structure as V0?
//
// V1 changes which search the write paths run; V2 changes how many searches run.
// Neither is allowed to change a single record, a single value, or the physical
// layout. So the gate builds three indexes from the same plan and puts the SAME
// operation sequence through all three, comparing them ELEMENT BY ELEMENT -- not
// by checksum -- after every batch and at the end.
//
// If V1 or V2 ever disagreed with V0 about a placement, the comparison would show
// which key and which value, not merely "a digest changed".
//
// Coverage the sequence is built to reach:
//   * update/erase in a primary sub-block, and in the overflow container
//   * erase of the FIRST slot of a sub-block (rebuilds the boundary directory)
//   * erase of the LAST live entry of a sub-block
//   * insert that fits, insert that overflows, insert that triggers compaction
//   * duplicate insert (must be refused, structure unchanged)
//   * miss on update / erase / upsert
//   * erase-then-insert of the same key
//   * groups spanning several sub-blocks, so the SIMD tag batches past the first
//     16 slots are exercised
//
// Output: one JSON line. The counters are of the same kind the other gates print.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
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
        if (argc < 3) throw std::invalid_argument("check_e25_write KIND N [TRANCOCSV] [RUNTAG]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
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

        // Baseline built field by field from the shared helper, NOT from Config
        // defaults: a default that changes at freeze time must not move this gate.
        // Compaction and scan switches are pinned to their pre-E26/E27 values.
        const Config c = e25sw::baseline_config(thr);
        Config c0 = c, c1 = c, c2 = c;
        c1.write_fast_locate = true;                              // V1
        c2.write_fast_locate = true;  c2.reuse_locate_ctx = true; // V2
        // Fail loudly if an arm stopped differing, instead of "passing" a comparison
        // that never happened.
        e25sw::require_only_switches_differ(c0, c1, {"write_fast_locate"});
        e25sw::require_only_switches_differ(c1, c2, {"reuse_locate_ctx"});
        e25sw::require_only_switches_differ(c0, c2, {"write_fast_locate", "reuse_locate_ctx"});
        std::cerr << "E25 arms V0[" << e25sw::switches(c0) << "] V1["
                  << e25sw::switches(c1) << "] V2[" << e25sw::switches(c2) << "]"
                  << std::endl;

        Pipeline plan;
        {
            Index probe(c);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        auto mk = [&](const Config& cfg) {
            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            return s;
        };
        auto v0 = mk(c0), v1 = mk(c1), v2 = mk(c2);

        Reference ref;
        for (const auto& kv : keys) ref[kv.first] = kv.second;

        std::uint64_t ops = 0, cmp_records = 0, batches = 0, mismatches = 0;

        // Element-by-element comparison of the full live content, sorted. `scan` is
        // the public way to read everything back, and it exercises the ordering
        // contract rather than a private accessor.
        auto content = [](const Index& s) { return s.scan(std::string_view{}, s.size()); };
        auto check = [&](const char* what) {
            ++batches;
            const auto a = content(*v0);
            const auto b = content(*v1);
            const auto d = content(*v2);
            if (a.size() != ref.size())
                throw std::runtime_error(std::string(what) + ": V0 size != reference");
            if (a.size() != b.size() || a.size() != d.size())
                throw std::runtime_error(std::string(what) + ": variant size differs");
            if (v0->size() != v1->size() || v0->size() != v2->size())
                throw std::runtime_error(std::string(what) + ": Index::size differs");
            if (v0->overflow_count() != v1->overflow_count() ||
                v0->overflow_count() != v2->overflow_count())
                throw std::runtime_error(std::string(what) + ": overflow count differs");
            if (v0->compaction_count() != v1->compaction_count() ||
                v0->compaction_count() != v2->compaction_count())
                throw std::runtime_error(std::string(what) + ": compaction count differs");
            for (std::size_t i = 0; i < a.size(); ++i) {
                ++cmp_records;
                if (a[i].first != b[i].first || a[i].second != b[i].second ||
                    a[i].first != d[i].first || a[i].second != d[i].second) {
                    ++mismatches;
                    throw std::runtime_error(std::string(what) + ": variant content differs at " +
                                             std::to_string(i) + " key=" + a[i].first);
                }
                const auto it = ref.find(a[i].first);
                if (it == ref.end() || it->second != a[i].second)
                    throw std::runtime_error(std::string(what) + ": V0 disagrees with reference: " +
                                             a[i].first);
            }
        };

        auto apply_all = [&](int kind_op, const std::string& k, std::uint64_t val, bool* r0,
                             bool* r1, bool* r2) {
            if (kind_op == 0) {
                *r0 = v0->update(k, val); *r1 = v1->update(k, val); *r2 = v2->update(k, val);
            } else if (kind_op == 1) {
                *r0 = v0->insert(k, val); *r1 = v1->insert(k, val); *r2 = v2->insert(k, val);
            } else if (kind_op == 2) {
                *r0 = v0->erase(k); *r1 = v1->erase(k); *r2 = v2->erase(k);
            } else {
                *r0 = v0->upsert(k, val); *r1 = v1->upsert(k, val); *r2 = v2->upsert(k, val);
            }
            if (*r0 != *r1 || *r0 != *r2)
                throw std::runtime_error("variant return value differs for " + k);
            ++ops;
        };

        std::mt19937_64 rng(2026091803);
        std::vector<std::size_t> order(N);
        for (std::size_t i = 0; i < N; ++i) order[i] = i;
        std::shuffle(order.begin(), order.end(), rng);

        check("after build");

        const std::size_t q = N / 8;
        std::size_t at = 0;

        // ---- 1. updates -------------------------------------------------------
        for (std::size_t j = 0; j < q; ++j) {
            const std::size_t i = order[at++];
            const std::string k = keys[i].first;
            const std::uint64_t nv = 0xABCD0000ULL + j;
            bool a, b, d;
            apply_all(0, k, nv, &a, &b, &d);
            if (!a) throw std::runtime_error("update missed a live key");
            ref[k] = nv;
            if ((j & 63) == 63) check("after updates");
        }
        check("after update phase");

        // ---- 2. duplicate inserts (must be refused) ---------------------------
        for (std::size_t j = 0; j < q / 4; ++j) {
            const std::string k = keys[order[at++]].first;
            bool a, b, d;
            apply_all(1, k, 0xDEADULL, &a, &b, &d);
            if (a) throw std::runtime_error("duplicate insert was accepted");
            if ((j & 63) == 63) check("after duplicate inserts");
        }
        check("after duplicate-insert phase");

        // ---- 3. misses --------------------------------------------------------
        for (std::size_t j = 0; j < q / 4; ++j) {
            const std::string k = keys[order[at++]].first + "~absent" + std::to_string(j);
            bool a, b, d;
            apply_all(0, k, 1, &a, &b, &d);
            if (a) throw std::runtime_error("update found an absent key");
            apply_all(2, k, 0, &a, &b, &d);
            if (a) throw std::runtime_error("erase found an absent key");
            if ((j & 31) == 31) check("after misses");
        }
        check("after miss phase");

        // ---- 4. erases --------------------------------------------------------
        for (std::size_t j = 0; j < q; ++j) {
            const std::string k = keys[order[at++]].first;
            bool a, b, d;
            apply_all(2, k, 0, &a, &b, &d);
            if (!a) throw std::runtime_error("erase missed a live key");
            ref.erase(k);
            if ((j & 63) == 63) check("after erases");
        }
        check("after erase phase");

        // ---- 5. inserts (some fit, some overflow, some compact) ---------------
        for (std::size_t j = 0; j < q; ++j) {
            const std::string k = keys[order[j % N]].first + "~new" + std::to_string(j);
            const std::uint64_t nv = 0x1234ULL + j;
            bool a, b, d;
            apply_all(1, k, nv, &a, &b, &d);
            if (!a) throw std::runtime_error("insert refused a new key");
            ref[k] = nv;
            if ((j & 63) == 63) check("after inserts");
        }
        check("after insert phase");

        // ---- 6. erase-then-insert of the same key -----------------------------
        for (std::size_t j = 0; j < q / 2; ++j) {
            const std::string k = keys[order[at++]].first;
            bool a, b, d;
            apply_all(2, k, 0, &a, &b, &d);
            if (!a) throw std::runtime_error("erase-then-insert: erase failed");
            ref.erase(k);
            const std::uint64_t nv = 0x5678ULL + j;
            apply_all(1, k, nv, &a, &b, &d);
            if (!a) throw std::runtime_error("erase-then-insert: insert failed");
            ref[k] = nv;
            if ((j & 31) == 31) check("after erase-then-insert");
        }
        check("after erase-then-insert phase");

        // ---- 7. upserts (both hit and miss) -----------------------------------
        for (std::size_t j = 0; j < q; ++j) {
            const bool hit = (j % 2) == 0;
            const std::string k = hit ? keys[order[at++ % N]].first
                                      : keys[j % N].first + "~up" + std::to_string(j);
            const std::uint64_t nv = 0x9E00ULL + j;
            bool a, b, d;
            apply_all(3, k, nv, &a, &b, &d);
            if (hit) {
                if (a) throw std::runtime_error("upsert on a live key reported an insert");
                ref[k] = nv;
            } else {
                if (!a) throw std::runtime_error("upsert on a new key reported an update");
                ref[k] = nv;
            }
            if ((j & 63) == 63) check("after upserts");
        }
        check("after upsert phase");

        if (mismatches) throw std::runtime_error("mismatches recorded");

        // ---- 8. targeted branch coverage -------------------------------------
        // "The final content matched" is NOT evidence that a branch ran -- that is
        // exactly the inference that missed the Index-level control flow E25 broke
        // once. Each branch the rewrite touched gets a purpose-built case and an
        // event-counter assertion.
        auto& C = validation::e25::counts();
        std::uint64_t br_fallback = 0, br_prefix = 0, br_split = 0;

        // (a) V2 NotLocated fallback. find_impl only returns NotLocated for an EMPTY
        //     region (a prefix mismatch cannot reach upsert_one), so the region has to
        //     be emptied on purpose.
        {
            const std::size_t m = std::min<std::size_t>(N, 128);
            std::vector<Pair> tiny(keys.begin(), keys.begin() + m);
            Index t(c2);
            t.bulk_load(tiny);
            for (const auto& kv : tiny) t.erase(kv.first);
            if (t.size() != 0) throw std::runtime_error("targeted(a): index not emptied");
            C.reset();
            t.upsert(tiny[0].first + "~back", 7);
            br_fallback = C.notlocated_fallback;
            if (t.size() != 1) throw std::runtime_error("targeted(a): reinsert failed");
            if (br_fallback == 0)
                throw std::runtime_error("targeted(a): NotLocated fallback did NOT run");
        }

        // (b) prefix-mismatch region rebuild. A key sorting BELOW every stored key is
        //     routed to region 0; if region 0 has a non-empty prefix it cannot accept
        //     the key, and Index::insert takes the rebuild branch. Keys sharing an
        //     explicit prefix make this deterministic instead of data-dependent.
        {
            std::vector<Pair> pk;
            for (int i = 0; i < 64; ++i) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "pref%04d", i);
                pk.emplace_back(buf, 100 + i);
            }
            Index p(c2);
            p.bulk_load(pk);
            C.reset();
            p.insert("aaa", 1);   // sorts below "pref...." -> region 0, prefix mismatch
            br_prefix = C.prefix_rebuilds;
            if (br_prefix == 0)
                throw std::runtime_error("targeted(b): prefix rebuild did NOT run");
        }

        // (c) region split at the 2*region_keys threshold. A small region_keys makes
        //     the threshold reachable from a handful of inserts.
        {
            Config cs = c2;
            cs.region_keys = 16;
            std::vector<Pair> sk;
            for (int i = 0; i < 40; ++i) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "sk%04d", i);
                sk.emplace_back(buf, 200 + i);
            }
            Index s(cs);
            s.bulk_load(sk);
            C.reset();
            for (int i = 0; i < 200; ++i) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "sk%04d~x", i);
                s.insert(buf, 300 + i);
            }
            br_split = C.threshold_splits;
            if (br_split == 0)
                throw std::runtime_error("targeted(c): region split did NOT run");
        }

        std::cout << "{\"status\":\"passed\",\"keys\":" << N
                  << ",\"operations\":" << ops
                  << ",\"comparison_batches\":" << batches
                  << ",\"records_compared\":" << cmp_records
                  << ",\"variants\":3"
                  << ",\"cfg_v0\":\"" << e25sw::switches(c0) << "\""
                  << ",\"cfg_v1\":\"" << e25sw::switches(c1) << "\""
                  << ",\"cfg_v2\":\"" << e25sw::switches(c2) << "\"" 
                  << ",\"final_size\":" << v0->size()
                  << ",\"final_overflow\":" << v0->overflow_count()
                  << ",\"compactions\":" << v0->compaction_count()
                  << ",\"branch_notlocated_fallback\":" << br_fallback
                  << ",\"branch_prefix_rebuild\":" << br_prefix
                  << ",\"branch_threshold_split\":" << br_split
                  << ",\"seed\":2026091803}" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

// E25 call matrix: what each write API actually invokes, per category, for V0/V1/V2.
//
// The point is to replace inference with counts. E24 could only show that the write
// paths were 34x-93x more comparison-heavy than find(); it could not say WHICH work
// was repeated. This program runs the same operation sequence on the same state for
// all three variants and reports, per API and per hit category, the routing calls,
// model predictions, existence searches, fingerprint bytes hashed, string compares
// and their bytes, and the placement/maintenance events.
//
// Accounting rules this program follows (they matter as much as the numbers):
//
//   * The DIAGNOSTIC WINDOW is explicit: counters are reset immediately before the
//     category's operations and read immediately after. Key selection, reference
//     upkeep and answer checking happen OUTSIDE the window and are not counted.
//   * `predict_calls` (model predictions), `existence_searches` (full duplicate
//     checks) and the placement probes are SEPARATE counters. "Predicting again
//     after a duplicate check" is not the same as running two full searches, and is
//     not reported as such.
//   * Fingerprint work is reported as BYTES HASHED, not as Fingerprint::of calls.
//     The prediction hashes the bytes it consumes and find_impl finishes with
//     tail(), so the fused path produces a full fingerprint without ever calling
//     Fingerprint::of. The call count is printed only as an auxiliary column.
//   * `cmp_bytes` is a LOGICAL comparison quantity under this protocol: bytes walked
//     by the comparison routine. It is not a DRAM read count and is not called one.
//   * Placement failures are counted as the FIRST rejection reason inside
//     insert_into (slot count checked before suffix bytes), so they are mutually
//     exclusive and their sum is bounded by the attempt count.
//
// Build with -DE25_DIAG (see tools_build.py). Never a timing build.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
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

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;
using Counts = validation::e25::Counts;

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
        if (argc < 3) throw std::invalid_argument("diag_e25_calls KIND N [TRANCOCSV] [RUNTAG]");
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

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        c.multiblock = true;
        c.reuse_group_ids = true;   // the frozen default is R1
        c.ov_threshold = thr;

        Config cv[3] = {c, c, c};
        cv[1].write_fast_locate = true;                              // V1
        cv[2].write_fast_locate = true;  cv[2].reuse_locate_ctx = true;  // V2
        const char* vname[3] = {"V0", "V1", "V2"};

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

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        // ---- select the probe keys ONCE, outside every diagnostic window -------
        // The classification uses a V0 index built from the same plan, and is
        // verified against the reference, so all three variants operate on the same
        // key sets and the same state.
        std::vector<std::string> upd_primary, upd_overflow, upd_miss;
        {
            auto s = mk(cv[0]);
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;
            std::mt19937_64 rng(2026091901);
            const std::size_t want = std::min<std::size_t>(2048, N);
            std::size_t seen = 0, tries = 0;
            while (seen < want && tries < want * 20) {
                const std::string& k = keys[rng() % N].first;
                ++tries;
                const Result r = s->find(k, qmode);
                const bool ref_has = ref.find(k) != ref.end();
                if (r.found != ref_has) throw std::runtime_error("classification disagrees with reference");
                if (!r.found) { upd_miss.push_back(k); }
                else if (r.source == 2) upd_overflow.push_back(k);
                else upd_primary.push_back(k);
                ++seen;
            }
            std::mt19937_64 rng2(2026091902);
            const std::size_t wa = std::min<std::size_t>(2048, N);
            for (std::size_t i = 0; i < wa; ++i)
                upd_miss.push_back(keys[rng2() % N].first + "~absent" + std::to_string(i));
        }
        std::vector<std::string> ins_new, ins_dup, ups_hit, ups_new;
        {
            std::mt19937_64 rng(2026091903);
            const std::size_t w = std::min<std::size_t>(2048, N);
            for (std::size_t i = 0; i < w; ++i) {
                ins_new.push_back(keys[rng() % N].first + "~ins" + std::to_string(i));
                ups_new.push_back(keys[rng() % N].first + "~upn" + std::to_string(i));
                ins_dup.push_back(keys[rng() % N].first);
                ups_hit.push_back(keys[rng() % N].first);
            }
        }
        // Deduplicate so a repeated key cannot make "how many succeeded" ambiguous.
        auto dedup = [](std::vector<std::string>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        dedup(ins_new); dedup(ups_new); dedup(ins_dup); dedup(ups_hit);
        dedup(upd_primary); dedup(upd_overflow); dedup(upd_miss);

        std::ofstream out("results/main/e25_calls.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);
        auto& C = validation::e25::counts();

        auto emit = [&](const char* variant, const char* api, const char* category,
                        std::size_t nops, const Counts& k) {
            out << "{\"kind\":\"" << kind << "\",\"run_tag\":\"" << run_tag
                << "\",\"variant\":\"" << variant << "\",\"api\":\"" << api
                << "\",\"category\":\"" << category << "\",\"ops\":" << nops
                << ",\"locate_calls\":" << k.locate_calls
                << ",\"predict_calls\":" << k.predict_calls
                << ",\"existence_searches\":" << k.existence_searches
                << ",\"fp_of_calls\":" << k.fp_of_calls
                << ",\"fp_hash_bytes\":" << k.fp_hash_bytes
                << ",\"cmp_calls\":" << k.cmp_calls
                << ",\"cmp_bytes\":" << k.cmp_bytes
                << ",\"place_attempts\":" << k.place_attempts
                << ",\"place_fail_slots\":" << k.place_fail_slots
                << ",\"place_fail_bytes\":" << k.place_fail_bytes
                << ",\"compactions\":" << k.compactions
                << ",\"region_rebuilds\":" << k.region_rebuilds
                << ",\"erase_shifts\":" << k.erase_shifts
                << ",\"erase_released_primary\":" << k.erase_released_primary
                << ",\"erase_released_overflow\":" << k.erase_released_overflow
                << ",\"string_allocs\":" << k.string_allocs
                << ",\"string_frees\":" << k.string_frees
                << ",\"notlocated_fallback\":" << k.notlocated_fallback
                << "}" << std::endl;
        };

        for (int v = 0; v < 3; ++v) {
            // ---- updates: state preserving, so one index serves all three --------
            {
                auto s = mk(cv[v]);
                const char* cats[3] = {"primary_hit", "overflow_hit", "miss"};
                const std::vector<std::string>* sets[3] = {&upd_primary, &upd_overflow, &upd_miss};
                for (int ci = 0; ci < 3; ++ci) {
                    C.reset();
                    std::uint64_t ok = 0;
                    for (const auto& k : *sets[ci]) if (s->update(k, 0xE25ULL)) ++ok;
                    const Counts k = C;
                    if ((ci < 2) != (ok > 0))
                        throw std::runtime_error("update category produced no hits on a hit set");
                    emit(vname[v], "update", cats[ci], sets[ci]->size(), k);
                }
            }
            // ---- erases: destructive, so a fresh index per variant --------------
            {
                auto s = mk(cv[v]);
                const char* cats[3] = {"primary_hit", "overflow_hit", "miss"};
                const std::vector<std::string>* sets[3] = {&upd_primary, &upd_overflow, &upd_miss};
                for (int ci = 0; ci < 3; ++ci) {
                    C.reset();
                    std::uint64_t ok = 0;
                    for (const auto& k : *sets[ci]) if (s->erase(k)) ++ok;
                    const Counts k = C;
                    if (ci < 2 && ok == 0)
                        throw std::runtime_error("erase category produced no hits on a hit set");
                    emit(vname[v], "erase", cats[ci], sets[ci]->size(), k);
                }
            }
            // ---- insert: new keys, then duplicates on the same index ------------
            {
                auto s = mk(cv[v]);
                C.reset();
                std::uint64_t ok = 0;
                for (const auto& k : ins_new) if (s->insert(k, 0xE25ULL)) ++ok;
                emit(vname[v], "insert", "new_key", ins_new.size(), C);
                if (ok != ins_new.size()) throw std::runtime_error("insert refused a new key");

                C.reset();
                std::uint64_t dup = 0;
                for (const auto& k : ins_dup) if (s->insert(k, 0xE25ULL)) ++dup;
                emit(vname[v], "insert", "duplicate_key", ins_dup.size(), C);
                if (dup != 0) throw std::runtime_error("duplicate insert was accepted");
            }
            // ---- upsert: existing key vs new key, on the SAME index -------------
            {
                auto s = mk(cv[v]);
                C.reset();
                std::uint64_t ins = 0;
                for (const auto& k : ups_hit) if (s->upsert(k, 0xE25ULL)) ++ins;
                emit(vname[v], "upsert", "existing_key", ups_hit.size(), C);
                if (ins != 0) throw std::runtime_error("upsert on a live key reported an insert");

                C.reset();
                std::uint64_t upd = 0;
                for (const auto& k : ups_new) if (!s->upsert(k, 0xE25ULL)) ++upd;
                emit(vname[v], "upsert", "new_key", ups_new.size(), C);
                if (upd != 0) throw std::runtime_error("upsert on a new key reported an update");
            }
        }

        // ---- the slow structural paths, counted once and reported SEPARATELY ----
        // A region rebuild is not a "fixed cost per operation", so it is never folded
        // into the per-operation averages above.
        {
            Config cs = cv[2];
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
            // Compaction events during this phase are part of the structural cost and
            // stay on the row; they are reported alongside the rebuild count rather
            // than mixed into any per-operation average.
            emit("V2", "structural", "region_split_build", 200, C);
        }

        std::cerr << kind << " call matrix done" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

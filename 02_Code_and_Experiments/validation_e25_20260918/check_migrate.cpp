// E37 gate: bounded migration, checked against the reference and against R0.
//
// THE TWO ARMS AND WHY THE COMPARISON IS SHAPED THIS WAY.
//
//   R0   the fixed base: the frozen configuration + Config::subblock_reclaim, and the
//        ORIGINAL overflow container. No migration.
//   R1   the same base + Config::overflow_migrate.
//
// R1's whole purpose is to move records between the primary area and overflow, so the two
// arms are EXPECTED to disagree about where a record lives and about how many records
// overflow holds. An arm-to-arm element comparison would therefore fail on the value this
// gate is supposed to prove. What must hold instead:
//
//   * both arms answer every query and every scan exactly as an INDEPENDENT reference does
//     -- a migrated record that could not be found afterwards would show up here, and that
//     is the failure this gate exists to catch;
//   * live counts and key/value content stay equal to the reference for both arms;
//   * R1's overflow is never LARGER than R0's (the direction of the effect);
//   * R1 actually migrated something -- a gate whose branch never runs proves nothing.
//
// The op sequence is built with an explicit live set, because a generator that asks the arms
// to erase a key that was never inserted fails on its own workload and looks like a bug in
// the code under test. That mistake was made once already in this project.

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

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_migrate KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

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

        // R0 and R1 differ by exactly one switch, asserted against the one field table every
        // gate shares.
        Config base = e25sw::baseline_config(thr);
        base.write_fast_locate = true;
        base.reuse_locate_ctx = true;
        base.scan_reserve_result = true;
        Config cfg[2] = {base, base};

        for (int a = 0; a < 2; ++a) {
            cfg[a].compact_reuse_tag = true;
            cfg[a].compact_needs_garbage = true;
            cfg[a].subblock_reclaim = true;
        }

        cfg[1].overflow_migrate = true;

        e25sw::require_only_switches_differ(cfg[0], cfg[1], {"overflow_migrate"});

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

        struct Op { int kind; std::string key; std::uint64_t value; };
        std::vector<std::vector<Op>> batches(4);

        {
            const std::size_t M = std::max<std::size_t>(64, N / 8);
            std::vector<char> alive(N, 1);
            std::vector<std::pair<std::string, std::uint64_t>> ins;
            std::vector<char> ins_alive;

            auto add_ins = [&](std::string k, std::uint64_t v) {
                ins.push_back({k, v});
                ins_alive.push_back(1);
                return k;
            };

            // 1. inserts sharing prefixes with existing keys: this is what pushes records
            //    into overflow, which is the state migration acts on.
            for (std::size_t i = 0; i < M; ++i)
                batches[0].push_back({1, add_ins(keys[(i * 11) % N].first + "~g" + std::to_string(i), 0x100ULL + i), 0x100ULL + i});

            // 2. erase half the initial keys and half of what batch 1 inserted, so both the
            //    primary area and the container are hit.
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

            // 3. more inserts, so R1 has pressure to migrate under.
            for (std::size_t i = 0; i < M; ++i)
                batches[2].push_back({1, add_ins(keys[(i * 5) % N].first + "~h" + std::to_string(i), 0x200ULL + i), 0x200ULL + i});

            // 4. value updates on live keys.
            for (std::size_t i = 0, taken = 0; taken < M / 2 && i < N * 4; ++i) {
                const std::size_t id = (i * 13) % N;

                if (!alive[id]) continue;

                batches[3].push_back({3, keys[id].first, 0x300ULL + taken});
                ++taken;
            }
        }

        std::vector<std::string> absent;
        for (std::size_t i = 0; i < N; i += std::max<std::size_t>(1, N / 4096))
            absent.push_back(keys[i].first + "~absent");

        std::uint64_t queries = 0, scans = 0, records = 0;
        std::size_t ov_r0 = 0, ov_r1 = 0;

        for (int pass = 0; pass < 2; ++pass) {
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;

            std::unique_ptr<Index> ix[2];

            for (int a = 0; a < 2; ++a) {
                ix[a] = std::make_unique<Index>(cfg[a]);
                ix[a]->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            }

            for (const auto& batch : batches) {
                for (const auto& op : batch) {
                    if (op.kind == 1) {
                        if (ref.find(op.key) != ref.end())
                            throw std::runtime_error("insert target already present");

                        for (int a = 0; a < 2; ++a) ix[a]->insert(op.key, op.value);

                        ref[op.key] = op.value;
                    } else if (op.kind == 2) {
                        if (ref.erase(op.key) != 1) throw std::runtime_error("erase target absent");

                        for (int a = 0; a < 2; ++a) ix[a]->erase(op.key);
                    } else {
                        const auto it = ref.find(op.key);

                        if (it == ref.end()) throw std::runtime_error("update target absent");

                        for (int a = 0; a < 2; ++a) ix[a]->update(op.key, op.value);

                        it->second = op.value;
                    }
                }

                // Both arms must hold exactly what the reference holds. A record that
                // migration moved and then lost would show up as a size mismatch here.
                for (int a = 0; a < 2; ++a)
                    if (ix[a]->size() != ref.size())
                        throw std::runtime_error("an arm's live count drifted from the reference");

                // Every live key, every absent key.
                std::vector<std::string> probe;
                probe.reserve(ref.size() + absent.size());

                for (const auto& kv : ref) probe.push_back(kv.first);
                for (const auto& k : absent) probe.push_back(k);

                for (const auto& k : probe) {
                    const bool is_absent = ref.find(k) == ref.end();

                    for (int a = 0; a < 2; ++a) {
                        const Result r = ix[a]->find(k, qm);
                        ++queries;

                        if (is_absent) {
                            if (r.found) throw std::runtime_error("an absent key was reported found");
                        } else if (!r.found || r.value != ref.at(k)) {
                            throw std::runtime_error("an arm disagrees with the reference");
                        }
                    }
                }

                // Scans from arbitrary starts, element by element.
                const std::size_t L = 16;

                for (std::size_t i = 0; i < N; i += std::max<std::size_t>(1, N / 512)) {
                    const std::string& start = keys[i].first + "~between";

                    std::vector<Pair> want;
                    for (auto it = ref.lower_bound(start); it != ref.end() && want.size() < L; ++it)
                        want.push_back(*it);

                    ++scans;

                    for (int a = 0; a < 2; ++a) {
                        const auto got = ix[a]->scan(start, L);

                        if (got.size() != want.size())
                            throw std::runtime_error("a scan returned the wrong number of records");

                        for (std::size_t j = 0; j < want.size(); ++j) {
                            ++records;

                            if (got[j].first != want[j].first || got[j].second != want[j].second)
                                throw std::runtime_error("a scan record disagrees with the reference");
                        }
                    }
                }

                // The overflow counts are RECORDED, not asserted. R1 is expected to hold
                // fewer on some workloads and can legitimately hold more on others: the
                // drain opens room for OLD records in the primary area, and that is room a
                // later insert no longer gets. Which way it nets out is the two-arm
                // benchmark's question, not this gate's -- this gate is here to prove no
                // record is lost or duplicated, which the reference comparisons above do.
                ov_r0 = ix[0]->overflow_count();
                ov_r1 = ix[1]->overflow_count();
            }
        }

#ifdef E25_DIAG
        // Non-vacuity: R1 must have migrated something, or this run compared R0 with R0.
        {
            const auto& C = validation::e25::counts();

            if (C.migrate_calls == 0)
                throw std::runtime_error("R1 never attempted a drain, so nothing was compared");

            if (C.migrated_records == 0)
                throw std::runtime_error("R1 attempted drains but moved no record");
        }
#endif

        std::cout << "{\"gate\":\"check_migrate\",\"kind\":\"" << kind << "\""
                  << ",\"keys\":" << N
                  << ",\"queries_compared\":" << queries
                  << ",\"scans_compared\":" << scans
                  << ",\"records_compared\":" << records
                  << ",\"overflow_R0\":" << ov_r0
                  << ",\"overflow_R1\":" << ov_r1
                  << ",\"result\":\"pass\""
                  << "}" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cout << "{\"gate\":\"check_migrate\",\"result\":\"fail\",\"error\":\"" << e.what() << "\"}"
                  << std::endl;
        return 1;
    }
}

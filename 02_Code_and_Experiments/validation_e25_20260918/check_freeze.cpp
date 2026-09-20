// Freeze confirmation: does the DEFAULT entry point actually use the chosen set?
//
// Three configurations, and what each one is for:
//
//   OLD     the pre-E25 baseline, explicit (all five switches off). Measures the
//           whole combination's change.
//   CHOSEN  the combination this project selected, explicit (all five on). Ties the
//           earlier rounds' experiments to the shipped artifact.
//   SHIPPED a Config that leaves every switch AT ITS DEFAULT and sets only the
//           frozen R1 fields. This is what a caller gets today.
//
// The assertions that matter:
//   * SHIPPED's switches == CHOSEN's switches. If a default were ever edited back,
//     this fails instead of the artifact quietly reverting.
//   * SHIPPED and CHOSEN produce IDENTICAL content and IDENTICAL structural counters
//     -- they are the same configuration, so anything else would be a bug.
//   * OLD and SHIPPED agree on CONTENT (every key, every value) but are ALLOWED to
//     differ on layout: C2 changes which compactions run, which is the point.
//
// A single confirmation run checks configuration and function. It does NOT by itself
// establish performance stability, and nothing here is presented as a timing.

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

struct Op {
    int kind;
    std::string key;
    std::uint64_t value;
};

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_freeze KIND N [TRANCOCSV] [RUNTAG]");
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

        // ---- OLD: the pre-E25 baseline, every switch explicit -------------------
        const Config oldcfg = e25sw::baseline_config(thr);

        // ---- CHOSEN: the same, with the five switches turned on explicitly ------
        Config chosen = oldcfg;
        chosen.write_fast_locate = true;
        chosen.reuse_locate_ctx = true;
        chosen.compact_reuse_tag = true;
        chosen.compact_needs_garbage = true;
        chosen.scan_reserve_result = true;
        // E31: the candidate reclaim is NOT part of the frozen set. Stated here
        // explicitly rather than inherited from baseline_config, so this file says on
        // its own face what the shipped combination is -- and so that a future change
        // to baseline_config's defaults cannot quietly move this gate's CHOSEN arm.
        // A flip of the Config default is caught either way: switches() prints this
        // switch, and the SHIPPED==CHOSEN assertion below compares that string.
        chosen.subblock_reclaim = false;

        // ---- SHIPPED: a Config that leaves the switches at their DEFAULTS ------
        // Only the frozen R1 fields are set; the five switches are whatever the
        // header currently says. This is the entry point a caller actually gets.
        Config shipped;
        shipped.block_bytes = 1024;
        shipped.slots = 16;
        shipped.model_kind = ModelKind::Context16;
        shipped.multiblock = true;
        shipped.reuse_group_ids = true;
        shipped.ov_threshold = thr;

        std::cerr << "OLD     [" << e25sw::switches(oldcfg) << "]\n"
                  << "CHOSEN  [" << e25sw::switches(chosen) << "]\n"
                  << "SHIPPED [" << e25sw::switches(shipped) << "]" << std::endl;

        // The freeze assertion. A default edited back fails here, loudly.
        if (e25sw::switches(shipped) != e25sw::switches(chosen))
            throw std::runtime_error("the DEFAULT configuration is not the chosen combination");
        e25sw::require_only_switches_differ(
            oldcfg, chosen,
            {"write_fast_locate", "reuse_locate_ctx", "compact_reuse_tag",
             "compact_needs_garbage", "scan_reserve_result"});

        Pipeline plan;
        {
            Index probe(chosen);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        const std::size_t quarter = N / 4;
        std::vector<Op> upd, ins, rep;
        {
            std::vector<std::size_t> idx(N);
            for (std::size_t i = 0; i < N; ++i) idx[i] = i;
            std::mt19937_64 rng(2026092002);
            std::shuffle(idx.begin(), idx.end(), rng);
            for (std::size_t j = 0; j < quarter; ++j)
                upd.push_back({0, keys[idx[j]].first, 0x5A5AULL + j});
        }
        for (std::size_t j = 0; j < quarter; ++j)
            ins.push_back({1, keys[(j * 7) % N].first + "~new" + std::to_string(j), 0x1000ULL + j});
        for (std::size_t j = 0; j < quarter; ++j) {
            rep.push_back({2, keys[(j * 3) % N].first, 0});
            rep.push_back({1, keys[(j * 3) % N].first + "~rep" + std::to_string(j), 0x2000ULL + j});
        }

        auto mk = [&](const Config& cfg) {
            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            return s;
        };

        std::uint64_t compared = 0;
        auto run_case = [&](const char* name, const std::vector<Op>& ops) {
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;
            for (const auto& o : ops) {
                if (o.kind == 0) { auto it = ref.find(o.key); if (it == ref.end()) throw std::runtime_error("update absent"); it->second = o.value; }
                else if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert present"); ref[o.key] = o.value; }
                else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase absent"); }
            }
            auto a = mk(oldcfg);
            auto b = mk(chosen);
            auto d = mk(shipped);
            for (const auto& o : ops) {
                if (o.kind == 0) { a->update(o.key, o.value); b->update(o.key, o.value); d->update(o.key, o.value); }
                else if (o.kind == 1) { a->insert(o.key, o.value); b->insert(o.key, o.value); d->insert(o.key, o.value); }
                else { a->erase(o.key); b->erase(o.key); d->erase(o.key); }
            }

            const auto ca = a->scan(std::string_view{}, a->size());
            const auto cb = b->scan(std::string_view{}, b->size());
            const auto cd = d->scan(std::string_view{}, d->size());
            for (const auto* v : {&ca, &cb, &cd})
                if (v->size() != ref.size())
                    throw std::runtime_error(std::string(name) + ": size != reference");

            // SHIPPED must be indistinguishable from CHOSEN -- same configuration.
            for (std::size_t i = 0; i < cb.size(); ++i) {
                ++compared;
                if (cb[i].first != cd[i].first || cb[i].second != cd[i].second)
                    throw std::runtime_error(std::string(name) + ": SHIPPED differs from CHOSEN");
            }
            if (b->overflow_count() != d->overflow_count() ||
                b->compaction_count() != d->compaction_count() ||
                b->memory() != d->memory())
                throw std::runtime_error(std::string(name) + ": SHIPPED structure differs from CHOSEN");

            // OLD may have a different LAYOUT (C2 changes which compactions run) but
            // must hold the same records.
            for (std::size_t i = 0; i < ca.size(); ++i) {
                ++compared;
                if (ca[i].first != cb[i].first || ca[i].second != cb[i].second)
                    throw std::runtime_error(std::string(name) + ": OLD/CHOSEN content differs");
                const auto it = ref.find(ca[i].first);
                if (it == ref.end() || it->second != ca[i].second)
                    throw std::runtime_error(std::string(name) + ": disagrees with reference");
            }

            std::cout << "{\"case\":\"" << name << "\",\"keys\":" << N
                      << ",\"live\":" << ref.size()
                      << ",\"old_overflow\":" << a->overflow_count()
                      << ",\"chosen_overflow\":" << b->overflow_count()
                      << ",\"old_compactions\":" << a->compaction_count()
                      << ",\"chosen_compactions\":" << b->compaction_count()
                      << ",\"old_memory\":" << a->memory()
                      << ",\"chosen_memory\":" << b->memory()
                      << ",\"cfg_shipped\":\"" << e25sw::switches(shipped) << "\""
                      << "}" << std::endl;
        };

        run_case("update_only", upd);
        run_case("insert_only", ins);
        run_case("replace", rep);

        std::cerr << kind << " freeze confirmation done, " << compared
                  << " element comparisons" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

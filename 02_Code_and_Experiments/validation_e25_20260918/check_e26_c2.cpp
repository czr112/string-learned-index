// E26 C2 gate: skipping a compaction must not change WHAT the index holds.
//
// C2 is NOT an ablation. C1 only does less work inside the same operations; C2
// changes which operations run, so the resulting LAYOUT may legitimately differ from
// C0's -- a record that would have been compacted into the primary can instead go to
// overflow. That is the whole point of the change, and it is why the argument "the
// skipped compaction could not have freed enough bytes" has to be checked rather
// than assumed.
//
// What must NOT differ is the CONTENT: every key present with the same value, no key
// missing, no key invented. So this gate compares C0 and C2 ELEMENT BY ELEMENT
// against the reference map, and separately REPORTS (does not fail on) the structural
// differences -- overflow count and compaction count -- because those are the
// expected consequence.
//
// C2 minus C0 on the two counters is also the cost side of the trade: if C2 pushes
// many more records to overflow, that has to be weighed against the time it saves.

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
    int kind;  // 0 update, 1 insert, 2 erase
    std::string key;
    std::uint64_t value;
};

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_e26_c2 KIND N [TRANCOCSV] [RUNTAG]");
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

        // The E25 write-path decision is FIXED for this gate, and so is the scan
        // switch; only the compaction policy varies. Baseline from the shared helper,
        // not from Config defaults.
        Config c = e25sw::baseline_config(thr);
        c.write_fast_locate = true;      // fixed: the E25 decision
        c.reuse_locate_ctx = true;       // fixed
        Config c0 = c;                   // existing compaction
        Config c2 = c;
        c2.compact_reuse_tag = true;
        c2.compact_needs_garbage = true; // the C2 trigger gate
        e25sw::require_only_switches_differ(c0, c2,
            {"compact_reuse_tag", "compact_needs_garbage"});
        std::cerr << "E26 arms C0[" << e25sw::switches(c0) << "] C2["
                  << e25sw::switches(c2) << "]" << std::endl;

        Pipeline plan;
        {
            Index probe(c);
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
            std::mt19937_64 rng(2026091906);
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
        auto check_case = [&](const char* name, const std::vector<Op>& ops) {
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;
            for (const auto& o : ops) {
                if (o.kind == 0) { auto it = ref.find(o.key); if (it == ref.end()) throw std::runtime_error("update target absent"); it->second = o.value; }
                else if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert target present"); ref[o.key] = o.value; }
                else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase target absent"); }
            }

            auto a = mk(c0);
            auto b = mk(c2);
            for (const auto& o : ops) {
                if (o.kind == 0) { a->update(o.key, o.value); b->update(o.key, o.value); }
                else if (o.kind == 1) { a->insert(o.key, o.value); b->insert(o.key, o.value); }
                else { a->erase(o.key); b->erase(o.key); }
            }

            const auto ca = a->scan(std::string_view{}, a->size());
            const auto cb = b->scan(std::string_view{}, b->size());
            if (ca.size() != ref.size())
                throw std::runtime_error(std::string(name) + ": C0 size != reference");
            if (cb.size() != ref.size())
                throw std::runtime_error(std::string(name) + ": C2 size != reference");
            for (std::size_t i = 0; i < ca.size(); ++i) {
                ++compared;
                const auto it = ref.find(ca[i].first);
                if (it == ref.end() || it->second != ca[i].second)
                    throw std::runtime_error(std::string(name) + ": C0 content wrong at " + ca[i].first);
            }
            for (std::size_t i = 0; i < cb.size(); ++i) {
                ++compared;
                const auto it = ref.find(cb[i].first);
                if (it == ref.end() || it->second != cb[i].second)
                    throw std::runtime_error(std::string(name) + ": C2 content wrong at " + cb[i].first);
            }
            // Key ORDER must be the same sequence on both -- C2 may move a record
            // between primary and overflow, but not change what the ordered scan
            // yields.
            for (std::size_t i = 0; i < ca.size(); ++i)
                if (ca[i].first != cb[i].first)
                    throw std::runtime_error(std::string(name) + ": C0/C2 order differs at " +
                                             std::to_string(i));

            // Space is part of the C2 trade: C2 lets more records stay in overflow,
            // so its cost is not only the skipped compactions. Reported here so the
            // decision has the cost side, not just the time saved.
            const auto ma = a->memory_breakdown();
            const auto mb2 = b->memory_breakdown();
            std::cout << "{\"case\":\"" << name << "\",\"keys\":" << N
                      << ",\"live\":" << ref.size()
                      << ",\"c0_overflow\":" << a->overflow_count()
                      << ",\"c2_overflow\":" << b->overflow_count()
                      << ",\"c0_compactions\":" << a->compaction_count()
                      << ",\"c2_compactions\":" << b->compaction_count()
                      << ",\"c0_memory\":" << a->memory()
                      << ",\"c2_memory\":" << b->memory()
                      << ",\"c0_overflow_bytes\":" << ma.overflow
                      << ",\"c2_overflow_bytes\":" << mb2.overflow
                      << ",\"c0_blocks\":" << ma.blocks
                      << ",\"c2_blocks\":" << mb2.blocks
                      << "}" << std::endl;
        };

        check_case("update_only", upd);
        check_case("insert_only", ins);
        check_case("replace", rep);

        std::cerr << kind << " C2 gate done, " << compared << " element comparisons" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

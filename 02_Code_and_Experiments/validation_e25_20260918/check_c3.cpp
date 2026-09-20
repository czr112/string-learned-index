// E29 C3 gate: relaxing the compaction trigger must not change WHAT the index holds.
//
// C3 changes which compactions run, so the resulting LAYOUT may differ from the
// frozen version's -- more records reclaimed into the primary is the whole point.
// What must NOT differ is the CONTENT: every key present with the same value, in the
// same key order, no key invented or lost.
//
// The comparison runs on the long-replacement workload, because that is where C3 is
// supposed to act. Both arms use the frozen configuration plus exactly one switch.
//
// It also asserts the budget guard: compact() throws if a group cannot hold its own
// live records, so a run that reached any compaction at all without throwing is
// evidence the guard held.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
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
        if (argc < 3) throw std::invalid_argument("check_c3 KIND N [TRANCOCSV] [RUNTAG] [MODE]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const std::string mode = argc > 5 ? argv[5] : "same";
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

        Config frozen = e25sw::baseline_config(thr);
        frozen.write_fast_locate = true;
        frozen.reuse_locate_ctx = true;
        frozen.scan_reserve_result = true;
        frozen.compact_reuse_tag = true;
        frozen.compact_needs_garbage = true;
        Config c3 = frozen;
        c3.compact_repack_budget = true;
        e25sw::require_only_switches_differ(frozen, c3, {"compact_repack_budget"});
        std::cerr << "arms frozen[" << e25sw::switches(frozen) << "] C3["
                  << e25sw::switches(c3) << "]" << std::endl;

        Pipeline plan;
        {
            Index probe(frozen);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        std::deque<std::string> live;
        std::unordered_set<std::string> live_set;
        Reference ref;
        for (const auto& kv : keys) { live.push_back(kv.first); ref[kv.first] = kv.second; live_set.insert(kv.first); }

        auto a = std::make_unique<Index>(frozen);
        auto b = std::make_unique<Index>(c3);
        a->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
        b->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        std::uint64_t serial = 0, compared = 0;
        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
        std::size_t done = 0;
        for (double frac : CPS) {
            const std::size_t target = static_cast<std::size_t>(double(N) * frac);
            while (done < target) {
                const std::string victim = live.front();
                live.pop_front();
                live_set.erase(victim);
                std::string base = victim;
                const auto pos = base.find("~r");
                if (pos != std::string::npos) base.resize(pos);
                std::string fresh;
                if (mode == "grow") {
                    fresh = base + "~r" + std::to_string(serial);
                } else {
                    fresh = base;
                    bool ok = false;
                    for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                        fresh.back() = static_cast<char>('a' + ((serial + t) % 26));
                        if (fresh != victim && ref.find(fresh) == ref.end() &&
                            live_set.find(fresh) == live_set.end()) { ok = true; break; }
                    }
                    if (!ok) throw std::runtime_error("alphabet exhausted");
                }
                const std::uint64_t val = 0x7000ULL + serial;
                ++serial;
                ref.erase(victim); ref[fresh] = val;
                a->erase(victim); a->insert(fresh, val);
                b->erase(victim); b->insert(fresh, val);
                live.push_back(fresh); live_set.insert(fresh);
                ++done;
            }

            // Full content comparison: element by element, against the reference AND
            // against each other. Agreeing on a wrong answer is not a pass.
            const auto ca = a->scan(std::string_view{}, a->size());
            const auto cb = b->scan(std::string_view{}, b->size());
            if (ca.size() != ref.size() || cb.size() != ref.size())
                throw std::runtime_error("size != reference");
            for (std::size_t i = 0; i < ca.size(); ++i) {
                ++compared;
                if (ca[i].first != cb[i].first || ca[i].second != cb[i].second)
                    throw std::runtime_error("C3 content or order differs at " + std::to_string(i));
                const auto it = ref.find(ca[i].first);
                if (it == ref.end() || it->second != ca[i].second)
                    throw std::runtime_error("disagrees with reference: " + ca[i].first);
            }
            std::cout << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\""
                      << ",\"frac\":" << frac << ",\"live\":" << ref.size()
                      << ",\"frozen_overflow\":" << a->overflow_count()
                      << ",\"c3_overflow\":" << b->overflow_count()
                      << ",\"frozen_compactions\":" << a->compaction_count()
                      << ",\"c3_compactions\":" << b->compaction_count()
                      << ",\"frozen_memory\":" << a->memory()
                      << ",\"c3_memory\":" << b->memory()
                      << ",\"records_compared\":" << compared << "}" << std::endl;
            std::cerr << kind << " " << mode << " frac=" << frac << " ok" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

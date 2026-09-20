// E27 gate: reserving the result string must not change a single returned record.
//
// The change is a pure materialisation change -- same records, same order, same
// values, same ownership. So this gate builds two indexes from the same plan, puts
// the same operation sequence through both, and compares the FULL scan output
// element by element at every protocol length, including the starts that fall
// between two keys and the scans that return fewer than n records.
//
// It also covers the case the scan path is most fragile about: a group whose middle
// sub-block was emptied (E9's defect). replace25 produces those, so the comparison
// runs on that state too.

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
        if (argc < 3) throw std::invalid_argument("check_scan KIND N [TRANCOCSV] [RUNTAG]");
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

        // Write path and compaction are FIXED at the chosen combination; only the
        // scan materialisation varies. Baseline from the shared helper.
        Config c = e25sw::baseline_config(thr);
        c.write_fast_locate = true;
        c.reuse_locate_ctx = true;
        c.compact_reuse_tag = true;
        c.compact_needs_garbage = true;
        Config c0 = c;                                  // two allocations per record
        Config c1 = c; c1.scan_reserve_result = true;   // one allocation per record
        e25sw::require_only_switches_differ(c0, c1, {"scan_reserve_result"});
        std::cerr << "E27 arms S0[" << e25sw::switches(c0) << "] S1["
                  << e25sw::switches(c1) << "]" << std::endl;

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
        std::vector<Op> ins, rep;
        for (std::size_t j = 0; j < quarter; ++j)
            ins.push_back({1, keys[(j * 7) % N].first + "~new" + std::to_string(j), 0x1000ULL + j});
        for (std::size_t j = 0; j < quarter; ++j) {
            rep.push_back({2, keys[(j * 3) % N].first, 0});
            rep.push_back({1, keys[(j * 3) % N].first + "~rep" + std::to_string(j), 0x2000ULL + j});
        }

        std::vector<std::string> starts;
        {
            std::mt19937_64 rng(2026091801);
            const std::size_t n = std::min<std::size_t>(4096, N);
            for (std::size_t i = 0; i < n; ++i) {
                const std::string& k = keys[rng() % N].first;
                starts.push_back((i % 5 == 0) ? k + "~between" : k);
            }
            starts.push_back("");                        // the very beginning
            starts.push_back(std::string());
        }
        const std::size_t NS[3] = {16, 128, 1024};

        std::uint64_t compared = 0, scans_run = 0, short_scans = 0;
        auto run_case = [&](const char* name, const std::vector<Op>& ops) {
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;
            for (const auto& o : ops) {
                if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert present"); ref[o.key] = o.value; }
                else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase absent"); }
            }
            auto a = std::make_unique<Index>(c0);
            auto b = std::make_unique<Index>(c1);
            a->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            b->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            for (const auto& o : ops) {
                if (o.kind == 1) { a->insert(o.key, o.value); b->insert(o.key, o.value); }
                else { a->erase(o.key); b->erase(o.key); }
            }

            for (std::size_t ni = 0; ni < 3; ++ni) {
                const std::size_t n = NS[ni];
                for (const auto& k : starts) {
                    ++scans_run;
                    const auto ga = a->scan(k, n);
                    const auto gb = b->scan(k, n);
                    if (ga.size() != gb.size())
                        throw std::runtime_error(std::string(name) + ": returned length differs");
                    if (ga.size() < n) ++short_scans;
                    for (std::size_t i = 0; i < ga.size(); ++i) {
                        ++compared;
                        if (ga[i].first != gb[i].first || ga[i].second != gb[i].second)
                            throw std::runtime_error(std::string(name) + ": returned record differs");
                    }
                    // And against the reference, so both are checked independently of
                    // each other -- agreeing on a wrong answer is not a pass.
                    std::size_t idx = 0;
                    for (auto it = ref.lower_bound(k); it != ref.end() && idx < ga.size(); ++it, ++idx) {
                        if (it->first != ga[idx].first || it->second != ga[idx].second)
                            throw std::runtime_error(std::string(name) + ": disagrees with reference");
                    }
                    if (idx != ga.size())
                        throw std::runtime_error(std::string(name) + ": reference length differs");
                }
            }
            std::cout << "{\"case\":\"" << name << "\",\"keys\":" << N
                      << ",\"live\":" << ref.size()
                      << ",\"scans\":" << scans_run
                      << ",\"short_scans\":" << short_scans
                      << ",\"records_compared\":" << compared << "}" << std::endl;
        };

        run_case("initial", {});
        run_case("insert25", ins);
        run_case("replace25", rep);

        std::cerr << kind << " scan gate done" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

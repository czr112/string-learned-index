// E30 step 2: measure the per-record cost of an in-place sub-block tightening.
//
// The break-even threshold is 70 ns/record; the frozen arm's group compaction spends
// 163 ns/record. Whether the in-place pass can reach 70 ns decides whether the whole
// sub-block-reclaim line is alive, and nothing measured so far answers it.
//
// The probe runs on a SCRATCH COPY of the real rank-selected sub-block: real data, real
// record lengths, and the index is never touched -- so it can run inside the workload
// without changing what it measures. Diagnostics are separate from any timing of the
// workload itself, which is not what this program reports.
//
// `pass_ns` is the number the decision uses. `copy_ns` is the scratch memcpy, an
// artefact of the probe that a real in-place implementation would not pay.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef __linux__
#include <sched.h>
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

int main(int argc, char** argv) {
    try {
#ifdef __linux__
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 3) throw std::invalid_argument("bench_repack KIND N [TRANCOCSV] [RUNTAG] [MODE]");
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

        Config c = e25sw::baseline_config(thr);
        c.write_fast_locate = true;
        c.reuse_locate_ctx = true;
        c.scan_reserve_result = true;
        c.compact_reuse_tag = true;
        c.compact_needs_garbage = true;
        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        Pipeline plan;
        {
            Index probe(c);
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

        Index s(c);
        s.bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        std::ofstream out("results/main/repack_cost.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        std::uint64_t serial = 0, probes = 0, records_sum = 0, moved_sum = 0;
        std::uint64_t pass_ns_sum = 0, copy_ns_sum = 0, bad = 0;
        std::size_t done = 0;

        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
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
                s.erase(victim); s.insert(fresh, val);

                if (s.find(fresh, qmode).source == 2) {
                    // The rank-selected candidate of the sub-block that just refused.
                    std::uint64_t cns = 0, pns = 0, mv = 0;
                    std::size_t recs = 0;
                    bool ok = false;
                    s.e30_probe(fresh, cns, pns, recs, mv, ok);
                    ++probes;
                    records_sum += recs;
                    moved_sum += mv;
                    pass_ns_sum += pns;
                    copy_ns_sum += cns;
                    if (!ok) ++bad;
                }
                live.push_back(fresh);
                live_set.insert(fresh);
                ++done;
            }

            out << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\",\"run_tag\":\"" << run_tag << "\""
                << ",\"frac\":" << frac << ",\"probes\":" << probes
                << ",\"records\":" << records_sum << ",\"moved_bytes\":" << moved_sum
                << ",\"pass_ns\":" << pass_ns_sum << ",\"copy_ns\":" << copy_ns_sum
                << ",\"pass_verify_failures\":" << bad
                << ",\"ns_per_record\":" << (records_sum ? double(pass_ns_sum) / double(records_sum) : 0.0)
                << ",\"ns_per_probe\":" << (probes ? double(pass_ns_sum) / double(probes) : 0.0)
                << ",\"ns_per_byte\":" << (moved_sum ? double(pass_ns_sum) / double(moved_sum) : 0.0)
                << ",\"recs_per_probe\":" << (probes ? double(records_sum) / double(probes) : 0.0)
                << "}" << std::endl;
            std::cerr << kind << " " << mode << " frac=" << frac << " probes " << probes
                      << " ns/rec " << (records_sum ? double(pass_ns_sum) / double(records_sum) : 0.0)
                      << " bad " << bad << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

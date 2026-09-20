// E30: how much of the frozen arm's replacement workload is spent inside compact()?
//
// Direct measurement. The two marginals disagreed by 5.5x and neither is a
// per-compaction cost, so the fraction is measured with a clock around the call
// rather than inferred from the difference between two configurations.
//
// CALIBRATION IS PART OF THE RESULT: this source builds twice, with and without
// -DE30_TIME. The totals must agree; a timer that moves the total is not an
// instrument, it is a participant. `bench_w` (clean) and `bench_w_t` (timed) exist for
// exactly that comparison.
//
// Frozen configuration. Same-mode replacement, active count constant.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <chrono>
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

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

using namespace validation;
using namespace validation::e9alloc;

using Clock = std::chrono::steady_clock;
using Reference = std::map<std::string, std::uint64_t, Less>;

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

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
        if (argc < 3) throw std::invalid_argument("bench_w KIND N [TRANCOCSV] [RUNTAG] [MODE]");
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
        c.compact_needs_garbage = true;   // the frozen version

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

#ifdef E30_TIME
        validation::e30::timers().reset();
#endif
        std::uint64_t serial = 0;
        std::size_t done = 0;
        const std::size_t target = static_cast<std::size_t>(double(N) * 2.0);   // 200%
        std::vector<std::string> seg;

        const auto T0 = Clock::now();
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

            ref.erase(victim);
            ref[fresh] = val;
            s.erase(victim);
            s.insert(fresh, val);

            live.push_back(fresh);
            live_set.insert(fresh);
            ++done;
        }
        const double total_ms = ms_between(T0, Clock::now());
        if (s.size() != ref.size()) throw std::runtime_error("live count drifted");

        std::ofstream out("results/main/w_diag.jsonl", std::ios::app);
        out << std::setprecision(10);
        out << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\",\"run_tag\":\"" << run_tag << "\""
            << ",\"cycles\":" << done << ",\"total_ms\":" << total_ms
            << ",\"overflow\":" << s.overflow_count()
            << ",\"compactions\":" << s.compaction_count();
#ifdef E30_TIME
        {
            const auto& t = validation::e30::timers();
            out << ",\"e30_compact_ms\":" << t.compact_ns / 1e6
                << ",\"e30_compact_calls\":" << t.compact_calls
                << ",\"e30_collect_ms\":" << t.collect_ns / 1e6
                << ",\"e30_erase_ms\":" << t.erase_ns / 1e6
                << ",\"e30_erase_calls\":" << t.erase_calls
                << ",\"e30_insert_ms\":" << t.insert_ns / 1e6
                << ",\"e30_insert_calls\":" << t.insert_calls
                << ",\"e30_us_per_compact\":" << (t.compact_calls ? (t.compact_ns / 1e3) / t.compact_calls : 0.0)
                << ",\"e30_compact_share\":" << (total_ms > 0 ? (t.compact_ns / 1e6) / total_ms : 0.0);
        }
#endif
        out << "}" << std::endl;
        std::cerr << kind << " " << mode << " total " << total_ms << " ms" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

// E27 timing: the scan materialisation change, against the current version and the
// plain sorted array.
//
// CLEAN TIMING BUILD. Three arms in one process, rotated per repetition:
//   S0  current materialisation (prefix_ + std::string(suffix))
//   S1  one-reservation materialisation
//   ARR a global sorted array with lower_bound, copying n records -- the baseline
//       E23 used. It returns the SAME form (complete key/value pairs copied into a
//       vector), so the comparison is like for like.
//
// Only ONE thing changed between S0 and S1, and it is inside the timed span on
// purpose: the point of this round is whether the materialisation cost is real.
//
// Scan semantics are E23's and are read from the protocol: from the first record with
// key >= start, return AT MOST n records in key order, as complete key/value pairs.
// States and lengths are E23's. Starts are four fifths real keys and one fifth
// synthetic keys that exist in no bucket.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
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
static volatile std::uint64_t sink = 0;
static std::uint64_t checksum(const std::vector<Pair>& v) {
    std::uint64_t s = 0;
    for (const auto& kv : v) s += kv.first.size() * 131 + kv.second + 0x9e3779b97f4a7c15ULL;
    return s;
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

struct Op {
    int kind;
    std::string key;
    std::uint64_t value;
};

int main(int argc, char** argv) {
    try {
#ifdef __linux__
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 4) throw std::invalid_argument("bench_scan KIND N REPS [TRANCOCSV] [RUNTAG]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argc > 4 ? argv[4] : "";
        const std::string run_tag = argc > 5 ? argv[5] : "";
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

        // Pinned through the shared helper. This matters MORE here than anywhere
        // else: S0 and S1 differ only by `scan_reserve_result`, so if the arms
        // followed a flipped default instead of setting it explicitly, both would
        // become the same configuration and the comparison would silently vanish.
        Config c = e25sw::baseline_config(thr);
        c.write_fast_locate = true;
        c.reuse_locate_ctx = true;
        c.compact_reuse_tag = true;
        c.compact_needs_garbage = true;
        Config cfg[2] = {c, c};
        cfg[1].scan_reserve_result = true;   // S1
        const char* names[3] = {"S0", "S1", "ARR"};

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
        }
        const std::size_t NS[3] = {16, 128, 1024};

        std::ofstream out("results/main/e27_scan_time.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        struct State { const char* name; const std::vector<Op>* ops; };
        const State states[3] = {{"initial", nullptr}, {"insert25", &ins}, {"replace25", &rep}};

        for (const auto& st : states) {
            static const std::vector<Op> none;
            const std::vector<Op>& ops = st.ops ? *st.ops : none;

            for (int r = 0; r < reps; ++r) {
                // The index is rebuilt per repetition so every arm starts from the
                // same state; arms differ only in how they materialise a result.
                Reference ref;
                for (const auto& kv : keys) ref[kv.first] = kv.second;
                for (const auto& o : ops) {
                    if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert present"); ref[o.key] = o.value; }
                    else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase absent"); }
                }
                auto s = std::make_unique<Index>(c);
                s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
                for (const auto& o : ops) {
                    if (o.kind == 1) s->insert(o.key, o.value);
                    else s->erase(o.key);
                }
                auto s1 = std::make_unique<Index>(cfg[1]);
                s1->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
                for (const auto& o : ops) {
                    if (o.kind == 1) s1->insert(o.key, o.value);
                    else s1->erase(o.key);
                }
                std::vector<Pair> flat;
                flat.reserve(ref.size());
                for (const auto& kv : ref) flat.push_back(kv);
                // Verify inside the reference ordering before it is timed: the baseline
                // must be the same key order the prototype returns.
                std::sort(flat.begin(), flat.end(), [](const Pair& a, const Pair& b) {
                    return compare(a.first, b.first) < 0;
                });

                for (std::size_t ni = 0; ni < 3; ++ni) {
                    const std::size_t n = NS[ni];
                    int order[3] = {r % 3, (r + 1) % 3, (r + 2) % 3};
                    for (int oi = 0; oi < 3; ++oi) {
                        const int arm = order[oi];
                        const auto a = Clock::now();
                        std::uint64_t acc = 0;
                        if (arm == 2) {
                            for (const auto& k : starts) {
                                std::vector<Pair> got;
                                got.reserve(std::min(n, flat.size()));
                                auto it = std::lower_bound(flat.begin(), flat.end(), k,
                                    [](const Pair& x, const std::string& b) {
                                        return compare(x.first, b) < 0;
                                    });
                                for (; it != flat.end() && got.size() < n; ++it) got.push_back(*it);
                                acc += checksum(got);
                            }
                        } else {
                            const Index& idx = (arm == 0) ? *s : *s1;
                            for (const auto& k : starts) acc += checksum(idx.scan(k, n));
                        }
                        const double dt = ms_between(a, Clock::now());
                        sink += acc;
                        out << "{\"kind\":\"" << kind << "\",\"state\":\"" << st.name
                            << "\",\"n\":" << n << ",\"rep\":" << r
                            << ",\"arm\":\"" << names[arm] << "\",\"run_tag\":\"" << run_tag
                            << "\",\"scans\":" << starts.size() << ",\"ms\":" << dt
                            << "}" << std::endl;
                    }
                    std::cerr << kind << " " << st.name << " n=" << n << " rep" << r << " done" << std::endl;
                }
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

// A/B for the EqualDepthDir backend: the SAME workload, the SAME query sequences and the SAME
// structure, with Context16 in one arm and the equal-depth separator directory in the other.
//
// WHY THIS DEVICE AND NOT A BIGGER ONE. The entry round measured the mechanism on the entry's
// own query mode and found the primary probes per query at 1.00 for the directory against
// 11.2 / 16.6 / 2.5 for the reference -- and zero overflow records. Those are COUNTS, and the
// project's rule is that counts do not become speedups by themselves. This file is the paired
// measurement the plan's P3 asks for: two arms, one process, one repetition, order rotated.
//
// TWO QUERY PHASES (kept from the routing A/B, which learned this the hard way):
//   static       -- after bulk_load, over the initial key set, mixed hits and misses;
//   post-update  -- after the SAME replacement schedule to 200% in every arm, over the keys
//                   live at that moment. The first version of the routing bench timed only the
//                   second and every query was a miss by then.
//
// VERIFY FIRST, TIME SECOND: mismatches are counted outside the timed span, and the hit counts
// are recorded beside the checksums, because a checksum only compares the arms to each other
// and cannot notice a workload that went degenerate for all of them.
//
// Usage: bench_depth_dir KIND N REPS [TRANCOCSV] [RUNTAG] [OUTSTEM]

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
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;
using Clock = std::chrono::steady_clock;

namespace {

struct Arm {
    const char* name;
    ModelBackend backend;
};

const Arm ARMS[2] = {
    {"C16", ModelBackend::Context16},
    {"DDir", ModelBackend::EqualDepthDir},
};

constexpr int kArms = 2;

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
            .count());
}

bool ubyte_less(std::string_view a, std::string_view b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) {
                                            return static_cast<unsigned char>(x) <
                                                   static_cast<unsigned char>(y);
                                        });
}

struct Probe {
    std::string key;
    bool expect_hit = false;
    std::uint64_t expect_value = 0;
};

std::vector<Probe> make_probes(const std::vector<std::string>& live, std::size_t count,
                               std::uint64_t seed, int miss_percent,
                               const std::unordered_map<std::string, std::uint64_t>& values) {
    std::vector<Probe> out;
    if (live.empty()) return out;

    std::mt19937_64 rng(seed);
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (static_cast<int>(rng() % 100) < miss_percent) {
            std::string k = live[rng() % live.size()];
            k.push_back('~');
            k.push_back(static_cast<char>('a' + (rng() % 26)));
            out.push_back(Probe{k, false, 0});
        } else {
            const std::string& k = live[rng() % live.size()];
            const auto it = values.find(k);
            out.push_back(Probe{k, true, it == values.end() ? 0 : it->second});
        }
    }
    return out;
}

std::uint64_t hash_probes(const std::vector<Probe>& probes) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto& p : probes) {
        for (unsigned char ch : p.key) h = (h ^ ch) * 1099511628211ULL;
        h = (h ^ (p.expect_hit ? 1u : 0u)) * 1099511628211ULL;
    }
    return h;
}

struct Phase {
    std::uint64_t ns = 0, checksum = 0, probes = 0;
    std::uint64_t expected_hits = 0, observed_hits = 0, mismatches = 0;
};

Phase run_phase(const Index& index, const std::vector<Probe>& probes, const Mode& mode,
                int replay, bool verify_only) {
    Phase p;
    p.probes = probes.size();
    for (const auto& probe : probes)
        if (probe.expect_hit) ++p.expected_hits;

    for (const auto& probe : probes) {
        const Result r = index.find(probe.key, mode);
        if (r.found != probe.expect_hit) ++p.mismatches;
        else if (probe.expect_hit && r.value != probe.expect_value) ++p.mismatches;
        if (r.found) ++p.observed_hits;
    }
    if (verify_only) return p;

    const std::uint64_t t0 = now_ns();
    for (int rep = 0; rep < replay; ++rep) {
        for (const auto& probe : probes) {
            const Result r = index.find(probe.key, mode);
            p.checksum += r.found ? (r.value + 1) : 0x9E3779B97F4A7C15ULL;
        }
    }
    p.ns = now_ns() - t0;
    return p;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 4)
            throw std::invalid_argument("bench_depth_dir KIND N REPS [TRANCOCSV] [RUNTAG] [OUT]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argc > 4 ? argv[4] : "";
        const std::string run_tag = argc > 5 ? argv[5] : "";
        const std::string out_stem = argc > 6 ? argv[6] : "depth_ab";

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        std::uint32_t thr = 32;
        {
            const std::string frozen_rel =
                "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        Config cfg[kArms];
        for (int a = 0; a < kArms; ++a) {
            cfg[a] = e25sw::baseline_config(thr);
            e25sw::pin_frozen_switches(cfg[a]);
            cfg[a].model_backend = ARMS[a].backend;
        }
        // The arms differ in the backend and NOTHING else -- the backend is excluded from this
        // check on purpose, since it is the variable under test.
        {
            Config x = cfg[0], y = cfg[1];
            x.model_backend = y.model_backend = ModelBackend::Context16;
            e25sw::require_only_switches_differ(x, y, {});
        }

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        std::vector<std::string> initial;
        std::unordered_map<std::string, std::uint64_t> initial_values;
        initial.reserve(N);
        initial_values.reserve(N);
        for (const auto& kv : keys) {
            initial.push_back(kv.first);
            initial_values.emplace(kv.first, kv.second);
        }

        const auto static_probes = make_probes(initial, 20000, 20261011, 10, initial_values);
        const std::uint64_t static_hash = hash_probes(static_probes);

        constexpr int kReplay = 100;
        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        std::ofstream out(("results/main/" + out_stem + ".jsonl").c_str(), std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (int r = 0; r < reps; ++r) {
            const int order[kArms] = {r % kArms, (r + 1) % kArms};

            for (int oi = 0; oi < kArms; ++oi) {
                const int arm = order[oi];

                std::deque<std::string> live;
                std::unordered_set<std::string> live_set;
                std::unordered_map<std::string, std::uint64_t> live_values;
                live_values.reserve(N);
                for (const auto& kv : keys) {
                    live.push_back(kv.first);
                    live_set.insert(kv.first);
                    live_values.emplace(kv.first, kv.second);
                }

                const std::uint64_t t_build0 = now_ns();
                auto s = std::make_unique<Index>(cfg[arm]);
                s->bulk_load(keys);
                const std::uint64_t build_ns = now_ns() - t_build0;

                const Phase st = run_phase(*s, static_probes, qmode, kReplay, false);

                std::uint64_t maint_ns = 0;
                std::size_t done = 0;
                std::uint64_t serial = 0;
                for (double frac : CPS) {
                    const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                    if (target <= done) continue;

                    struct Move { std::string victim, fresh; std::uint64_t value; };
                    std::vector<Move> seg;
                    seg.reserve(target - done);
                    for (std::size_t i = done; i < target; ++i) {
                        const std::string victim = live.front();
                        live.pop_front();
                        live_set.erase(victim);
                        live_values.erase(victim);

                        std::string base = victim;
                        const auto pos = base.find("~r");
                        if (pos != std::string::npos) base.resize(pos);
                        std::string fresh = base + "~r" + std::to_string(serial++);
                        while (!live_set.insert(fresh).second) fresh += "x";

                        const std::uint64_t value = 0x3000ULL + serial;
                        live.push_back(fresh);
                        live_values.emplace(fresh, value);
                        seg.push_back({victim, std::move(fresh), value});
                    }

                    const std::uint64_t t0 = now_ns();
                    for (const auto& m : seg) s->erase(m.victim);
                    for (const auto& m : seg) s->insert(m.fresh, m.value);
                    maint_ns += now_ns() - t0;
                    done = target;
                }

                std::vector<std::string> live_vec(live.begin(), live.end());
                std::sort(live_vec.begin(), live_vec.end(), ubyte_less);
                const auto post_probes = make_probes(live_vec, 20000, 20261012, 10, live_values);
                const std::uint64_t post_hash = hash_probes(post_probes);
                const Phase post = run_phase(*s, post_probes, qmode, kReplay, false);

                const auto m = s->memory_breakdown();
                const auto rd = Region::depth_dir_stats();

                out << "{\"kind\":\"" << kind << "\",\"run_tag\":\"" << run_tag
                    << "\",\"arm\":\"" << ARMS[arm].name
                    << "\",\"backend\":\"" << e25sw::backend_name(ARMS[arm].backend)
                    << "\",\"rep\":" << r << ",\"position\":" << oi
                    << ",\"n\":" << N << ",\"thr\":" << thr << ",\"replay\":" << kReplay
                    << ",\"build_ns\":" << build_ns
                    << ",\"maint_ns\":" << maint_ns
                    << ",\"static_ns\":" << st.ns
                    << ",\"static_probes\":" << st.probes
                    << ",\"static_expected_hits\":" << st.expected_hits
                    << ",\"static_hits\":" << st.observed_hits
                    << ",\"static_mismatches\":" << st.mismatches
                    << ",\"static_checksum\":" << st.checksum
                    << ",\"static_probe_hash\":" << static_hash
                    << ",\"post_ns\":" << post.ns
                    << ",\"post_probes\":" << post.probes
                    << ",\"post_expected_hits\":" << post.expected_hits
                    << ",\"post_hits\":" << post.observed_hits
                    << ",\"post_mismatches\":" << post.mismatches
                    << ",\"post_checksum\":" << post.checksum
                    << ",\"post_probe_hash\":" << post_hash
                    << ",\"live_keys\":" << live.size()
                    << ",\"index_bytes\":"
                    << (m.blocks + m.model + m.prefix + m.overflow + m.tables + m.directory +
                        m.regions_self + m.fences + m.self)
                    << ",\"model_bytes\":" << m.model
                    << ",\"regions\":" << s->region_count()
                    << ",\"dir_thresholds\":" << rd.thresholds_total
                    << "}" << std::endl;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

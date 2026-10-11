// A/B for the compact region-routing candidate: the SAME index, the SAME workload and the
// SAME query sequences, with the switch off in one arm and on in the other.
//
// TWO QUERY PHASES, AND THE FIRST VERSION HAD NONE OF THE SECOND.
//
// The first version built its query set from the INITIAL keys and then timed it AFTER a
// replacement run to 200%. By the 100% checkpoint every initial key had been deleted, so
// every query was a miss: all 240 recorded checksums equalled 2,000,000 * 0x9E3779B97F4A7C15
// mod 2^64, the all-miss value. That measured the cost of a MISS after an update, and the
// report quoted it as a lookup gain without saying so.
//
// What made it hard to notice is that the checksum self-check PASSED. It compares the arms to
// each other, so it catches an arm that skipped work -- it cannot catch a workload that went
// degenerate for every arm at once. So this version records the ACTUAL HIT COUNT beside the
// checksum, and verifies every query against a reference table OUTSIDE the timed span; the
// checksum stays only as a secondary guard against a skipped lookup.
//
//   static phase       -- after bulk_load, over the initial key set. Mixed hits and misses.
//   post-update phase  -- after replacement to 200%, over the keys that are LIVE AT THAT
//                         MOMENT. This is the phase the first version was missing.
//
// Both phases keep hits and misses as separate reported counts, because a routing change can
// help one and not the other and a single blended number would hide which.
//
// FOUR ARMS. Three corpora crossed with two model backends, in one sweep so both backends see
// the same process, the same machine state and the same rotation. The arms rotate position
// within each repetition; the ratios are formed WITHIN one repetition by the analyzer.
//
// Usage:  bench_routing KIND N REPS [TRANCOCSV] [RUNTAG] [MODE] [OUTSTEM]

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
    bool routing;
};

const Arm ARMS[4] = {
    {"C16-off", ModelBackend::Context16, false},
    {"C16-on", ModelBackend::Context16, true},
    {"LITS-off", ModelBackend::LitsHpt, false},
    {"LITS-on", ModelBackend::LitsHpt, true},
};

constexpr int kArms = 4;

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
            .count());
}

// unsigned-byte order, the order the contract and the index both use
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

// A fixed query set over an arbitrary live-key population. Deterministic given the seed, so
// every arm produces the same set; the caller records a hash of it so the analyzer can check
// that all four arms really did query the same keys rather than assume it.
std::vector<Probe> make_probes(const std::vector<std::string>& live, std::size_t count,
                               std::uint64_t seed, int miss_percent,
                               const std::unordered_map<std::string, std::uint64_t>& values) {
    std::vector<Probe> out;

    if (live.empty()) return out;

    std::mt19937_64 rng(seed);
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        if (static_cast<int>(rng() % 100) < miss_percent) {
            // An absent key: a live key with a byte appended that keeps it distinct. It is
            // built from a live key so it lands in the same key-space neighbourhood, which is
            // the realistic miss rather than one that sorts off the end.
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
        for (unsigned char c : p.key) {
            h = (h ^ c) * 1099511628211ULL;
        }

        h = (h ^ (p.expect_hit ? 1u : 0u)) * 1099511628211ULL;
    }

    return h;
}

struct Phase {
    std::uint64_t ns = 0;
    std::uint64_t checksum = 0;
    std::uint64_t probes = 0;
    std::uint64_t expected_hits = 0;
    std::uint64_t observed_hits = 0;
    std::uint64_t mismatches = 0;
};

// VERIFY FIRST, TIME SECOND, and they are different passes on purpose: the verifying pass runs
// outside the timed span so a mismatch cannot slow the measurement, and a mismatch is counted
// rather than thrown so the run still produces a record saying what was wrong.
Phase run_phase(const Index& index, const std::vector<Probe>& probes, const Mode& mode,
                int replay, bool verify_only) {
    Phase p;
    p.probes = probes.size();

    for (const auto& probe : probes) {
        if (probe.expect_hit) ++p.expected_hits;
    }

    // Verification pass: outside every timed span.
    for (const auto& probe : probes) {
        const Result r = index.find(probe.key, mode);

        if (r.found != probe.expect_hit) {
            ++p.mismatches;
        } else if (probe.expect_hit && r.value != probe.expect_value) {
            ++p.mismatches;
        }

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
        if (argc < 4) throw std::invalid_argument("bench_routing KIND N REPS [CSV] [TAG] [MODE] [OUT]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argc > 4 ? argv[4] : "";
        const std::string run_tag = argc > 5 ? argv[5] : "";
        const std::string mode = argc > 6 ? argv[6] : "grow";
        const std::string out_stem = argc > 7 ? argv[7] : ("routing_" + mode);

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        const std::string frozen_rel =
            "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
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
            cfg[a].compact_region_routing = ARMS[a].routing;
        }

        for (int a = 0; a < kArms; ++a) {
            for (int b = a + 1; b < kArms; ++b) {
                Config x = cfg[a], y = cfg[b];
                x.model_backend = y.model_backend = ModelBackend::Context16;
                x.compact_region_routing = y.compact_region_routing = false;
                e25sw::require_only_switches_differ(x, y, {});
            }
        }

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        // The static probe set is over the initial keys, so it can be built once for all arms.
        std::vector<std::string> initial;
        initial.reserve(N);
        std::unordered_map<std::string, std::uint64_t> initial_values;
        initial_values.reserve(N);

        for (const auto& kv : keys) {
            initial.push_back(kv.first);
            initial_values.emplace(kv.first, kv.second);
        }

        const auto static_probes = make_probes(initial, 20000, 20261009, 10, initial_values);
        const std::uint64_t static_hash = hash_probes(static_probes);

        // The post-update probe set is over the keys live after the SAME replacement schedule
        // every arm runs. The schedule is deterministic and the arms share it, so the live set
        // is identical -- but that is an argument, so the probe hash is recorded per record and
        // the analyzer checks the four arms agree rather than assuming it.
        constexpr int kReplay = 100;
        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        std::ofstream out(("results/main/" + out_stem + ".jsonl").c_str(), std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (int r = 0; r < reps; ++r) {
            const int order[kArms] = {r % kArms, (r + 1) % kArms, (r + 2) % kArms,
                                      (r + 3) % kArms};

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

                // ---------------- phase 1: static ----------------
                const Phase st = run_phase(*s, static_probes, qmode, kReplay, false);

                // ---------------- maintenance ----------------
                std::size_t done = 0;
                std::uint64_t serial = 0;
                std::uint64_t maint_ns = 0;

                for (double frac : CPS) {
                    const std::size_t target = static_cast<std::size_t>(double(N) * frac);

                    if (target <= done) continue;

                    struct Move {
                        std::string victim, fresh;
                        std::uint64_t value;
                    };

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

                // ---------------- phase 2: post-update, LIVE keys ----------------
                std::vector<std::string> live_vec(live.begin(), live.end());
                std::sort(live_vec.begin(), live_vec.end(), ubyte_less);

                const auto post_probes = make_probes(live_vec, 20000, 20261010, 10, live_values);
                const std::uint64_t post_hash = hash_probes(post_probes);
                const Phase post = run_phase(*s, post_probes, qmode, kReplay, false);

                const auto m = s->memory_breakdown();

                out << "{\"kind\":\"" << kind << "\",\"run_tag\":\"" << run_tag
                    << "\",\"mode\":\"" << mode << "\",\"arm\":\"" << ARMS[arm].name
                    << "\",\"backend\":\""
                    << (ARMS[arm].backend == ModelBackend::LitsHpt ? "LitsHpt" : "Context16")
                    << "\",\"routing\":" << (ARMS[arm].routing ? "true" : "false")
                    << ",\"rep\":" << r << ",\"position\":" << oi
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
                    << ",\"routing_bytes\":" << s->routing_bytes()
                    << ",\"regions\":" << s->region_count()
                    << "}" << std::endl;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

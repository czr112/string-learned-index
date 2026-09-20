// E26 ablation: C0 (existing compaction) vs C1 (replay reuses the stored tag).
//
// CLEAN TIMING BUILD. No diagnostics. The tag-equality check that verifies C1 lives
// in diag_e26, because running it here would re-do the very hashing C1 removes --
// which is exactly why the diagnostic build's hash counter cannot show C1's saving.
// The saving has to be measured as TIME here, and as `compact_bytes_copied` there.
//
// Three arms, each one Config step from the next:
//   C0  existing compaction
//   C1  C0 + the replay reuses the stored tag (subtracts a recomputation)
//   C2  C1 + compaction runs only when the group holds at least as much reclaimable
//       garbage as the record that failed to place
// C1 runs the SAME operations as C0 and only does less work inside one of them, so it
// is a clean ablation. C2 CHANGES WHICH OPERATIONS RUN -- it is a trigger change, not
// an ablation, and its argument is that a compaction which cannot free enough bytes
// cannot enable the placement it was invoked for. That is verified in diag_e26
// (compact_helped), not assumed here.
//
// All arms sit on the E25 default candidate (V1 + V2), so this measures the increment
// on top of that, not against the old write path. No ART arm: ART's standing is
// established in E25_TIME.md and does not change here.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "native_e22.hpp"
#include "selected.hpp"
#include "switches.hpp"
#include "third_party/vendor/libart/src/art.h"

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
    int kind;  // 1 insert, 2 erase
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
        if (argc < 4) throw std::invalid_argument("bench_e26 KIND N REPS [TRANCOCSV] [RUNTAG]");
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

        // The query mode, same as the one E21/E25 used.
        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        // Pinned through the shared helper, NOT read from Config defaults: the
        // defaults were flipped at freeze time, and a benchmark that followed them
        // would silently start measuring a different configuration than the one it
        // reports. The scan switch is pinned OFF here -- it cannot affect an update
        // benchmark, and pinning it keeps the arms comparable to the measured run.
        Config c = e25sw::baseline_config(thr);
        c.write_fast_locate = true;                 // the E25 decision
        c.reuse_locate_ctx = true;
        // ART joins for the final standing, which MUST be measured in the same round
        // as the arm it is compared against -- a historical ART time is not evidence
        // about a new arm.
        Config cfg[3] = {c, c, c};
        cfg[1].compact_reuse_tag = true;               // C1
        cfg[2].compact_reuse_tag = true;               // C2 = C1 + a trigger gate
        cfg[2].compact_needs_garbage = true;
        const char* names[4] = {"C0", "C1", "C2", "ART"};

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

        std::ofstream out("results/main/e26_time.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        // A fixed query sequence, timed at each checkpoint. C2 lets a few more records
        // stay in overflow, so the READ side is part of its cost and has to be
        // measured, not argued away.
        std::vector<std::string> qs;
        {
            std::mt19937_64 rng(2026092001);
            const std::size_t n = std::min<std::size_t>(16384, N);
            for (std::size_t i = 0; i < n; ++i) qs.push_back(keys[rng() % N].first);
        }

        const char* wlnames[2] = {"insert_only", "replace"};
        const std::vector<Op>* wlops[2] = {&ins, &rep};

        for (int w = 0; w < 2; ++w) {
            const std::string wl = wlnames[w];
            const std::vector<Op>& ops = *wlops[w];
            const bool cycles = (wl == "replace");
            const std::size_t ncycles = cycles ? ops.size() / 2 : ops.size();

            for (int r = 0; r < reps; ++r) {
                // Rotate which arm goes first so "ran first" is not systematically
                // the same structure.
                int order[4] = {r % 4, (r + 1) % 4, (r + 2) % 4, (r + 3) % 4};
                for (int oi = 0; oi < 4; ++oi) {
                    const int arm = order[oi];
                    Reference ref;
                    for (const auto& kv : keys) ref[kv.first] = kv.second;

                    const bool is_art = (arm == 3);
                    std::uint64_t q_acc = 0;
                    std::unique_ptr<Index> s;
                    std::unique_ptr<e22art::NativeArt> art;
                    if (is_art) art = std::make_unique<e22art::NativeArt>(keys);
                    else {
                        s = std::make_unique<Index>(cfg[arm]);
                        s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
                    }

                    std::size_t consumed = 0, applied = 0;
                    for (double f : {0.05, 0.10, 0.25}) {
                        const std::size_t target = std::min<std::size_t>(
                            static_cast<std::size_t>(double(ncycles) * f), ncycles);
                        if (target <= consumed) continue;
                        std::vector<Op> seg;
                        const std::size_t b = cycles ? consumed * 2 : consumed;
                        const std::size_t e = cycles ? target * 2 : target;
                        for (std::size_t i = b; i < e; ++i) seg.push_back(ops[i]);

                        // Reference upkeep and answer checking OUTSIDE the timed span.
                        for (const auto& o : seg) {
                            if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert target present"); ref[o.key] = o.value; }
                            else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase target absent"); }
                        }

                        // Query the state BEFORE this segment is applied, so the
                        // checkpoints line up with the update checkpoints.
                        double q_ms = 0;
                        {
                            std::uint64_t acc = 0;
                            const auto q0 = Clock::now();
                            if (is_art) {
                                for (const auto& k : qs) { const Result r = art->find(k); acc += r.value + r.found; }
                            } else {
                                for (const auto& k : qs) { const Result r = s->find(k, qmode); acc += r.value + r.found; }
                            }
                            q_ms = ms_between(q0, Clock::now());
                            q_acc += acc;
                        }

                        const auto a = Clock::now();
                        if (is_art) {
                            for (const auto& o : seg) {
                                if (o.kind == 1) art->upsert(o.key, o.value);
                                else art->erase(o.key);
                            }
                        } else {
                            for (const auto& o : seg) {
                                if (o.kind == 1) s->insert(o.key, o.value);
                                else s->erase(o.key);
                            }
                        }
                        const double dt = ms_between(a, Clock::now());
                        consumed = target;
                        applied += seg.size();
                        const std::size_t got = is_art ? art->size() : s->size();
                        if (got != ref.size()) throw std::runtime_error("size drift vs reference");

                        out << "{\"kind\":\"" << kind << "\",\"workload\":\"" << wl
                            << "\",\"rep\":" << r << ",\"arm\":\"" << names[arm]
                            << "\",\"run_tag\":\"" << run_tag << "\""
                            << ",\"frac\":" << f << ",\"cycles\":" << consumed
                            << ",\"ops\":" << applied << ",\"ms\":" << dt
                            << ",\"query_ms\":" << q_ms << "}" << std::endl;
                    }
                    std::cerr << kind << " " << wl << " rep" << r << " " << names[arm] << " done" << std::endl;
                }
            }
        }
        std::cerr << "query accumulator " << sink << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

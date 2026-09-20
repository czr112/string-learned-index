// E25 formal comparison: V0 / V1 / V2 / ART on one workload each, same sequence.
//
// CLEAN TIMING BUILD. No diagnostic counters, no allocator wrappers. The counters
// that produced the call matrix live in a separate binary (diag_e25_calls); mixing
// them in would inflate whichever path allocates or compares the most.
//
// What the timed span contains: the index operations AND everything they trigger --
// compaction, region rebuilds, container growth, string allocation. What it does NOT
// contain: key generation, reference-map upkeep, and answer checking, all of which
// run before the span.
//
// The arms are the SAME BINARY with different Config flags (V0/V1/V2), so there is
// no compile difference between them; ART is the same vendored libart as E20/E21.
//
// Every arm starts from its own fresh index built from the SAME capacity plan, and
// receives the SAME operation sequence. Time is reported per segment (between
// checkpoints), so ratios can be formed PAIRED within a repetition rather than by
// dividing two aggregate medians.
//
// Queries are a separate timed quantity on a fixed key sequence: find_impl gained an
// optional out-parameter this round, and whether the read path still costs what it
// did is a measurement question, not a design claim.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "native_e22.hpp"
#include "selected.hpp"
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
    int kind;  // 0 update, 1 insert, 2 erase
    std::string key;
    std::uint64_t value;
};

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        SetProcessAffinityMask(GetCurrentProcess(), 1);
#elif defined(__linux__)
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 4) throw std::invalid_argument("bench_e25 KIND N REPS [TRANCOCSV] [RUNTAG]");
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
        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        c.multiblock = true;
        c.reuse_group_ids = true;   // the frozen default is R1
        c.ov_threshold = thr;

        Config cfg[3] = {c, c, c};
        cfg[1].write_fast_locate = true;
        cfg[2].write_fast_locate = true;  cfg[2].reuse_locate_ctx = true;

        Pipeline plan;
        {
            Index probe(c);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        // ---- operation sequences, built once ---------------------------------
        const std::size_t quarter = N / 4;
        std::vector<Op> upd, ins, rep;
        {
            std::vector<std::size_t> idx(N);
            for (std::size_t i = 0; i < N; ++i) idx[i] = i;
            std::mt19937_64 rng(2026091904);
            std::shuffle(idx.begin(), idx.end(), rng);
            for (std::size_t j = 0; j < quarter; ++j)
                upd.push_back({0, keys[idx[j]].first, 0x5A5A0000ULL + j});
        }
        {
            for (std::size_t j = 0; j < quarter; ++j)
                ins.push_back({1, keys[(j * 7) % N].first + "~new" + std::to_string(j), 0x1000ULL + j});
        }
        {
            for (std::size_t j = 0; j < quarter; ++j) {
                rep.push_back({2, keys[(j * 3) % N].first, 0});
                rep.push_back({1, keys[(j * 3) % N].first + "~rep" + std::to_string(j), 0x2000ULL + j});
            }
        }

        // ---- fixed query sequence, timed separately --------------------------
        std::vector<std::string> qs;
        {
            std::mt19937_64 rng(2026091905);
            const std::size_t n = std::min<std::size_t>(16384, N);
            for (std::size_t i = 0; i < n; ++i) qs.push_back(keys[rng() % N].first);
        }

        std::ofstream out("results/main/e25_time.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        const char* wlnames[3] = {"update_only", "insert_only", "replace"};
        const std::vector<Op>* wlops[3] = {&upd, &ins, &rep};

        for (int wi = 0; wi < 3; ++wi) {
            const std::string workload = wlnames[wi];
            const std::vector<Op>& ops = *wlops[wi];
            const bool cycles = (workload == "replace");
            const std::size_t ncycles = cycles ? ops.size() / 2 : ops.size();

            for (int r = 0; r < reps; ++r) {
                // Rotate which arm runs first, so "ran first" is not systematically
                // the same structure.
                int order[4] = {0, 1, 2, 3};
                for (int i = 0; i < 4; ++i) order[i] = (i + r) % 4;

                for (int oi = 0; oi < 4; ++oi) {
                    const int arm = order[oi];   // 0=V0 1=V1 2=V2 3=ART

                    Reference ref;
                    for (const auto& kv : keys) ref[kv.first] = kv.second;

                    std::unique_ptr<Index> s;
                    std::unique_ptr<e22art::NativeArt> art;
                    auto t_build0 = Clock::now();
                    if (arm < 3) {
                        s = std::make_unique<Index>(cfg[arm]);
                        s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
                    } else {
                        art = std::make_unique<e22art::NativeArt>(keys);
                    }
                    const double build_ms = ms_between(t_build0, Clock::now());

                    // ---- query checkpoint 0 (before any update) -----------------
                    double q_ms = 0;
                    {
                        std::uint64_t acc = 0;
                        const auto a = Clock::now();
                        if (arm < 3) for (const auto& k : qs) { const Result r = s->find(k, qmode); acc += r.value + r.found; }
                        else        for (const auto& k : qs) { const Result r = art->find(k);        acc += r.value + r.found; }
                        q_ms = ms_between(a, Clock::now());
                        sink += acc;
                    }
                    out << "{\"kind\":\"" << kind << "\",\"workload\":\"" << workload
                        << "\",\"rep\":" << r << ",\"arm\":\""
                        << (arm == 0 ? "V0" : arm == 1 ? "V1" : arm == 2 ? "V2" : "ART")
                        << "\",\"run_tag\":\"" << run_tag << "\""
                        << ",\"build_ms\":" << build_ms
                        << ",\"query_cp\":" << 0 << ",\"query_ms\":" << q_ms
                        << ",\"cycles\":" << 0 << ",\"ops\":0,\"update_ms\":0}" << std::endl;

                    // ---- progressive checkpoints --------------------------------
                    std::size_t consumed = 0;   // cycles (replace) or ops
                    std::size_t applied = 0;
                    for (double f : {0.05, 0.10, 0.25}) {                        const std::size_t target = std::min<std::size_t>(
                            static_cast<std::size_t>(double(ncycles) * f), ncycles);
                        if (target <= consumed) continue;
                        std::vector<Op> seg;
                        const std::size_t b = cycles ? consumed * 2 : consumed;
                        const std::size_t e = cycles ? target * 2 : target;
                        for (std::size_t i = b; i < e; ++i) seg.push_back(ops[i]);

                        // Reference upkeep OUTSIDE the timed span.
                        for (const auto& o : seg) {
                            if (o.kind == 0) { auto it = ref.find(o.key); if (it == ref.end()) throw std::runtime_error("update target absent"); it->second = o.value; }
                            else if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert target present"); ref[o.key] = o.value; }
                            else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase target absent"); }
                        }

                        const auto a = Clock::now();
                        if (arm < 3) {
                            for (const auto& o : seg) {
                                if (o.kind == 0) s->update(o.key, o.value);
                                else if (o.kind == 1) s->insert(o.key, o.value);
                                else s->erase(o.key);
                            }
                        } else {
                            for (const auto& o : seg) {
                                if (o.kind == 2) art->erase(o.key);
                                else art->upsert(o.key, o.value);
                            }
                        }
                        const double dt = ms_between(a, Clock::now());
                        consumed = target;
                        applied += seg.size();

                        // The arm must agree with the reference, or the timing is
                        // measuring something that is not the same structure.
                        if (arm < 3 && s->size() != ref.size())
                            throw std::runtime_error("size drift vs reference");
                        if (arm == 3 && art->size() != ref.size())
                            throw std::runtime_error("ART size drift vs reference");

                        out << "{\"kind\":\"" << kind << "\",\"workload\":\"" << workload
                            << "\",\"rep\":" << r << ",\"arm\":\""
                            << (arm == 0 ? "V0" : arm == 1 ? "V1" : arm == 2 ? "V2" : "ART")
                            << "\",\"run_tag\":\"" << run_tag << "\""
                            << ",\"build_ms\":0,\"query_cp\":-1,\"query_ms\":0"
                            // `cycles` counts completed UNITS of this workload: a replace
                            // cycle is one delete + one insert, so it advances once per two
                            // operations; for the single-op workloads a unit IS the
                            // operation. An earlier version wrote 0 for those, which
                            // silently collapsed every one of their checkpoints to "0%".
                            << ",\"cycles\":" << consumed
                            << ",\"frac\":" << f
                            << ",\"ops\":" << applied
                            << ",\"update_ms\":" << dt << "}" << std::endl;
                    }
                    std::cerr << kind << " " << workload << " rep" << r << " "
                              << (arm == 0 ? "V0" : arm == 1 ? "V1" : arm == 2 ? "V2" : "ART")
                              << " done" << std::endl;
                }
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

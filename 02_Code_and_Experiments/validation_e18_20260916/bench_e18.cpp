// E18: where does the FINAL construction path actually spend its time?
//
// E17 established the end-to-end result: the final flow (C) runs at 0.453-0.526 of
// the original (B), measured in one process. It did not decompose C, so "how much
// is still construction" had no same-round answer.
//
// This round answers that, and then looks inside the construction.
//
// Two builds from this one source:
//
//   bench_e18       clean. Three coarse spans -- statistics, planning, final index
//                   construction -- nested inside one outer span for the whole flow.
//                   Proportions are computed WITHIN a repetition (stage/flow), never
//                   by adding stage medians together.
//
//   bench_e18_diag  -DE14_PHASE -DE18_PREPASS -DE18_COUNT. Adds:
//                     * the Region-constructor phase breakdown over the final build
//                     * grp_prep vs place: the placement work partitioned into "work
//                       out the group id and fingerprint" and "everything else",
//                       using TWO clock calls per region rather than per key
//                     * activity counters for the operations that are too short to
//                       time individually
//
// The diagnostic build must produce the same structure; the round compares its
// digest against the clean build's and against the value earlier rounds recorded.
//
// Formal timings always come from the clean build. The diagnostic build is
// instrumented and is not used for any headline number.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
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

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ---------------------------------------------------------------------------
// Structural digest -- E14/E15/E16/E17's serialisation, so the value is comparable
// across rounds and not just inside this process.
// ---------------------------------------------------------------------------
class Sha256 {
public:
    Sha256() {
        static const std::uint32_t iv[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        for (int i = 0; i < 8; ++i) h_[i] = iv[i];
    }

    void update(const void* data, std::size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        total_ += n;
        while (n) {
            const std::size_t take = std::min<std::size_t>(n, 64 - buf_n_);
            std::memcpy(buf_ + buf_n_, p, take);
            buf_n_ += take;
            p += take;
            n -= take;
            if (buf_n_ == 64) { block(buf_); buf_n_ = 0; }
        }
    }

    void update(std::string_view s) { update(s.data(), s.size()); }

    void update_u64(std::uint64_t v) {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xffu);
        update(b, 8);
    }

    std::string hex() {
        const std::uint64_t bits = total_ * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (buf_n_ != 56) update(&zero, 1);
        unsigned char tail[8];
        for (int i = 0; i < 8; ++i) tail[i] = static_cast<unsigned char>((bits >> (8 * (7 - i))) & 0xffu);
        update(tail, 8);

        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i)
            for (int b = 3; b >= 0; --b) {
                const unsigned char byte = static_cast<unsigned char>((h_[i] >> (8 * b)) & 0xffu);
                out.push_back(digits[byte >> 4]);
                out.push_back(digits[byte & 0xf]);
            }
        return out;
    }

    static bool self_test() {
        Sha256 s;
        const char* abc = "abc";
        s.update(abc, 3);
        return s.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const unsigned char* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
                   (std::uint32_t(p[4 * i + 2]) << 8) | std::uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    std::uint32_t h_[8];
    std::uint64_t total_ = 0;
    unsigned char buf_[64];
    std::size_t buf_n_ = 0;
};

static std::string structure_digest(const Index& idx) {
    Sha256 h;
    h.update_u64(idx.region_count());
    for (std::size_t r = 0; r < idx.region_count(); ++r) {
        const Region& reg = idx.region(r);
        h.update_u64(r);
        h.update_u64(reg.prefix().size());
        h.update(reg.prefix());
        h.update_u64(reg.groups());
        h.update_u64(reg.size());
        h.update_u64(reg.overflow_count());
        h.update_u64(reg.block_bytes_total());
        for (auto v : reg.slot_table()) h.update_u64(v);
        for (auto v : reg.block_table()) h.update_u64(v);
        for (auto v : reg.offset_table()) h.update_u64(v);
        for (std::uint32_t g = 0; g < reg.groups(); ++g) {
            const std::size_t nb = reg.subblock_count(g);
            h.update_u64(nb);
            for (std::size_t i = 0; i < nb; ++i) {
                h.update_u64(reg.subblock_entries(g, i));
                const auto ks = reg.subblock_keys(g, i);
                h.update_u64(ks.size());
                for (const auto& k : ks) { h.update_u64(k.size()); h.update(k); }
            }
            const auto dir = reg.boundary_dir(g);
            h.update_u64(dir.size());
            for (const auto& e : dir) { h.update_u64(e.first.size()); h.update(e.first); h.update_u64(e.second); }
        }
        for (const auto& kv : reg.all()) {
            h.update_u64(kv.first.size());
            h.update(kv.first);
            h.update_u64(kv.second);
        }
    }
    return h.hex();
}

struct Pipeline {
    std::vector<Alloc> d_alloc, s_alloc;
    std::vector<std::vector<std::uint16_t>> s_caps;
};

static Pipeline make_plan(const std::vector<std::vector<GroupStats>>& demand) {
    Pipeline p;
    for (const auto& gs : demand) {
        const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
        p.d_alloc.push_back(demand_single(gs, budget));
        StrictLog log;
        std::vector<std::uint16_t> caps;
        p.s_alloc.push_back(demand_strict(gs, p.d_alloc.back(), log, caps));
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

#ifdef E14_PHASE
static validation::e14::Phases take_phases() {
    const auto p = validation::e14::phases();
    validation::e14::phases().reset();
    return p;
}
#endif

#ifdef E18_COUNT
struct CountSnap {
    validation::e18::Counts c;
};
static CountSnap take_counts() {
    CountSnap s{validation::e18::counts()};
    validation::e18::counts().reset();
    return s;
}
static std::uint64_t cget(const validation::e18::Counts& c, const std::string& k) {
    if (k == "group_calls") return c.group_calls;
    if (k == "fingerprint_calls") return c.fingerprint_calls;
    if (k == "region_ctors") return c.region_ctors;
    if (k == "subblock_probes") return c.subblock_probes;
    if (k == "insert_attempts") return c.insert_attempts;
    if (k == "insert_ok") return c.insert_ok;
    if (k == "entry_shifts") return c.entry_shifts;
    if (k == "suffix_bytes_copied") return c.suffix_bytes_copied;
    if (k == "overflow_inserts") return c.overflow_inserts;
    if (k == "overflow_containers") return c.overflow_containers;
    return 0;
}
static const char* COUNT_KEYS[] = {"group_calls", "fingerprint_calls", "region_ctors",
                                   "subblock_probes", "insert_attempts", "insert_ok",
                                   "entry_shifts", "suffix_bytes_copied",
                                   "overflow_inserts", "overflow_containers"};
#endif

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        SetProcessAffinityMask(GetCurrentProcess(), 1);
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#elif defined(__linux__)
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 3)
            throw std::invalid_argument("bench_e18 KIND N [TRANCOCSV] [RUNTAG]");
        if (!Sha256::self_test())
            throw std::runtime_error("SHA-256 self-test failed; digests would be meaningless");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const auto keys = make_data(kind, nkeys, trcsv);

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;

        const int REPS = 5;
        std::vector<double> flow, t_stats, t_plan, t_build;
        std::vector<double> p_stats, p_plan, p_build;
        std::string digest;

#ifdef E14_PHASE
        validation::e14::Phases ph{};
        std::vector<validation::e14::Phases> ph_build;
#endif
#ifdef E18_COUNT
        validation::e18::Counts cn_stats{}, cn_plan{}, cn_build{};
#endif

        for (int rep = 0; rep < REPS; ++rep) {
            const auto t0 = Clock::now();

            Index stats_only(c);
            const auto t1 = Clock::now();
            const auto demand = stats_only.demand_profile(keys);
            const auto t2 = Clock::now();
            const Pipeline plan = make_plan(demand);
            const auto t3 = Clock::now();
            auto x = std::make_unique<Index>(cm);
            x->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            const auto t4 = Clock::now();

            const double f = ms_between(t0, t4);
            const double s = ms_between(t1, t2);
            const double pl = ms_between(t2, t3);
            const double b = ms_between(t3, t4);

            flow.push_back(f);
            t_stats.push_back(s);
            t_plan.push_back(pl);
            t_build.push_back(b);
            // Proportions WITHIN the repetition. These sum to 1 by construction
            // apart from the outer-span overhead, and are never built by adding
            // stage medians.
            p_stats.push_back(s / f);
            p_plan.push_back(pl / f);
            p_build.push_back(b / f);

            if (rep == 0) digest = structure_digest(*x);
        }

#ifdef E14_PHASE
        // One more pass, instrumented, to capture the constructor phases and the
        // grp_prep/place partition. Not used for any headline timing.
        {
            Index stats_only(c);
            const auto demand = stats_only.demand_profile(keys);
            const Pipeline plan = make_plan(demand);
            validation::e14::phases().reset();
            auto x = std::make_unique<Index>(cm);
            x->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            ph = take_phases();
            ph_build.push_back(ph);
        }
#endif
#ifdef E18_COUNT
        // Counters, on their own pass over the three stages.
        {
            validation::e18::counts().reset();
            Index stats_only(c);
            const auto demand = stats_only.demand_profile(keys);
            cn_stats = take_counts().c;
            const Pipeline plan = make_plan(demand);
            cn_plan = take_counts().c;
            auto x = std::make_unique<Index>(cm);
            x->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            cn_build = take_counts().c;
        }
#endif

        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        auto join = [](const std::vector<double>& v) {
            std::string o = "[";
            for (std::size_t i = 0; i < v.size(); ++i) { if (i) o += ","; o += std::to_string(v[i]); }
            return o + "]";
        };

        std::ofstream fout("results/main/e18_build.jsonl", std::ios::app);
        fout.exceptions(std::ios::badbit | std::ios::failbit);
        fout << std::setprecision(10);
        fout << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
             << ",\"run_tag\":\"" << run_tag << "\",\"reps\":" << REPS
             << ",\"diag\":" <<
#ifdef E14_PHASE
                "true"
#else
                "false"
#endif
             << ",\"samples_ms\":{\"flow\":" << join(flow) << ",\"stats\":" << join(t_stats)
             << ",\"plan\":" << join(t_plan) << ",\"build\":" << join(t_build) << "}"
             << ",\"proportions\":{\"stats\":" << join(p_stats) << ",\"plan\":" << join(p_plan)
             << ",\"build\":" << join(p_build) << "}"
             << ",\"median_ms\":{\"flow\":" << median(flow) << ",\"stats\":" << median(t_stats)
             << ",\"plan\":" << median(t_plan) << ",\"build\":" << median(t_build) << "}"
             << ",\"median_proportion\":{\"stats\":" << median(p_stats) << ",\"plan\":" << median(p_plan)
             << ",\"build\":" << median(p_build) << "}"
             << ",\"digest\":\"" << digest << "\"";

#ifdef E14_PHASE
        {
            const auto& p = ph;
            fout << ",\"ctor_phases\":{\"prefix\":" << p.prefix << ",\"model\":" << p.model
                 << ",\"suffix_scan\":" << p.suffix_scan << ",\"demand_pass\":" << p.demand_pass
                 << ",\"layout\":" << p.layout << ",\"storage\":" << p.storage
                 << ",\"place\":" << p.place << ",\"grp_prep\":" << p.grp_prep
                 << ",\"dir\":" << p.dir << ",\"ctor_total\":" << p.ctor_total
                 << ",\"bulk_planned\":" << p.bulk_planned << "}";
        }
#endif
#ifdef E18_COUNT
        {
            auto emit = [&](const char* name, const validation::e18::Counts& cc) {
                fout << ",\"" << name << "\":{";
                for (std::size_t i = 0; i < sizeof(COUNT_KEYS) / sizeof(COUNT_KEYS[0]); ++i) {
                    if (i) fout << ",";
                    fout << "\"" << COUNT_KEYS[i] << "\":" << cget(cc, COUNT_KEYS[i]);
                }
                fout << "}";
            };
            emit("count_stats", cn_stats);
            emit("count_plan", cn_plan);
            emit("count_build", cn_build);
        }
#endif
        fout << "}" << std::endl;
        fout.close();

        std::cout << "OK " << kind << " flow=" << median(flow) << " stats=" << median(t_stats)
                  << " plan=" << median(t_plan) << " build=" << median(t_build)
                  << " props=" << median(p_stats) << "/" << median(p_plan) << "/" << median(p_build)
                  << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

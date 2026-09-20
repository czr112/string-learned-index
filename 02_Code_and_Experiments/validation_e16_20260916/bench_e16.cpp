// E16: what does the probe cost, and what does it cost to replace it?
//
// E14 measured `probe` at 44-75 ms, which is 46-50% of the one-pass construction
// flow -- the largest remaining item. The probe exists only so the pipeline can
// call group_stats(); E15 established that the allocators read four fields of it.
//
// This round adds Index::demand_profile, which produces the same vector<GroupStats>
// by replaying the placement predicate without allocating blocks. check_e16_probe
// is the correctness evidence (1,134,675 group statistics compared field by field
// over 2700 configurations). This file measures the difference.
//
// Both pipelines run IN THE SAME PROCESS on the same key set and the same plan
// step, and the resulting S and D indexes are compared by structural digest before
// any timing is reported -- the same discipline as E14/E15.
//
// It also reports the transient footprint of each statistics path, measured by
// wrapping the allocator (and, as a calibration, alongside the prototype's own
// memory() accounting, so a reader can see whether the wrapper is capturing).
//
// No libart here: this round is about the prototype's own construction flow, and
// native.hpp already defines calloc/free wrappers that would collide.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <malloc.h>
#include <memory>
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

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ---------------------------------------------------------------------------
// Allocator accounting, for the transient footprint of each statistics path.
// Peak is reset around the phase under measurement. malloc_usable_size is used on
// both sides so the count is the rounded block the allocator actually handed out.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Allocator accounting is a SEPARATE BUILD (-DE16_MEM), not part of the timing
// build.
//
// The first version of this file enabled accounting at the top of main and never
// turned it off, so every timed region ran with a wrapped allocator. That is
// exactly the wrong way round: the probe path allocates far more than the
// statistics path (block arenas, overflow containers, one string per overflow
// record), so per-allocation bookkeeping inflates the path this round is trying
// to show is expensive. The timings reported now come from a build with no
// wrappers at all; the footprint comes from this one.
// ---------------------------------------------------------------------------
#ifdef E16_MEM
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void* __real_aligned_alloc(std::size_t, std::size_t);
extern "C" int __real_posix_memalign(void**, std::size_t, std::size_t);
extern "C" void __real_free(void*);

namespace acct {
inline bool on = false;
inline long long live = 0, peak = 0;
}  // namespace acct

extern "C" void* __wrap_malloc(std::size_t n) {
    void* p = __real_malloc(n);
    if (acct::on && p) {
        acct::live += malloc_usable_size(p);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return p;
}

extern "C" void* __wrap_calloc(std::size_t n, std::size_t m) {
    void* p = __real_calloc(n, m);
    if (acct::on && p) {
        acct::live += malloc_usable_size(p);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return p;
}

extern "C" void* __wrap_realloc(void* q, std::size_t n) {
    const std::size_t before = (acct::on && q) ? malloc_usable_size(q) : 0;
    void* p = __real_realloc(q, n);
    if (acct::on && p) {
        acct::live -= static_cast<long long>(before);
        acct::live += malloc_usable_size(p);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return p;
}

// Region storage comes from ::operator new(size, align_val_t), which libstdc++
// satisfies through an ALIGNED allocation, not malloc. Wrapping only malloc
// undercounted the probe's peak by roughly half -- the calibration against
// probe.memory() is what exposed it (16.4 MB measured vs 35.4 MB accounted).
extern "C" void* __wrap_aligned_alloc(std::size_t a, std::size_t n) {
    void* p = __real_aligned_alloc(a, n);
    if (acct::on && p) {
        acct::live += malloc_usable_size(p);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return p;
}

extern "C" int __wrap_posix_memalign(void** out, std::size_t a, std::size_t n) {
    const int rc = __real_posix_memalign(out, a, n);
    if (acct::on && rc == 0 && *out) {
        acct::live += malloc_usable_size(*out);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return rc;
}

extern "C" void __wrap_free(void* p) {
    if (acct::on && p) acct::live -= malloc_usable_size(p);
    __real_free(p);
}

// Accounting runs for the WHOLE process, from the first line of main.
//
// An earlier version enabled it only around the phase under measurement. That is
// unsound with this counter: `live` only saw allocations made while enabled, but
// __wrap_free decremented for EVERY free -- so freeing anything allocated before
// the scope dragged the counter negative and the "peak" became meaningless (it
// reported the replacement path as LARGER than the probe path, which allocates
// whole blocks). Counting everything from process start makes `live` the true net.
struct PeakScope {
    long long base = 0, peak = 0, end = 0;
    PeakScope() {
        base = acct::live;
        acct::peak = acct::live;
    }
    void stop() {
        peak = acct::peak;
        end = acct::live;
    }
    double peak_delta_mb() const { return double(peak - base) / 1e6; }
    bool consistent() const { return end >= 0; }
};
#else
struct PeakScope {
    void stop() {}
    double peak_delta_mb() const { return 0.0; }
};
#endif


// ---------------------------------------------------------------------------
// Structural digest.
//
// SHA-256 over exactly the serialisation E14 and E15 used, so this round's value
// can be compared with E15's RECORDED value for the same key set -- that
// comparison is the cross-round evidence that replacing the probe did not change
// the structure. The implementation is E15's, including the FIPS self-test; a
// broken hash must fail loudly rather than report agreement.
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
            throw std::invalid_argument("bench_e16 KIND N [TRANCOCSV] [RUNTAG]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        if (!Sha256::self_test())
            throw std::runtime_error("SHA-256 self-test failed; digests would be meaningless");
#ifdef E16_MEM
        acct::on = true;   // see PeakScope: accounting must cover the whole process
#endif
        const auto keys = make_data(kind, nkeys, trcsv);

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;

        const int REPS = 3;
        std::vector<double> probe_ms, light_ms, plan_probe_ms, plan_light_ms, build_ms;
        double peak_probe = 0, peak_light = 0, probe_memory = 0;
        std::string dig_s_probe, dig_s_light, dig_d_probe, dig_d_light;

        for (int rep = 0; rep < REPS; ++rep) {
            const bool light_first = (rep % 2) == 1;

            std::vector<std::vector<GroupStats>> demand_probe, demand_light;
            double t_probe = 0, t_light = 0, t_plan_probe = 0, t_plan_light = 0;

            auto run_probe = [&] {
                demand_probe.clear();
                PeakScope pk;
                const auto t0 = Clock::now();
                auto probe = std::make_unique<Index>(c);
                probe->bulk_load(keys);
                t_probe = ms_between(t0, Clock::now());
                for (std::size_t i = 0; i < probe->region_count(); ++i)
                    demand_probe.push_back(probe->region(i).group_stats());
                pk.stop();
                if (rep == 0) {
                    // The delta includes the GroupStats vectors, which both paths
                    // must produce; it is the STRUCTURE the two paths differ in.
                    peak_probe = pk.peak_delta_mb();
                    std::size_t mem = 0;
                    for (std::size_t i = 0; i < probe->region_count(); ++i) mem += probe->region(i).memory();
                    probe_memory = double(mem) / 1e6;
                }
                // plan, timed separately
                const auto p0 = Clock::now();
                auto plan = make_plan(demand_probe);
                t_plan_probe = ms_between(p0, Clock::now());
                return plan;
            };

            auto run_light = [&] {
                demand_light.clear();
                PeakScope pk;
                Index stats_only(c);
                const auto t0 = Clock::now();
                demand_light = stats_only.demand_profile(keys);
                t_light = ms_between(t0, Clock::now());
                pk.stop();
                if (rep == 0) peak_light = pk.peak_delta_mb();
                const auto p0 = Clock::now();
                auto plan = make_plan(demand_light);
                t_plan_light = ms_between(p0, Clock::now());
                return plan;
            };

            Pipeline plan_probe, plan_light;
            if (light_first) { plan_light = run_light(); plan_probe = run_probe(); }
            else             { plan_probe = run_probe(); plan_light = run_light(); }

            // Build S both ways and compare the structures BEFORE timing anything.
            double t_build = 0;
            {
                auto a = std::make_unique<Index>(cm);
                auto b = std::make_unique<Index>(cm);
                const auto t0 = Clock::now();
                b->bulk_load_with_plan(keys, sg_of(plan_light.s_alloc), bb_of(plan_light.s_alloc),
                                       plan_light.s_caps);
                t_build = ms_between(t0, Clock::now());
                a->bulk_load_with_plan(keys, sg_of(plan_probe.s_alloc), bb_of(plan_probe.s_alloc),
                                       plan_probe.s_caps);
                dig_s_probe = structure_digest(*a);
                dig_s_light = structure_digest(*b);
                if (dig_s_probe != dig_s_light)
                    throw std::runtime_error("S structure differs between the two statistics paths");

                Index da(cm), db(cm);
                da.bulk_load_with_plan(keys, sg_of(plan_probe.d_alloc), bb_of(plan_probe.d_alloc));
                db.bulk_load_with_plan(keys, sg_of(plan_light.d_alloc), bb_of(plan_light.d_alloc));
                dig_d_probe = structure_digest(da);
                dig_d_light = structure_digest(db);
                if (dig_d_probe != dig_d_light)
                    throw std::runtime_error("D structure differs between the two statistics paths");
            }

            probe_ms.push_back(t_probe);
            light_ms.push_back(t_light);
            plan_probe_ms.push_back(t_plan_probe);
            plan_light_ms.push_back(t_plan_light);
            build_ms.push_back(t_build);
        }

        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        const double m_probe = median(probe_ms), m_light = median(light_ms);
        const double m_plan_p = median(plan_probe_ms), m_plan_l = median(plan_light_ms);
        const double m_build = median(build_ms);

        const double old_flow = m_probe + m_plan_p + m_build;
        const double new_flow = m_light + m_plan_l + m_build;

        {
            std::ofstream f("results/main/e16_build.jsonl", std::ios::app);
            f.exceptions(std::ios::badbit | std::ios::failbit);
            f << std::setprecision(10);
            f << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
              << ",\"run_tag\":\"" << run_tag << "\""
              << ",\"reps\":" << REPS
              << ",\"samples\":{\"probe\":[";
            for (std::size_t i = 0; i < probe_ms.size(); ++i) { if (i) f << ","; f << probe_ms[i]; }
            f << "],\"light\":[";
            for (std::size_t i = 0; i < light_ms.size(); ++i) { if (i) f << ","; f << light_ms[i]; }
            f << "],\"plan_probe\":[";
            for (std::size_t i = 0; i < plan_probe_ms.size(); ++i) { if (i) f << ","; f << plan_probe_ms[i]; }
            f << "],\"plan_light\":[";
            for (std::size_t i = 0; i < plan_light_ms.size(); ++i) { if (i) f << ","; f << plan_light_ms[i]; }
            f << "],\"build\":[";
            for (std::size_t i = 0; i < build_ms.size(); ++i) { if (i) f << ","; f << build_ms[i]; }
            f << "]}"
              << ",\"median_ms\":{\"probe\":" << m_probe << ",\"light\":" << m_light
              << ",\"plan_probe\":" << m_plan_p << ",\"plan_light\":" << m_plan_l
              << ",\"build\":" << m_build
              << ",\"flow_probe\":" << old_flow << ",\"flow_light\":" << new_flow << "}"
              << ",\"ratio\":{\"stats\":" << (m_light / m_probe)
              << ",\"flow\":" << (new_flow / old_flow) << "}"
              << ",\"mem_build\":" <<
#ifdef E16_MEM
                 "true"
#else
                 "false"
#endif
              << ",\"peak_mb\":{\"probe_path\":" << peak_probe << ",\"light_path\":" << peak_light
              << ",\"probe_memory\":" << probe_memory << "}"
              << ",\"digest\":{\"S\":\"" << dig_s_light << "\",\"D\":\"" << dig_d_light << "\"}"
              << ",\"digest_agrees\":" << (dig_s_probe == dig_s_light && dig_d_probe == dig_d_light ? "true" : "false")
              << "}" << std::endl;
        }

        std::cout << "OK " << kind << " probe=" << m_probe << "ms light=" << m_light
                  << "ms ratio=" << (m_light / m_probe)
                  << " flow=" << old_flow << "->" << new_flow
                  << " peak=" << peak_probe << "MB->" << peak_light << "MB" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

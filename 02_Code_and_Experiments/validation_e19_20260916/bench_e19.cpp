// E19 (R1): does reusing the group ids pay, measured in one process?
//
// The change is small: the constructor records the group id it computes in the
// demand pass and reads it back in the placement loop, instead of asking the model
// the same question twice. What it does NOT save is the fingerprint, which is
// computed once, in placement only -- the counters say group_calls = 2 per key and
// fingerprint_calls = 1 per key, so exactly one prediction per key is removed and
// none of grp_prep's fingerprint work is claimed.
//
//   bench_e19        clean. Both settings of Config::reuse_group_ids, alternating,
//                    with the structure digest compared before any timing.
//   bench_e19_diag   -DE18_COUNT -DE19_MEM. Prediction counts, allocation counts
//                    and the transient peak, per variant. Never a headline number.
//
// Headline timings come from the clean build only.

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
#include <malloc.h>
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
// Optional allocator accounting, for the diagnostic build only. See E16: wrapping
// the allocator inside a timed region inflates whichever path allocates more, so
// the clean build carries none of it.
// ---------------------------------------------------------------------------
#ifdef E19_MEM
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void* __real_aligned_alloc(std::size_t, std::size_t);
extern "C" int __real_posix_memalign(void**, std::size_t, std::size_t);
extern "C" void __real_free(void*);

namespace acct {
inline bool on = false;
inline long long live = 0, peak = 0;
inline std::uint64_t calls = 0;
}  // namespace acct

extern "C" void* __wrap_malloc(std::size_t n) {
    void* p = __real_malloc(n);
    if (acct::on && p) {
        ++acct::calls;
        acct::live += malloc_usable_size(p);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return p;
}
extern "C" void* __wrap_calloc(std::size_t n, std::size_t m) {
    void* p = __real_calloc(n, m);
    if (acct::on && p) {
        ++acct::calls;
        acct::live += malloc_usable_size(p);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return p;
}
extern "C" void* __wrap_realloc(void* q, std::size_t n) {
    const std::size_t before = (acct::on && q) ? malloc_usable_size(q) : 0;
    void* p = __real_realloc(q, n);
    if (acct::on && p) {
        ++acct::calls;
        acct::live -= static_cast<long long>(before);
        acct::live += malloc_usable_size(p);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return p;
}
// Region storage comes from ::operator new(size, align_val_t): an ALIGNED
// allocation, not malloc. E16 found this the hard way -- wrapping malloc alone
// undercounted the peak by half.
extern "C" void* __wrap_aligned_alloc(std::size_t a, std::size_t n) {
    void* p = __real_aligned_alloc(a, n);
    if (acct::on && p) {
        ++acct::calls;
        acct::live += malloc_usable_size(p);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return p;
}
extern "C" int __wrap_posix_memalign(void** out, std::size_t a, std::size_t n) {
    const int rc = __real_posix_memalign(out, a, n);
    if (acct::on && rc == 0 && *out) {
        ++acct::calls;
        acct::live += malloc_usable_size(*out);
        if (acct::live > acct::peak) acct::peak = acct::live;
    }
    return rc;
}
extern "C" void __wrap_free(void* p) {
    if (acct::on && p) acct::live -= malloc_usable_size(p);
    __real_free(p);
}

struct Scope {
    long long base = 0;
    Scope() {
        base = acct::live;
        acct::peak = acct::live;
        acct::calls = 0;
        acct::on = true;
    }
    void stop() { acct::on = false; }
    double peak_mb() const { return double(acct::peak - base) / 1e6; }
    std::uint64_t calls() const { return acct::calls; }
};
#else
struct Scope {
    void stop() {}
    double peak_mb() const { return 0.0; }
    std::uint64_t calls() const { return 0; }
};
#endif

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

struct Outcome {
    double ms = 0;
    std::string digest;
};

// The final construction path (C of E17/E18), with the reuse flag as the only
// variable.
static Outcome build_final(const std::vector<Pair>& keys, const Config& c, const Config& cm,
                           const Pipeline& plan) {
    const auto t0 = Clock::now();
    Index stats_only(c);
    const auto demand = stats_only.demand_profile(keys);
    auto x = std::make_unique<Index>(cm);
    x->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
    const auto t1 = Clock::now();
    return {ms_between(t0, t1), structure_digest(*x)};
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
            throw std::invalid_argument("bench_e19 KIND N [TRANCOCSV] [RUNTAG]");
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
        Config cm_off = c;
        cm_off.multiblock = true;
        cm_off.reuse_group_ids = false;
        Config cm_on = cm_off;
        cm_on.reuse_group_ids = true;

        // The plan comes from the probe, which neither variant touches, so both
        // build under exactly the same capacity plan.
        std::vector<std::vector<GroupStats>> demand;
        {
            Index probe(c);
            probe.bulk_load(keys);
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
        }
        const Pipeline plan = make_plan(demand);

        const int REPS = 5;
        std::vector<double> off_ms, on_ms, ratios;
        std::string d_off, d_on;
        double peak_off = 0, peak_on = 0;
        std::uint64_t calls_off = 0, calls_on = 0;
        std::uint64_t pred_off = 0, pred_on = 0;

        for (int rep = 0; rep < REPS; ++rep) {
            const bool on_first = (rep % 2) == 0;
            double t_on = 0, t_off = 0;
            Outcome o_off, o_on;

            if (on_first) { o_on = build_final(keys, c, cm_on, plan);
                            o_off = build_final(keys, c, cm_off, plan); }
            else          { o_off = build_final(keys, c, cm_off, plan);
                            o_on = build_final(keys, c, cm_on, plan); }
            t_off = o_off.ms;
            t_on = o_on.ms;
            if (rep == 0) { d_off = o_off.digest; d_on = o_on.digest; }

            off_ms.push_back(t_off);
            on_ms.push_back(t_on);
            ratios.push_back(t_on / t_off);
        }

        if (d_off != d_on)
            throw std::runtime_error("reuse changed the structure; refusing to report timings");

        // Diagnostic pass: counters and allocation accounting, one build per variant.
#ifdef E18_COUNT
        {
            validation::e18::counts().reset();
            { Index x(cm_off);
              x.bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps); }
            pred_off = validation::e18::counts().group_calls;
            validation::e18::counts().reset();
            { Index x(cm_on);
              x.bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps); }
            pred_on = validation::e18::counts().group_calls;
        }
#endif
        {
            Scope s;
            { Index x(cm_off);
              x.bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps); }
            s.stop();
            peak_off = s.peak_mb();
            calls_off = s.calls();
        }
        {
            Scope s;
            { Index x(cm_on);
              x.bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps); }
            s.stop();
            peak_on = s.peak_mb();
            calls_on = s.calls();
        }

        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        auto join = [](const std::vector<double>& v) {
            std::string o = "[";
            for (std::size_t i = 0; i < v.size(); ++i) { if (i) o += ","; o += std::to_string(v[i]); }
            return o + "]";
        };

        {
            std::ofstream f("results/main/e19_build.jsonl", std::ios::app);
            f.exceptions(std::ios::badbit | std::ios::failbit);
            f << std::setprecision(10);
            f << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
              << ",\"run_tag\":\"" << run_tag << "\",\"reps\":" << REPS
              << ",\"samples_ms\":{\"reuse_off\":" << join(off_ms) << ",\"reuse_on\":" << join(on_ms) << "}"
              << ",\"ratios\":" << join(ratios)
              << ",\"median_ms\":{\"reuse_off\":" << median(off_ms) << ",\"reuse_on\":" << median(on_ms) << "}"
              << ",\"median_ratio\":" << median(ratios)
              << ",\"digest\":\"" << d_on << "\""
              << ",\"predictions\":{\"off\":" << pred_off << ",\"on\":" << pred_on << "}"
              << ",\"alloc\":{\"off_calls\":" << calls_off << ",\"on_calls\":" << calls_on
              << ",\"off_peak_mb\":" << peak_off << ",\"on_peak_mb\":" << peak_on << "}"
              << "}" << std::endl;
        }

        std::cout << "OK " << kind << " off=" << median(off_ms) << " on=" << median(on_ms)
                  << " ratio=" << median(ratios)
                  << " pred " << pred_off << "->" << pred_on
                  << " peakMB " << peak_off << "->" << peak_on
                  << " allocs " << calls_off << "->" << calls_on << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

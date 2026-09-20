// E17: the three construction paths, compared IN ONE PROCESS.
//
// E15 removed the discarded pre-rebuild construction; E16 replaced the probe with a
// statistics replay. Each was measured against the path it replaced, in its own
// round. The two improvements were therefore NEVER measured against each other, and
// no product of the two rounds' ratios is a substitute -- the intermediate quantity
// is not the same observation in both.
//
// This round keeps all three paths in one clean timing program:
//
//   B   probe (full build)  + plan + bulk_load then rebuild_with_caps   (original)
//   A   probe (full build)  + plan + bulk_load_with_plan                (E15)
//   C   demand_profile      + plan + bulk_load_with_plan                (E16)
//
// Same keys, same Config, same plan. Each repetition runs all three in a ROTATED
// order, so position within the round cannot favour one path; ratios are formed
// WITHIN a repetition (A_r/B_r, C_r/A_r, C_r/B_r) and then aggregated.
//
// C/B is the number that was missing: the original flow against the final one.
//
// Before any timing, the three paths' structures are compared by digest. The
// serialisation is E14/E15/E16's, including the SHA-256 self-test.
//
// No allocator wrapping and no accounting here at all: the probe path allocates far
// more than the statistics path, so bookkeeping inside the timed region would
// inflate the very path under test. E16 measures the footprint in a separate build.

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


struct Outcome {
    double ms = 0;
    std::string digest;
};

// B: the original flow. Probe built in full, plan, then the index is built under the
// default capacity and every region is immediately replaced.
static Outcome path_B(const std::vector<Pair>& keys, const Config& c, const Config& cm) {
    const auto t0 = Clock::now();
    auto probe = std::make_unique<Index>(c);
    probe->bulk_load(keys);
    std::vector<std::vector<GroupStats>> demand;
    for (std::size_t i = 0; i < probe->region_count(); ++i)
        demand.push_back(probe->region(i).group_stats());
    const Pipeline plan = make_plan(demand);
    auto x = std::make_unique<Index>(cm);
    x->bulk_load(keys);
    for (std::size_t i = 0; i < x->region_count(); ++i)
        x->rebuild_with_caps(i, x->region(i).all(), plan.s_alloc[i].sg, plan.s_alloc[i].bb,
                             plan.s_caps[i]);
    const auto t1 = Clock::now();
    return {ms_between(t0, t1), structure_digest(*x)};
}

// A: E15. Same probe and plan, but the final index is constructed once.
static Outcome path_A(const std::vector<Pair>& keys, const Config& c, const Config& cm) {
    const auto t0 = Clock::now();
    auto probe = std::make_unique<Index>(c);
    probe->bulk_load(keys);
    std::vector<std::vector<GroupStats>> demand;
    for (std::size_t i = 0; i < probe->region_count(); ++i)
        demand.push_back(probe->region(i).group_stats());
    const Pipeline plan = make_plan(demand);
    auto x = std::make_unique<Index>(cm);
    x->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
    const auto t1 = Clock::now();
    return {ms_between(t0, t1), structure_digest(*x)};
}

// C: E16. The probe is replaced by the statistics replay.
static Outcome path_C(const std::vector<Pair>& keys, const Config& c, const Config& cm) {
    const auto t0 = Clock::now();
    Index stats_only(c);
    const auto demand = stats_only.demand_profile(keys);
    const Pipeline plan = make_plan(demand);
    auto x = std::make_unique<Index>(cm);
    x->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
    const auto t1 = Clock::now();
    return {ms_between(t0, t1), structure_digest(*x)};
}

static std::string join(const std::vector<double>& v) {
    std::string o = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) o += ",";
        o += std::to_string(v[i]);
    }
    return o + "]";
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
            throw std::invalid_argument("bench_e17 KIND N [TRANCOCSV] [RUNTAG]");
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
        std::vector<double> b_ms, a_ms, c_ms, ab, ca, cb;
        std::string db, da, dc;

        // Rotation by rep over the three positions, so "ran first" is spread evenly
        // across the paths rather than always landing on the same one.
        const int perm[3][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}};

        for (int rep = 0; rep < REPS; ++rep) {
            double tb = 0, ta = 0, tc = 0;
            for (int s = 0; s < 3; ++s) {
                switch (perm[rep % 3][s]) {
                    case 0: { const auto o = path_B(keys, c, cm); tb = o.ms; if (rep == 0) db = o.digest; break; }
                    case 1: { const auto o = path_A(keys, c, cm); ta = o.ms; if (rep == 0) da = o.digest; break; }
                    default: { const auto o = path_C(keys, c, cm); tc = o.ms; if (rep == 0) dc = o.digest; break; }
                }
            }
            b_ms.push_back(tb);
            a_ms.push_back(ta);
            c_ms.push_back(tc);
            ab.push_back(ta / tb);
            ca.push_back(tc / ta);
            cb.push_back(tc / tb);
        }

        // The equality gate: nothing is reported if the three paths disagree.
        if (!(db == da && da == dc))
            throw std::runtime_error("the three paths do not produce the same structure");

        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        const double m_b = median(b_ms), m_a = median(a_ms), m_c = median(c_ms);

        {
            std::ofstream f("results/main/e17_build.jsonl", std::ios::app);
            f.exceptions(std::ios::badbit | std::ios::failbit);
            f << std::setprecision(10);
            f << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
              << ",\"run_tag\":\"" << run_tag << "\",\"reps\":" << REPS
              << ",\"samples\":{\"B\":" << join(b_ms) << ",\"A\":" << join(a_ms)
              << ",\"C\":" << join(c_ms) << "}"
              << ",\"ratios\":{\"A_over_B\":" << join(ab) << ",\"C_over_A\":" << join(ca)
              << ",\"C_over_B\":" << join(cb) << "}"
              << ",\"median_ms\":{\"B\":" << m_b << ",\"A\":" << m_a << ",\"C\":" << m_c << "}"
              << ",\"median_ratio\":{\"A_over_B\":" << median(ab) << ",\"C_over_A\":" << median(ca)
              << ",\"C_over_B\":" << median(cb) << "}"
              << ",\"digest\":\"" << dc << "\""
              << "}" << std::endl;
        }

        std::cout << "OK " << kind << " B=" << m_b << " A=" << m_a << " C=" << m_c
                  << " A/B=" << median(ab) << " C/A=" << median(ca) << " C/B=" << median(cb)
                  << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

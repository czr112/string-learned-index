// E14: where does the build time actually go?
//
// E12 measured the end-to-end build cost (S 138-182 ms vs ART 47-94 ms) but not
// its composition. E13 closed the query side of the story; this round opens the
// build side.
//
// Two targets are built from this same file:
//
//   bench_e14        WITHOUT -DE14_PHASE -> clean stage timings
//   bench_e14_phase  WITH    -DE14_PHASE -> the same stage timings PLUS the
//                                          per-phase counters inside Region's
//                                          constructor
//
// Both write the same canonical layout digest for every structure they build.
// E14 asserts the digests agree across the two builds: that is the evidence that
// the instrumentation is inert, and it is checked BEFORE any number is reported.
//
// What is timed
// -------------
// E12/E13 define a structure's build_ms as `bulk_load(keys)` followed by one
// `rebuild_with_caps` per region. That is reproduced exactly here as
// `d_counted`/`s_counted`, so this round's totals must reconcile with E12/E13.
//
// But that definition starts AFTER an unnamed prefix of work that the same
// pipeline really performs: the `probe` build that supplies group statistics,
// the statistics themselves, and the two capacity allocators. Those are timed as
// separate stages and reported as `pipeline_total`, which is the honest cost of
// getting an S index from a sorted key vector.
//
// No number here is a speed-up claim. This round produces a decomposition only.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "native.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
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
// SHA-256, self-contained.
//
// The digests below decide whether two builds of the same layout are equal, so a
// wrong hash would silently turn a real difference into "identical". The
// constructor therefore self-tests against the FIPS vector for "abc" and throws
// if it does not match.
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

    // FIPS 180-4 self-check. Cheap, and it makes a silent hash bug impossible.
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

// ---------------------------------------------------------------------------
// Canonical layout digest.
//
// Covers everything the build decides: region split, region prefix, the three
// capacity tables, the derived sub-block structure, the boundary directory, and
// every stored (key, value) in scan order. Two structures with the same digest
// have the same model output, the same grouping, the same capacity plan and the
// same placement -- which is exactly the "keep the model output unchanged"
// constraint this round is bound by.
// ---------------------------------------------------------------------------
static std::string layout_digest(const Index& idx) {
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
                const auto keys = reg.subblock_keys(g, i);
                h.update_u64(keys.size());
                for (const auto& k : keys) { h.update_u64(k.size()); h.update(k); }
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

#ifdef E14_PHASE
static validation::e14::Phases take_phases() {
    const auto p = validation::e14::phases();
    validation::e14::phases().reset();
    return p;
}
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
            throw std::invalid_argument("bench_e14 KIND N [TRANCOCSV] [RUNTAG]");
        if (!Sha256::self_test())
            throw std::runtime_error("SHA-256 self-test failed; digests would be meaningless");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        // Passed through to the record so warm-up runs can be excluded by tag
        // rather than by guessing. The first execution of a freshly built binary
        // pays its own page-in cost, which is not part of the build being measured.
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const auto keys = make_data(kind, nkeys, trcsv);

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;

        // --- stage clocks -------------------------------------------------
        double t_probe = 0, t_stats = 0, t_alloc = 0;
        double t_d_bulk = 0, t_d_rebuild = 0, t_d_all = 0;
        double t_s_bulk = 0, t_s_rebuild = 0, t_s_all = 0;
        double t_art = 0;
        std::string d_digest, s_digest;

#ifdef E14_PHASE
        validation::e14::phases().reset();
        validation::e14::Phases p_probe{}, p_d_bulk{}, p_d_rebuild{}, p_s_bulk{}, p_s_rebuild{};
#endif

        {
            const auto t0 = Clock::now();
            Index probe(c);
            probe.bulk_load(keys);
            const auto t1 = Clock::now();
            t_probe = ms_between(t0, t1);

#ifdef E14_PHASE
            p_probe = take_phases();
#endif

            std::vector<std::vector<GroupStats>> demand;
            std::vector<std::uint64_t> budget;
            const auto t2 = Clock::now();
            for (std::size_t i = 0; i < probe.region_count(); ++i) {
                demand.push_back(probe.region(i).group_stats());
                budget.push_back(std::uint64_t(demand.back().size()) * 1024);
            }
            const auto t3 = Clock::now();
            t_stats = ms_between(t2, t3);

            std::vector<Alloc> d_alloc, s_alloc;
            std::vector<std::vector<std::uint16_t>> s_caps;
            ClassDelta delta;
            StrictLog slog;
            const auto t4 = Clock::now();
            for (std::size_t i = 0; i < demand.size(); ++i) {
                d_alloc.push_back(demand_single(demand[i], budget[i]));
                std::vector<std::uint16_t> caps;
                s_alloc.push_back(demand_strict(demand[i], d_alloc[i], slog, caps));
                s_caps.push_back(std::move(caps));
            }
            const auto t5 = Clock::now();
            t_alloc = ms_between(t4, t5);

            auto* d = new Index(c);
            {
                const auto a0 = Clock::now();
                d->bulk_load(keys);
                const auto a1 = Clock::now();
                t_d_bulk = ms_between(a0, a1);
#ifdef E14_PHASE
                p_d_bulk = take_phases();
#endif
                const auto b0 = Clock::now();
                for (std::size_t i = 0; i < d->region_count(); ++i)
                    d->rebuild_with_caps(i, d->region(i).all(), d_alloc[i].sg, d_alloc[i].bb);
                const auto b1 = Clock::now();
                t_d_rebuild = ms_between(b0, b1);
#ifdef E14_PHASE
                p_d_rebuild = take_phases();
#endif
            }
            t_d_all = t_d_bulk + t_d_rebuild;

            auto* s = new Index(cm);
            {
                const auto a0 = Clock::now();
                s->bulk_load(keys);
                const auto a1 = Clock::now();
                t_s_bulk = ms_between(a0, a1);
#ifdef E14_PHASE
                p_s_bulk = take_phases();
#endif
                const auto b0 = Clock::now();
                for (std::size_t i = 0; i < s->region_count(); ++i)
                    s->rebuild_with_caps(i, s->region(i).all(), s_alloc[i].sg, s_alloc[i].bb, s_caps[i]);
                const auto b1 = Clock::now();
                t_s_rebuild = ms_between(b0, b1);
#ifdef E14_PHASE
                p_s_rebuild = take_phases();
#endif
            }
            t_s_all = t_s_bulk + t_s_rebuild;

            d_digest = layout_digest(*d);
            s_digest = layout_digest(*s);

            const auto c0 = Clock::now();
            auto art = std::make_shared<e12::NativeArt>(keys);
            const auto c1 = Clock::now();
            t_art = ms_between(c0, c1);

            delete d;
            delete s;
        }

        // --- record -------------------------------------------------------
        {
            std::ofstream f("results/main/e14_build.jsonl", std::ios::app);
            f.exceptions(std::ios::badbit | std::ios::failbit);
            f << std::setprecision(10);
            f << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
              << ",\"run_tag\":\"" << run_tag << "\""
              << ",\"phases_build\":" <<
#ifdef E14_PHASE
                 "true"
#else
                 "false"
#endif
              << ",\"stage_ms\":{"
              << "\"probe\":" << t_probe
              << ",\"stats\":" << t_stats
              << ",\"alloc\":" << t_alloc
              << ",\"d_bulk\":" << t_d_bulk
              << ",\"d_rebuild\":" << t_d_rebuild
              << ",\"d_counted\":" << t_d_all
              << ",\"s_bulk\":" << t_s_bulk
              << ",\"s_rebuild\":" << t_s_rebuild
              << ",\"s_counted\":" << t_s_all
              << ",\"pipeline_total\":" << (t_probe + t_stats + t_alloc + t_s_all)
              << ",\"art\":" << t_art
              << "},\"digest\":{\"D\":\"" << d_digest << "\",\"S\":\"" << s_digest << "\"}";

#ifdef E14_PHASE
            auto emit = [&](const char* name, const validation::e14::Phases& p) {
                f << ",\"" << name << "\":{\"prefix\":" << p.prefix
                  << ",\"model\":" << p.model
                  << ",\"suffix_scan\":" << p.suffix_scan
                  << ",\"demand_pass\":" << p.demand_pass
                  << ",\"layout\":" << p.layout
                  << ",\"storage\":" << p.storage
                  << ",\"place\":" << p.place
                  << ",\"dir\":" << p.dir
                  << ",\"ctor_total\":" << p.ctor_total
                  << ",\"ctor_calls\":" << p.ctor_calls
                  << ",\"bulk_load\":" << p.bulk_load
                  << ",\"rebuild\":" << p.rebuild
                  << ",\"rebuild_calls\":" << p.rebuild_calls
                  << ",\"region_all\":" << p.region_all << "}";
            };
            emit("ph_probe", p_probe);
            emit("ph_d_bulk", p_d_bulk);
            emit("ph_d_rebuild", p_d_rebuild);
            emit("ph_s_bulk", p_s_bulk);
            emit("ph_s_rebuild", p_s_rebuild);
#endif
            f << "}" << std::endl;
        }

        std::cout << "OK " << kind << " s_counted=" << t_s_all << "ms digest=" << s_digest.substr(0, 16)
                  << "…" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

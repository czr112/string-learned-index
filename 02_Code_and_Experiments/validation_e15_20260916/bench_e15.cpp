// E15: build S in one pass instead of two.
//
// E14 established two things:
//   * S's construction was measured as `bulk_load(keys)` + one
//     `rebuild_with_caps` per region;
//   * that first half is discarded work -- 55.6%-66.3% of the build -- because
//     `rebuild_with_caps` constructs a brand new Region, and the first build's
//     only consumer is `Region::all()`, which hands back a contiguous slice of
//     the sorted input that the caller already holds.
//
// This round adds `Index::bulk_load_with_plan`, which builds every region once,
// directly under the capacity plan the allocators produced. Nothing about the
// resulting structure changes; only the number of times it is constructed does.
//
// The A/B happens INSIDE ONE PROCESS, so the two pipelines see the same machine
// state, the same allocator state and the same cache. Before any timing is
// reported, both structures' canonical layout digests are computed and compared;
// a mismatch aborts the run. A shared query workload is then run against both and
// every found/value is compared as well.
//
// What is NOT claimed: the probe pass is still there, and E15 does not touch it.
// `group_stats()` reads a constructed region, so producing the same statistics
// without constructing one is a separate change and is left to a later round.

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

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ---------------------------------------------------------------------------
// SHA-256, self-contained, with a FIPS self-test. Identical to E14's: the digest
// is what decides whether the one-pass build produced the same structure, so a
// wrong hash must fail loudly rather than report "identical".
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

// Canonical layout digest -- byte-for-byte the same serialisation E14 used, so
// the two rounds' equality claims are about the same thing.
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

struct BenchQuery {
    std::string key;
    std::uint64_t expected = 0;
    bool hit = false;
};

static std::vector<BenchQuery> make_queries(const std::vector<Pair>& keys, std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<BenchQuery> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& kv = keys[rng() % keys.size()];
        BenchQuery q;
        q.hit = i % 2 == 0;
        q.key = kv.first;
        if (q.hit) {
            q.expected = kv.second;
        } else {
            q.key += "~missing~";
            auto it = std::lower_bound(keys.begin(), keys.end(), q.key,
                [](const Pair& a, const std::string& b) { return compare(a.first, b) < 0; });
            if (it != keys.end() && it->first == q.key)
                throw std::runtime_error("negative query exists");
        }
        out.push_back(std::move(q));
    }
    std::shuffle(out.begin(), out.end(), rng);
    return out;
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
            throw std::invalid_argument("bench_e15 KIND N [TRANCOCSV] [RUNTAG]");
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

        // --- the plan, computed once and shared by both pipelines -------------
        double t_probe = 0, t_stats = 0, t_alloc = 0;
        std::vector<std::vector<std::uint16_t>> d_sg, s_sg, s_caps;
        std::vector<std::vector<std::uint32_t>> d_bb, s_bb;
        {
            const auto t0 = Clock::now();
            Index probe(c);
            probe.bulk_load(keys);
            t_probe = ms_between(t0, Clock::now());

            std::vector<std::vector<GroupStats>> demand;
            std::vector<std::uint64_t> budget;
            const auto t1 = Clock::now();
            for (std::size_t i = 0; i < probe.region_count(); ++i) {
                demand.push_back(probe.region(i).group_stats());
                budget.push_back(std::uint64_t(demand.back().size()) * 1024);
            }
            t_stats = ms_between(t1, Clock::now());

            std::vector<Alloc> d_alloc, s_alloc;
            std::vector<std::vector<std::uint16_t>> d_caps;
            ClassDelta delta;
            StrictLog slog;
            const auto t2 = Clock::now();
            for (std::size_t i = 0; i < demand.size(); ++i) {
                d_alloc.push_back(demand_single(demand[i], budget[i]));
                std::vector<std::uint16_t> caps;
                s_alloc.push_back(demand_strict(demand[i], d_alloc[i], slog, caps));
                s_caps.push_back(std::move(caps));
            }
            t_alloc = ms_between(t2, Clock::now());

            for (auto& a : d_alloc) { d_sg.push_back(std::move(a.sg)); d_bb.push_back(std::move(a.bb)); }
            for (auto& a : s_alloc) { s_sg.push_back(std::move(a.sg)); s_bb.push_back(std::move(a.bb)); }
            std::cerr << "plan: regions=" << s_sg.size() << " probe=" << t_probe << "ms\n";
        }

        // Old pipeline: bulk_load under the default capacity, then replace every
        // region. New pipeline: construct each region once, under the plan.
        //
        // Split of the OLD S pipeline, recorded on the first repetition only. E14
        // predicted the saving from the standalone cost of the discarded half;
        // these three numbers are what that half actually costs in THIS process,
        // so the prediction is checked against the outcome rather than asserted
        // to match it. Declared before the lambdas that write them.
        double t_s_bulk = 0, t_s_rebuild = 0, t_s_all = 0;

        auto build_D_old = [&] {
            auto* x = new Index(c);
            x->bulk_load(keys);
            for (std::size_t i = 0; i < x->region_count(); ++i)
                x->rebuild_with_caps(i, x->region(i).all(), d_sg[i], d_bb[i]);
            return x;
        };
        auto build_D_new = [&] {
            auto* x = new Index(c);
            x->bulk_load_with_plan(keys, d_sg, d_bb);
            return x;
        };
        auto build_S_old = [&](bool record) {
            auto* x = new Index(cm);
            const auto a0 = Clock::now();
            x->bulk_load(keys);
            const auto a1 = Clock::now();
            double all_ms = 0;
            for (std::size_t i = 0; i < x->region_count(); ++i) {
                const auto u0 = Clock::now();
                auto data = x->region(i).all();
                all_ms += ms_between(u0, Clock::now());
                x->rebuild_with_caps(i, std::move(data), s_sg[i], s_bb[i], s_caps[i]);
            }
            const auto a2 = Clock::now();
            if (record) {
                t_s_bulk = ms_between(a0, a1);
                t_s_rebuild = ms_between(a1, a2);
                t_s_all = all_ms;
            }
            return x;
        };
        auto build_S_new = [&] {
            auto* x = new Index(cm);
            x->bulk_load_with_plan(keys, s_sg, s_bb, s_caps);
            return x;
        };

        const int REPS = 3;
        std::vector<double> d_old_ms, d_new_ms, s_old_ms, s_new_ms;
        std::string d_digest_old, d_digest_new, s_digest_old, s_digest_new;

        for (int rep = 0; rep < REPS; ++rep) {
            // Alternate which pipeline is built first, so "ran first" cannot be
            // confounded with "is the old one".
            const bool new_first = (rep % 2) == 1;

            auto run = [&](bool is_new, bool is_s, bool record) {
                const auto t0 = Clock::now();
                Index* x = is_s ? (is_new ? build_S_new() : build_S_old(record))
                                : (is_new ? build_D_new() : build_D_old());
                const double dt = ms_between(t0, Clock::now());
                if (rep == 0) {
                    const auto dg = layout_digest(*x);
                    if (is_s) { if (is_new) s_digest_new = dg; else s_digest_old = dg; }
                    else      { if (is_new) d_digest_new = dg; else d_digest_old = dg; }
                }
                delete x;
                return dt;
            };

            // D pair
            if (new_first) { d_new_ms.push_back(run(true, false, false)); d_old_ms.push_back(run(false, false, false)); }
            else           { d_old_ms.push_back(run(false, false, false)); d_new_ms.push_back(run(true, false, false)); }
            // S pair; the old side records its own split on the first repetition
            if (new_first) { s_new_ms.push_back(run(true, true, false)); s_old_ms.push_back(run(false, true, rep == 0)); }
            else           { s_old_ms.push_back(run(false, true, rep == 0)); s_new_ms.push_back(run(true, true, false)); }
        }

        // --- the equality gate: nothing below is reported if this fails --------
        if (s_digest_old != s_digest_new)
            throw std::runtime_error("S layout digest differs between the two-pass and one-pass builds");
        if (d_digest_old != d_digest_new)
            throw std::runtime_error("D layout digest differs between the two-pass and one-pass builds");

        // --- query A/B on the shared workload ---------------------------------
        std::size_t q_checked = 0;
        {
            const auto qs = make_queries(keys, 131072, 20260916);
            Mode mode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
            auto* a = build_S_old(false);
            auto* b = build_S_new();
            for (const auto& q : qs) {
                const auto ra = a->find(q.key, mode);
                const auto rb = b->find(q.key, mode);
                if (ra.found != rb.found || (ra.found && ra.value != rb.value))
                    throw std::runtime_error("query A/B mismatch between the two builds");
                ++q_checked;
            }
            delete a;
            delete b;
        }

        const auto start = Clock::now();
        auto art = std::make_shared<e12::NativeArt>(keys);
        const double art_ms = ms_between(start, Clock::now());

        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };

        {
            std::ofstream f("results/main/e15_build.jsonl", std::ios::app);
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
              << ",\"plan_ms\":{\"probe\":" << t_probe << ",\"stats\":" << t_stats
              << ",\"alloc\":" << t_alloc << "}"
              << ",\"reps\":" << REPS
              // Samples are emitted as ARRAYS. A bare comma-separated number list
              // after a key is not valid JSON, and writing one produced a file
              // that every reader rejected -- caught as soon as the analysis ran.
              << ",\"samples\":{\"s_old\":" << [&]{ std::string o = "["; for (std::size_t i = 0; i < s_old_ms.size(); ++i) { if (i) o += ","; o += std::to_string(s_old_ms[i]); } return o + "]"; }()
              << ",\"s_new\":" << [&]{ std::string o = "["; for (std::size_t i = 0; i < s_new_ms.size(); ++i) { if (i) o += ","; o += std::to_string(s_new_ms[i]); } return o + "]"; }()
              << ",\"d_old\":" << [&]{ std::string o = "["; for (std::size_t i = 0; i < d_old_ms.size(); ++i) { if (i) o += ","; o += std::to_string(d_old_ms[i]); } return o + "]"; }()
              << ",\"d_new\":" << [&]{ std::string o = "["; for (std::size_t i = 0; i < d_new_ms.size(); ++i) { if (i) o += ","; o += std::to_string(d_new_ms[i]); } return o + "]"; }()
              << "}"
              << ",\"median_ms\":{\"s_old\":" << median(s_old_ms) << ",\"s_new\":" << median(s_new_ms)
              << ",\"d_old\":" << median(d_old_ms) << ",\"d_new\":" << median(d_new_ms)
              << ",\"art\":" << art_ms << "}"
              << ",\"old_split_ms\":{\"bulk\":" << t_s_bulk << ",\"rebuild\":" << t_s_rebuild
              << ",\"region_all\":" << t_s_all << "}"
              << ",\"digest\":{\"S\":\"" << s_digest_new << "\",\"D\":\"" << d_digest_new << "\"}"
              << ",\"query_ab_checked\":" << q_checked
              << "}" << std::endl;
        }

        std::cout << "OK " << kind << " s_old=" << median(s_old_ms) << "ms s_new=" << median(s_new_ms)
                  << "ms ratio=" << (median(s_new_ms) / median(s_old_ms)) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

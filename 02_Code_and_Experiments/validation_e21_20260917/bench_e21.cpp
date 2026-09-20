// E21: point queries on the FROZEN default, against ART, a sorted array and the
// original S.
//
// The build work is finished and frozen (R1). This round answers a different
// question: where does the frozen index stand when you QUERY it?
//
//   sorted_array       std::lower_bound over the shared sorted keys (no copy)
//   ART_armong         native armong/libart
//   S_original         the two-pass construction (bulk_load + rebuild_with_caps)
//   S_frozen_R1        the frozen default: statistics -> plan -> direct build + R1
//
// R1 optimised CONSTRUCTION. It is NOT presumed to query faster than the original
// S -- and it should not be, because the two build the same structure. That is
// asserted by digest first, so the query comparison is against a known-equal
// structure and the round can confirm "build changes did not move query cost"
// rather than assume it.
//
// Protocol is E12/E13's, unchanged: every method is verified query by query outside
// the timed region, then rounds are shuffled, warmed and checksummed. Queries are
// timed in three classes so a shift in the hit/miss balance cannot hide behind an
// aggregate.
//
//   bench_e21      clean. No allocator wrappers.
//   bench_e21_mem  -DE20_ART_MEM plus the wrap flags, for ART's own footprint.
//
// Data, query sequence, leaf config, routing and overflow policy are the same for
// every method; nothing is tuned in this round.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "native_e20.hpp"
#include "third_party/vendor/libart/src/art.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <malloc.h>
#include <memory>
#include <random>
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

static double ns(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}
static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
static volatile std::uint64_t sink = 0;
static std::uint64_t checksum(const Result& r) {
    return r.found ? r.value + 0x9e3779b97f4a7c15ULL : 0;
}

// ---------------------------------------------------------------------------
// Allocator accounting for ART's footprint, diagnostic build only.
// ---------------------------------------------------------------------------
#ifdef E20_ART_MEM
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void* __real_aligned_alloc(std::size_t, std::size_t);
extern "C" int __real_posix_memalign(void**, std::size_t, std::size_t);
extern "C" void __real_free(void*);

// ART opens and closes its own window around its constructor (see native_e20.hpp),
// so the hook keys off e20mem::on -- NOT a flag this file owns. An earlier version
// checked a local flag that nothing ever set, and ART's footprint came out as
// sizeof(tree_) with a peak of zero.
namespace e20mem {
bool on = true;
long long live = 0;
long long peak = 0;
}  // namespace e20mem

static void note(void* p) {
    if (!e20mem::on || !p) return;
    e20mem::live += malloc_usable_size(p);
    if (e20mem::live > e20mem::peak) e20mem::peak = e20mem::live;
}
extern "C" void* __wrap_malloc(std::size_t n) { void* p = __real_malloc(n); note(p); return p; }
extern "C" void* __wrap_calloc(std::size_t a, std::size_t b) { void* p = __real_calloc(a, b); note(p); return p; }
extern "C" void* __wrap_realloc(void* q, std::size_t n) {
    const std::size_t before = (e20mem::on && q) ? malloc_usable_size(q) : 0;
    void* p = __real_realloc(q, n);
    if (e20mem::on && p) { e20mem::live -= (long long)before; e20mem::live += malloc_usable_size(p); if (e20mem::live > e20mem::peak) e20mem::peak = e20mem::live; }
    return p;
}
extern "C" void* __wrap_aligned_alloc(std::size_t a, std::size_t n) { void* p = __real_aligned_alloc(a, n); note(p); return p; }
extern "C" int __wrap_posix_memalign(void** o, std::size_t a, std::size_t n) {
    const int rc = __real_posix_memalign(o, a, n);
    if (rc == 0 && o) note(*o);
    return rc;
}
extern "C" void __wrap_free(void* p) {
    if (e20mem::on && p) e20mem::live -= malloc_usable_size(p);
    __real_free(p);
}
#endif

// ---------------------------------------------------------------------------
// Structural digest -- E15..E20's serialisation.
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
            buf_n_ += take; p += take; n -= take;
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
        static const char* d = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i)
            for (int b = 3; b >= 0; --b) {
                const unsigned char byte = static_cast<unsigned char>((h_[i] >> (8 * b)) & 0xffu);
                out.push_back(d[byte >> 4]);
                out.push_back(d[byte & 0xf]);
            }
        return out;
    }
    static bool self_test() {
        Sha256 s; const char* abc = "abc"; s.update(abc, 3);
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
            w[i] = (std::uint32_t(p[4*i]) << 24) | (std::uint32_t(p[4*i+1]) << 16) |
                   (std::uint32_t(p[4*i+2]) << 8) | std::uint32_t(p[4*i+3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            const std::uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],hh=h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h_[0]+=a; h_[1]+=b; h_[2]+=c; h_[3]+=d; h_[4]+=e; h_[5]+=f; h_[6]+=g; h_[7]+=hh;
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

struct Method {
    std::string name;
    std::function<Result(const std::string&)> find;
    std::size_t bytes = 0;
    bool bytes_known = false;
    double build_ms = 0;
};

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        SetProcessAffinityMask(GetCurrentProcess(), 1);
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#elif defined(__linux__)
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 3) throw std::invalid_argument("bench_e21 KIND N [TRANCOCSV]");
        if (!Sha256::self_test()) throw std::runtime_error("SHA-256 self-test failed");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const auto keys = make_data(kind, nkeys, trcsv);

        const std::string frozen_rel = "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
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
        const Mode mode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;
        Config cm_frozen = cm;
        cm_frozen.reuse_group_ids = true;   // the frozen default is R1

        std::vector<Method> methods;
        std::string s_orig_digest, s_frozen_digest;

        // ---- sorted array: a view over the shared sorted keys, no copy ----------
        {
            std::size_t bytes = keys.capacity() * sizeof(Pair);
            for (const auto& kv : keys) bytes += kv.first.capacity() + 1;
            methods.push_back({"sorted_array",
                [&keys](const std::string& k) {
                    auto it = std::lower_bound(keys.begin(), keys.end(), k,
                        [](const Pair& a, const std::string& b) { return compare(a.first, b) < 0; });
                    return it != keys.end() && it->first == k ? Result{true, it->second} : Result{};
                },
                bytes, true, 0.0});
        }

        // ---- ART ---------------------------------------------------------------
        {
            const auto t0 = Clock::now();
            auto art = std::make_shared<e20art::NativeArt>(keys);
            const auto t1 = Clock::now();
            methods.push_back({"ART_armong",
                [art](const std::string& k) { return art->find(k); },
                art->bytes, art->bytes > 0, ms_between(t0, t1)});
        }

        // ---- the two-pass construction (original S) -----------------------------
        {
            Index probe(c);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            const Pipeline plan = make_plan(demand);

            auto* x = new Index(cm);
            const auto t0 = Clock::now();
            x->bulk_load(keys);
            for (std::size_t i = 0; i < x->region_count(); ++i)
                x->rebuild_with_caps(i, x->region(i).all(), plan.s_alloc[i].sg, plan.s_alloc[i].bb,
                                     plan.s_caps[i]);
            const auto t1 = Clock::now();
            s_orig_digest = structure_digest(*x);
            methods.push_back({"S_original_two_pass",
                [x, mode](const std::string& k) { return x->find(k, mode); },
                x->memory(), true, ms_between(t0, t1)});
        }

        // ---- the frozen default: statistics -> plan -> direct build + R1 --------
        {
            auto* x = new Index(cm_frozen);
            const auto t0 = Clock::now();
            Index stats(c);
            const auto demand = stats.demand_profile(keys);
            // The plan is built from the statistics outside the timed span for the
            // build itself, exactly as the two-pass path does: both methods' build
            // times cover construction only, not the allocators.
            std::vector<Alloc> s_alloc;
            std::vector<std::vector<std::uint16_t>> s_caps;
            for (const auto& gs : demand) {
                const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
                const Alloc d = demand_single(gs, budget);
                StrictLog log;
                std::vector<std::uint16_t> caps;
                s_alloc.push_back(demand_strict(gs, d, log, caps));
                s_caps.push_back(std::move(caps));
            }
            x->bulk_load_with_plan(keys, sg_of(s_alloc), bb_of(s_alloc), s_caps);
            const auto t1 = Clock::now();
            s_frozen_digest = structure_digest(*x);
            methods.push_back({"S_frozen_R1",
                [x, mode](const std::string& k) { return x->find(k, mode); },
                x->memory(), true, ms_between(t0, t1)});
        }

        // The two S methods must be the same structure, or comparing their query
        // cost would be comparing two different things.
        if (s_orig_digest != s_frozen_digest)
            throw std::runtime_error("S_frozen_R1 and S_original built different structures");

        // --- build record -------------------------------------------------------
        {
            std::ofstream b("results/main/e21_build.jsonl", std::ios::app);
            b.exceptions(std::ios::badbit | std::ios::failbit);
            b << std::setprecision(10);
            b << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
              << ",\"run_tag\":\"" << run_tag << "\""
              << ",\"config\":{\"model_kind\":\"Context16\",\"block_bytes\":" << c.block_bytes
              << ",\"slots\":" << c.slots << ",\"region_keys\":" << c.region_keys
              << ",\"route\":\"Interval\",\"prefetch\":\"Off\",\"fingerprint\":true"
              << ",\"simd\":true,\"ov_search\":\"Hybrid\",\"ov_threshold\":" << thr
              << ",\"ov_threshold_source\":\"" << frozen_rel << "\""
              << ",\"S_frozen_is_R1\":true,\"query_hit_ratio\":0.5"
              << ",\"miss_key_construction\":\"source key + ~missing~, verified absent before timing\""
              << ",\"query_tuning_this_round\":false}"
              << ",\"s_digest\":\"" << s_frozen_digest << "\""
              << ",\"methods\":[";
            for (std::size_t i = 0; i < methods.size(); ++i) {
                if (i) b << ",";
                b << "{\"name\":\"" << methods[i].name << "\",\"bytes\":" << methods[i].bytes
                  << ",\"bytes_known\":" << (methods[i].bytes_known ? "true" : "false")
                  << ",\"build_ms\":" << methods[i].build_ms << "}";
            }
            b << "]}" << std::endl;
        }

        // --- timed runs, split by query class -----------------------------------
        const std::size_t nq = 131072;
        const int seeds = 3;
        const int rounds = 5;

        std::ofstream csv("results/main/e21_timings.csv", std::ios::app);
        csv.exceptions(std::ios::badbit | std::ios::failbit);
        csv << std::setprecision(12);

        for (int seed = 0; seed < seeds; ++seed) {
            const auto all = make_queries(keys, nq, 2026091700 + seed);
            std::vector<BenchQuery> hits, misses;
            for (const auto& x : all) (x.hit ? hits : misses).push_back(x);

            const auto expected_of = [](const std::vector<BenchQuery>& v) {
                std::uint64_t s = 0;
                for (const auto& x : v) s += x.hit ? x.expected + 0x9e3779b97f4a7c15ULL : 0;
                return s;
            };
            const std::uint64_t want_all = expected_of(all);
            const std::uint64_t want_hit = expected_of(hits);
            const std::uint64_t want_miss = expected_of(misses);

            for (auto& m : methods) {
                for (const auto& x : all) {
                    const Result r = m.find(x.key);
                    if (r.found != x.hit || (r.found && r.value != x.expected))
                        throw std::runtime_error("verification failed: " + m.name);
                }
            }

            struct Class { const char* name; const std::vector<BenchQuery>* q; std::uint64_t want; };
            const std::vector<Class> classes{{"all", &all, want_all},
                                             {"hit", &hits, want_hit},
                                             {"miss", &misses, want_miss}};

            for (int round = 0; round < rounds; ++round) {
                for (const auto& cls : classes) {
                    std::vector<std::size_t> order(methods.size());
                    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
                    std::shuffle(order.begin(), order.end(),
                                 std::mt19937_64(2026091731 + seed * 17 + round + std::strlen(cls.name)));

                    for (std::size_t idx : order) {
                        auto& m = methods[idx];
                        const auto& q = *cls.q;

                        std::uint64_t warm = 0;
                        for (std::size_t i = 0; i < std::min<std::size_t>(20000, q.size()); ++i)
                            warm += checksum(m.find(q[i].key));
                        sink = warm;

                        std::uint64_t sum = 0;
                        const auto t0 = Clock::now();
                        for (const auto& x : q) sum += checksum(m.find(x.key));
                        const auto t1 = Clock::now();

                        if (sum != cls.want) throw std::runtime_error("batch checksum: " + m.name);

                        if (run_tag.compare(0, 6, "warmup") != 0)
                            csv << kind << ',' << keys.size() << ',' << q.size() << ',' << seed << ','
                                << round << ',' << cls.name << ',' << m.name << ','
                                << ns(t0, t1) / double(q.size()) << ',' << m.bytes << ','
                                << (m.bytes_known ? 1 : 0) << ',' << m.build_ms << ',' << run_tag << "\n";
                    }
                }
            }
            std::cerr << "seed " << seed << " done (hits=" << hits.size()
                      << " misses=" << misses.size() << ")\n";
        }
        std::cerr << "COMPLETE " << kind << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

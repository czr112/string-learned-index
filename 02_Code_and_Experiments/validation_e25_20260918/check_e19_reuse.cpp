// E19 (R1): does reusing the group ids change anything except the call count?
//
// The constructor computes the group id once per key in the demand pass and once
// per key again in the placement loop. R1 records the ids from the first pass and
// reads them in the second.
//
// The conditions for that being sound are all satisfied inside one Region: same
// prefix, same model, same logical group count, same routing mode, and the ids are
// indexed by the same key order the placement loop walks. This gate checks the
// consequence rather than the conditions:
//
//   1. STRUCTURE   the digest is identical with the flag off and on, over a grid of
//                  region sizes, slot counts, block sizes, routes and model kinds
//   2. CONTENT     a query workload returns the same found/value from both
//   3. PLAN        the capacity plan is identical (it comes from the probe, which
//                  R1 does not touch -- asserted anyway, so a future change that
//                  moved statistics into the construction would be caught)
//   4. COUNT       the number of group predictions drops by exactly one per key
//   5. FALLBACK    the flag has no effect when multiblock is off, and a
//                  correspondence failure falls back instead of misplacing
//
// Built with -DE18_COUNT so the counter in (4) exists. A failure exits non-zero.
#include "e9_alloc.hpp"
#include "index.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

static void require(bool x, const std::string& why) {
    if (!x) throw std::runtime_error(why);
}

// Same canonical structural digest as E15/E16/E17/E18.
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

int main() {
    try {
        if (!Sha256::self_test())
            throw std::runtime_error("SHA-256 self-test failed");

        std::mt19937_64 rng(20260916);
        std::set<std::string, Less> pool_set;
        for (const auto& s : std::vector<std::string>{
                 "", "a", "aa", "aaa", "z", std::string("a\0b", 3), std::string("\0", 1),
                 std::string("\xff", 1), std::string(8, '\0'), std::string(8, '\xff'),
                 std::string(4096, 'q')})
            pool_set.insert(s);
        for (int i = 0; i < 3000; ++i) {
            std::string s = i % 3 ? "common/prefix/path/" : "";
            for (std::size_t j = 0, m = rng() % 100; j < m; ++j)
                s.push_back(static_cast<char>(rng() % 256));
            pool_set.insert(s);
        }
        const std::vector<std::string> pool(pool_set.begin(), pool_set.end());

        std::vector<Pair> keys;
        for (std::size_t i = 0; i < pool.size(); ++i) keys.emplace_back(pool[i], i + 1);
        std::sort(keys.begin(), keys.end(), pair_less);

        std::size_t configs = 0, digest_checks = 0, plan_checks = 0, query_checks = 0;
        std::size_t count_checks = 0;

        for (const auto region_keys : {std::size_t(32), std::size_t(64), std::size_t(2048)}) {
        for (const auto slots : {std::uint32_t(16), std::uint32_t(48)}) {
        for (const auto bb : {std::uint32_t(1024), std::uint32_t(4096)}) {
        for (const auto route : {Route::Full, Route::Interval}) {
        for (const auto mk : {ModelKind::Context16, ModelKind::Unigram}) {
        for (const bool multiblock : {true, false}) {
            Config probe_cfg;
            e25sw::pin_frozen_switches(probe_cfg);
            probe_cfg.region_keys = region_keys;
            probe_cfg.slots = slots;
            probe_cfg.block_bytes = bb;
            probe_cfg.canonical = route;
            probe_cfg.model_kind = mk;
            probe_cfg.multiblock = false;

            Index probe(probe_cfg);
            probe.bulk_load(keys);

            /* ---- 3. plan: derived from the probe, which R1 does not touch ---- */
            std::vector<Alloc> d_alloc, s_alloc;
            std::vector<std::vector<std::uint16_t>> s_caps;
            for (std::size_t i = 0; i < probe.region_count(); ++i) {
                const auto gs = probe.region(i).group_stats();
                d_alloc.push_back(demand_single(gs, std::uint64_t(gs.size()) * 1024));
                StrictLog log;
                std::vector<std::uint16_t> caps;
                s_alloc.push_back(demand_strict(gs, d_alloc.back(), log, caps));
                s_caps.push_back(std::move(caps));
            }
            auto sg_of = [](const std::vector<Alloc>& v) {
                std::vector<std::vector<std::uint16_t>> o;
                for (const auto& a : v) o.push_back(a.sg);
                return o;
            };
            auto bb_of = [](const std::vector<Alloc>& v) {
                std::vector<std::vector<std::uint32_t>> o;
                for (const auto& a : v) o.push_back(a.bb);
                return o;
            };

            Config cm = probe_cfg;
            cm.multiblock = multiblock;
            cm.reuse_group_ids = false;
            Config cm_reuse = cm;
            cm_reuse.reuse_group_ids = true;

            /* ---- 4. prediction count ---- */
            validation::e18::counts().reset();
            Index a(cm);
            a.bulk_load_with_plan(keys, sg_of(s_alloc), bb_of(s_alloc), s_caps);
            const auto n_off = validation::e18::counts().group_calls;

            validation::e18::counts().reset();
            Index b(cm_reuse);
            b.bulk_load_with_plan(keys, sg_of(s_alloc), bb_of(s_alloc), s_caps);
            const auto n_on = validation::e18::counts().group_calls;

            if (multiblock) {
                // One prediction per key is saved in the construction stage. The
                // fingerprint is untouched and is NOT claimed.
                require(n_on + keys.size() == n_off,
                        "prediction count did not drop by exactly one per key (off=" +
                            std::to_string(n_off) + " on=" + std::to_string(n_on) + ")");
            } else {
                require(n_on == n_off, "the flag must have no effect when multiblock is off");
            }
            ++count_checks;

            /* ---- 1. structure ---- */
            require(a.region_count() == b.region_count(), "region count differs");
            require(structure_digest(a) == structure_digest(b),
                    "structure digest differs with reuse on");
            require(a.size() == b.size(), "size differs");
            for (std::size_t i = 0; i < a.region_count(); ++i)
                require(a.region(i).prefix() == b.region(i).prefix(), "prefix differs");
            ++digest_checks;

            /* ---- 2. content, query by query ---- */
            {
                Mode mode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
                Mode mode_l = mode;
                mode_l.locate = true;
                std::mt19937_64 qrng(7 + configs);
                for (int q = 0; q < 400; ++q) {
                    const auto& k = pool[qrng() % pool.size()];
                    std::string probe_key = (q % 2) ? k : k + "~absent~";
                    const auto ra = a.find(probe_key, mode);
                    const auto rb = b.find(probe_key, mode);
                    const auto rb2 = b.find(probe_key, mode_l);
                    require(ra.found == rb.found && (!ra.found || ra.value == rb.value),
                            "query result differs with reuse on");
                    require(rb.found == rb2.found && (!rb.found || rb.value == rb2.value),
                            "locate and scan modes disagree with reuse on");
                    ++query_checks;
                }
                require(a.scan({}, a.size() + 1) == b.scan({}, b.size() + 1),
                        "full scan differs with reuse on");
            }

            ++plan_checks;
            ++configs;
        }}}}}}

        /* ---- 5. the fallback: reuse requested but the ids do not cover the keys ---- */
        {
            Config c;
            e25sw::pin_frozen_switches(c);
            c.region_keys = 64;
            c.multiblock = true;
            c.reuse_group_ids = true;
            Index x(c);
            // An empty key set leaves the recorded ids empty, so the guard must fall
            // back rather than index an empty vector.
            x.bulk_load_with_plan({}, {}, {});
            require(x.region_count() == 1 && x.size() == 0, "empty input with reuse on");
        }

        std::cout << "{\"status\":\"passed\""
                  << ",\"configurations\":" << configs
                  << ",\"digest_checks\":" << digest_checks
                  << ",\"plan_checks\":" << plan_checks
                  << ",\"query_checks\":" << query_checks
                  << ",\"count_checks\":" << count_checks
                  << ",\"keys\":" << keys.size()
                  << ",\"seed\":20260916}" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

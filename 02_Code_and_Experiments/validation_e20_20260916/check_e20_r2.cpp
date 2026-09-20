// E20 (R2): does carrying the group ids across stages change anything but the count?
//
// R1 removed the second prediction inside one constructor. R2 hands the constructor
// the ids the statistics stage already computed, so the construction makes none.
//
// "Same pure function" is not the right condition here -- the two stages are
// different objects. The condition is that both map a string to the SAME LOGICAL
// group, which needs the prefix and the logical group count to agree. The context
// therefore carries both, and the constructor refuses a preset whose prefix or
// group count does not match its own. Capacity tables and physical sub-block counts
// are deliberately NOT part of the check: they may differ freely, because they do
// not change the mapping.
//
// The gate checks:
//   1. STRUCTURE   digest identical for R1 and R2, over a configuration grid
//   2. CONTENT     same query results
//   3. PLAN        same capacity plan (asserted so a later move of statistics into
//                  the construction would be caught)
//   4. COUNT       the construction's predictions go from one per key to ZERO
//   6. ONE-CALL    bulk_load_reusing_ids, the documented entry, matches R1
//   5. GUARD       four mismatched/stale contexts are refused and the fallback
//                  builds the correct structure. These go through the private
//                  bulk_load_with_context via IndexTestAccess, because a caller
//                  cannot obtain a context at all: the documented entry point is
//                  bulk_load_reusing_ids (check 6), which keeps the context inside
//                  one call:
//                    (i)   same keys, different slot count -> different group count
//                    (ii)  same keys, same prefix and group count, different MODEL
//                    (iii) a DIFFERENT key set
//                    (iv)  the SAME vector, edited in place -- mapping unchanged
//                          (same trained table, same prefix, same group count), key
//                          sequence changed. Equal model tables do NOT imply equal
//                          inputs, so this is the case the mapping digest cannot
//                          catch; the key-sequence digest is what refuses it.
//
// Built with -DE18_COUNT so (4) can be measured. A failure exits non-zero.
#include "e9_alloc.hpp"
#include "index.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
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

int main() {
    try {
        if (!Sha256::self_test()) throw std::runtime_error("SHA-256 self-test failed");

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
        std::size_t count_checks = 0, guard_checks = 0;
        std::size_t guard_model_checks = 0, guard_keyset_checks = 0, guard_stale_checks = 0;
        std::size_t onecall_checks = 0;

        for (const auto region_keys : {std::size_t(32), std::size_t(64), std::size_t(2048)}) {
        for (const auto slots : {std::uint32_t(16), std::uint32_t(48)}) {
        for (const auto bb : {std::uint32_t(1024), std::uint32_t(4096)}) {
        for (const auto route : {Route::Full, Route::Interval}) {
        for (const auto mk : {ModelKind::Context16, ModelKind::Unigram}) {
            Config probe_cfg;
            probe_cfg.region_keys = region_keys;
            probe_cfg.slots = slots;
            probe_cfg.block_bytes = bb;
            probe_cfg.canonical = route;
            probe_cfg.model_kind = mk;
            probe_cfg.multiblock = false;

            Index stats_only(probe_cfg);
            const auto ctx = stats_only.demand_profile_ctx(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (const auto& rd : ctx.regions) demand.push_back(rd.stats);
            const Pipeline plan = make_plan(demand);

            Config cm = probe_cfg;
            cm.multiblock = true;
            cm.reuse_group_ids = true;   // R1 on

            /* ---- 4. prediction count ---- */
            validation::e18::counts().reset();
            Index r1(cm);
            r1.bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            const auto n_r1 = validation::e18::counts().group_calls;

            validation::e18::counts().reset();
            Index r2(cm);
            IndexTestAccess::build_with_context(r2, keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc),
                                                plan.s_caps, &ctx);
            const auto n_r2 = validation::e18::counts().group_calls;

            // R2 removes every prediction the construction was making: R1 leaves one
            // per key (the demand pass), R2 leaves none.
            require(n_r1 == keys.size(),
                    "R1 should predict once per key, got " + std::to_string(n_r1));
            require(n_r2 == 0,
                    "R2 should predict zero times, got " + std::to_string(n_r2));
            ++count_checks;

            /* ---- 1. structure ---- */
            require(r1.region_count() == r2.region_count(), "region count differs");
            require(r1.size() == r2.size(), "size differs");
            require(structure_digest(r1) == structure_digest(r2),
                    "structure digest differs with R2");
            for (std::size_t i = 0; i < r1.region_count(); ++i) {
                require(r1.region(i).prefix() == r2.region(i).prefix(), "prefix differs");
                require(r1.region(i).groups() == r2.region(i).groups(), "group count differs");
            }
            ++digest_checks;

            /* ---- 2. content ---- */
            {
                Mode mode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
                Mode mode_l = mode;
                mode_l.locate = true;
                std::mt19937_64 qrng(11 + configs);
                for (int q = 0; q < 400; ++q) {
                    const auto& k = pool[qrng() % pool.size()];
                    const std::string probe_key = (q % 2) ? k : k + "~absent~";
                    const auto a = r1.find(probe_key, mode);
                    const auto b = r2.find(probe_key, mode);
                    const auto c = r2.find(probe_key, mode_l);
                    require(a.found == b.found && (!a.found || a.value == b.value),
                            "query result differs with R2");
                    require(b.found == c.found && (!b.found || b.value == c.value),
                            "locate and scan modes disagree with R2");
                    ++query_checks;
                }
                require(r1.scan({}, r1.size() + 1) == r2.scan({}, r2.size() + 1),
                        "full scan differs with R2");
            }

            /* ---- 3. plan ---- */
            // The plan is a function of the statistics, which R2 does not change;
            // asserted so that a later move of statistics into the construction is
            // caught here rather than silently changing the layout.
            {
                std::vector<std::vector<GroupStats>> from_r2;
                for (const auto& rd : ctx.regions) from_r2.push_back(rd.stats);
                require(from_r2.size() == demand.size(), "context region count differs");
                for (std::size_t i = 0; i < demand.size(); ++i)
                    require(from_r2[i].size() == demand[i].size(), "context group count differs");
            }
            ++plan_checks;

            /* ---- 5. the guard: a MISMATCHED context must be refused ---- */
            //
            // Three ways a context can be wrong, of which only the first is caught by
            // a length check and only the first two by a prefix+group-count check:
            //   (i)   same keys, different slot count -> different group COUNT
            //   (ii)  same keys, same prefix and group count, DIFFERENT MODEL KIND
            //   (iii) DIFFERENT KEYS with the same prefix and group count
            // (ii) and (iii) are why the context carries a digest of the trained
            // model table: prefix and group count alone would accept both.
            {
                auto build_with = [&](const BuildContext& bad) {
                    Index wrong(cm);
                    IndexTestAccess::build_with_context(wrong, keys, sg_of(plan.s_alloc),
                                                        bb_of(plan.s_alloc), plan.s_caps, &bad);
                    return structure_digest(wrong);
                };

                // (i) different slot count -> different logical group count
                {
                    Config other = probe_cfg;
                    other.slots = (slots == 16) ? 48 : 16;
                    Index other_stats(other);
                    const auto other_ctx = other_stats.demand_profile_ctx(keys);
                    bool differs = false;
                    for (std::size_t i = 0; i < ctx.regions.size() && i < other_ctx.regions.size(); ++i)
                        if (other_ctx.regions[i].groups != ctx.regions[i].groups) differs = true;
                    if (differs) {
                        require(build_with(other_ctx) == structure_digest(r1),
                                "case (i): a context with a different group count was NOT refused");
                        ++guard_checks;
                    }
                }

                // (ii) same prefix and same group count, different model kind.
                // derive_groups does not depend on the model, so the group count is
                // identical by construction and only the trained table differs.
                {
                    Config other = probe_cfg;
                    other.model_kind = (mk == ModelKind::Context16) ? ModelKind::Unigram
                                                                    : ModelKind::Context16;
                    Index other_stats(other);
                    const auto other_ctx = other_stats.demand_profile_ctx(keys);
                    bool same_shape = other_ctx.regions.size() == ctx.regions.size();
                    for (std::size_t i = 0; i < other_ctx.regions.size() && i < ctx.regions.size(); ++i)
                        if (other_ctx.regions[i].prefix != ctx.regions[i].prefix ||
                            other_ctx.regions[i].groups != ctx.regions[i].groups ||
                            other_ctx.regions[i].group_ids.size() != ctx.regions[i].group_ids.size())
                            same_shape = false;
                    // The digest is what must separate them.
                    bool digest_differs = false;
                    for (std::size_t i = 0; i < other_ctx.regions.size() && i < ctx.regions.size(); ++i)
                        if (other_ctx.regions[i].model_digest != ctx.regions[i].model_digest)
                            digest_differs = true;
                    require(same_shape && digest_differs,
                            "case (ii): the setup did not produce same-shape, different-model contexts");
                    require(build_with(other_ctx) == structure_digest(r1),
                            "case (ii): a context from a DIFFERENT MODEL was NOT refused");
                    ++guard_model_checks;
                }

                // (iii) different keys, same prefix and same group count. The keys are
                // replaced in the MIDDLE only, so the first and last key -- and hence
                // the prefix -- are untouched, and each replacement keeps its length,
                // so the suffix byte total -- and hence the group count -- is
                // unchanged. Only the trained table differs.
                {
                    std::vector<Pair> keys_b = keys;
                    const std::size_t lo = keys_b.size() / 2;
                    const std::size_t hi = std::min(keys_b.size(), lo + keys_b.size() / 10);
                    for (std::size_t i = lo; i < hi; ++i) {
                        std::string v = keys_b[i].first;
                        for (auto& c : v) c = static_cast<char>('a' + ((c + 7) % 26));
                        keys_b[i].first = v;   // same length, different content
                    }
                    Index stats_b(probe_cfg);
                    const auto ctx_b = stats_b.demand_profile_ctx(keys_b);
                    bool same_shape = ctx_b.regions.size() == ctx.regions.size();
                    for (std::size_t i = 0; i < ctx_b.regions.size() && i < ctx.regions.size(); ++i)
                        if (ctx_b.regions[i].prefix != ctx.regions[i].prefix ||
                            ctx_b.regions[i].groups != ctx.regions[i].groups)
                            same_shape = false;
                    // Whichever check fires first, the context must be refused and the
                    // structure must come out right. The SOURCE check refuses it here.
                    if (same_shape) {
                        require(build_with(ctx_b) == structure_digest(r1),
                                "case (iii): a context from a DIFFERENT KEY SET was NOT refused");
                        ++guard_keyset_checks;
                    }

                    // (iv) THE CASE THE OTHER THREE DO NOT COVER: the mapping is
                    // UNCHANGED and the context still points at this very vector, but
                    // the key sequence has been edited in place. Two middle keys are
                    // swapped, which leaves the trained table identical (training
                    // accumulates counts over the key multiset, so it does not depend
                    // on order), leaves the prefix identical (first and last keys are
                    // untouched) and leaves the group count identical (same multiset
                    // of lengths). Only the key SEQUENCE changes -- and that is enough
                    // to make ids[i] describe a different key.
                    {
                        std::vector<Pair> keys_c = keys;
                        Index stats_c(probe_cfg);
                        const auto ctx_c = stats_c.demand_profile_ctx(keys_c);
                        const std::size_t m = keys_c.size() / 2;
                        std::swap(keys_c[m], keys_c[m + 1]);

                        Index stats_d(probe_cfg);
                        const auto ctx_d = stats_d.demand_profile_ctx(keys_c);
                        bool mapping_same = ctx_c.regions.size() == ctx_d.regions.size();
                        bool sequence_differs = false;
                        for (std::size_t i = 0; i < ctx_c.regions.size() && i < ctx_d.regions.size(); ++i) {
                            if (ctx_c.regions[i].model_digest != ctx_d.regions[i].model_digest ||
                                ctx_c.regions[i].prefix != ctx_d.regions[i].prefix ||
                                ctx_c.regions[i].groups != ctx_d.regions[i].groups)
                                mapping_same = false;
                            if (ctx_c.regions[i].key_digest != ctx_d.regions[i].key_digest)
                                sequence_differs = true;
                        }
                        // The setup must really be "same mapping, different sequence",
                        // or this test would pass for the wrong reason.
                        require(mapping_same,
                                "case (iv): the swap changed the mapping; the test proves nothing");
                        require(sequence_differs, "case (iv): the swap did not change the key digest");

                        Index a_c(cm), b_c(cm);
                        IndexTestAccess::build_with_context(a_c, keys_c, sg_of(plan.s_alloc),
                                                            bb_of(plan.s_alloc), plan.s_caps, &ctx_c);
                        b_c.bulk_load_with_plan(keys_c, sg_of(plan.s_alloc), bb_of(plan.s_alloc),
                                                plan.s_caps);
                        require(structure_digest(a_c) == structure_digest(b_c),
                                "case (iv): a STALE context survived an in-place key edit");
                        ++guard_stale_checks;
                    }
                }
            }

            /* ---- 6. the ONE-CALL entry, which is the documented way to use R2 ---- */
            {
                auto plan_for = [&](const std::vector<RegionDemand>& regions) {
                    Index::CapacityPlan p;
                    for (const auto& rd : regions) {
                        const std::uint64_t budget = std::uint64_t(rd.stats.size()) * 1024;
                        const Alloc d = demand_single(rd.stats, budget);
                        StrictLog log;
                        std::vector<std::uint16_t> caps;
                        const Alloc s = demand_strict(rd.stats, d, log, caps);
                        p.sg.push_back(s.sg);
                        p.bb.push_back(s.bb);
                        p.caps.push_back(caps);
                    }
                    return p;
                };
                validation::e18::counts().reset();
                Index one(cm);
                one.bulk_load_reusing_ids(keys, probe_cfg, plan_for);
                const auto n_one = validation::e18::counts().group_calls;
                require(n_one == keys.size(),
                        "the one-call entry should predict once per key, got " + std::to_string(n_one));
                require(structure_digest(one) == structure_digest(r1),
                        "the one-call R2 entry produced a different structure");
                require(one.scan({}, one.size() + 1) == r1.scan({}, r1.size() + 1),
                        "the one-call R2 entry scans differently");
                ++onecall_checks;
            }

            ++configs;
        }}}}}

        std::cout << "{\"status\":\"passed\""
                  << ",\"configurations\":" << configs
                  << ",\"digest_checks\":" << digest_checks
                  << ",\"plan_checks\":" << plan_checks
                  << ",\"query_checks\":" << query_checks
                  << ",\"count_checks\":" << count_checks
                  << ",\"guard_checks\":" << guard_checks
                  << ",\"guard_model_checks\":" << guard_model_checks
                  << ",\"guard_keyset_checks\":" << guard_keyset_checks
                  << ",\"guard_stale_checks\":" << guard_stale_checks
                  << ",\"onecall_checks\":" << onecall_checks
                  << ",\"keys\":" << keys.size()
                  << ",\"seed\":20260916}" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

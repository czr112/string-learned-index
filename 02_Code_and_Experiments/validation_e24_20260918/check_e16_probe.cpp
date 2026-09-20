// E16: does the statistics-only path reproduce the probe, field by field?
//
// The construction pipeline builds a "probe" Region per key range purely to call
// group_stats(), and feeds the result to the capacity allocators. E16 replaces
// that with Region::demand_profile, which replays the same placement predicate
// without allocating blocks. This gate is the evidence that the replacement is
// faithful, in three layers:
//
//   1. STATISTICS   every field of every GroupStats, for every group of every
//                   region, against a real probe built with the same Config
//   2. PLAN         the D and S capacity plans are deterministic functions of
//                   those statistics, so they are compared too -- a stats field
//                   that matched but was read differently would show up here
//   3. STRUCTURE    the final D and S indexes built from each JSON-free plan are
//                   compared by structural digest, which is the end-to-end claim
//
// Configurations vary along every axis the statistics depend on: region size,
// slot count, block size, load factor, canonical route, and model kind. The
// invalid case (a multi-block Config, which demand_profile refuses rather than
// approximates) is asserted to throw.
//
// A failure exits non-zero with the reason. The build closure refuses to leave a
// runnable artifact behind if this does not compile.
#include "e9_alloc.hpp"
#include "index.hpp"

#include <array>
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

// Same canonical structural digest as E15's gate, so "the structure agrees"
// means the same thing in both rounds.
struct Fnv {
    std::uint64_t h = 1469598103934665603ULL;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    }
    void u64(std::uint64_t v) { bytes(&v, 8); }
    void str(std::string_view s) { u64(s.size()); bytes(s.data(), s.size()); }
};

static std::uint64_t structure_digest(const Index& idx) {
    Fnv f;
    f.u64(idx.region_count());
    for (std::size_t r = 0; r < idx.region_count(); ++r) {
        const Region& reg = idx.region(r);
        f.u64(r);
        f.str(reg.prefix());
        f.u64(reg.groups());
        f.u64(reg.size());
        f.u64(reg.overflow_count());
        f.u64(reg.block_bytes_total());
        for (auto v : reg.slot_table()) f.u64(v);
        for (auto v : reg.block_table()) f.u64(v);
        for (auto v : reg.offset_table()) f.u64(v);
        for (std::uint32_t g = 0; g < reg.groups(); ++g) {
            const std::size_t nb = reg.subblock_count(g);
            f.u64(nb);
            for (std::size_t i = 0; i < nb; ++i) {
                f.u64(reg.subblock_entries(g, i));
                for (const auto& k : reg.subblock_keys(g, i)) f.str(k);
            }
            for (const auto& e : reg.boundary_dir(g)) { f.str(e.first); f.u64(e.second); }
        }
        for (const auto& kv : reg.all()) { f.str(kv.first); f.u64(kv.second); }
    }
    return f.h;
}

// Field-by-field equality of two GroupStats, with a message naming the offender.
static void same_stats(const GroupStats& a, const GroupStats& b, const std::string& where) {
    auto eq = [&](std::size_t x, std::size_t y, const char* what) {
        if (x != y)
            throw std::runtime_error(where + ": " + what + " differs (" + std::to_string(x) +
                                     " vs " + std::to_string(y) + ")");
    };
    eq(a.group, b.group, "group");
    eq(a.primary_entries, b.primary_entries, "primary_entries");
    eq(a.primary_bytes, b.primary_bytes, "primary_bytes");
    eq(a.overflow_entries, b.overflow_entries, "overflow_entries");
    eq(a.overflow_bytes, b.overflow_bytes, "overflow_bytes");
    eq(a.free_entries, b.free_entries, "free_entries");
    eq(a.free_bytes, b.free_bytes, "free_bytes");
    eq(a.garbage_bytes, b.garbage_bytes, "garbage_bytes");
    eq(a.blocked_slots_only, b.blocked_slots_only, "blocked_slots_only");
    eq(a.blocked_bytes_only, b.blocked_bytes_only, "blocked_bytes_only");
    eq(a.blocked_both, b.blocked_both, "blocked_both");
    eq(a.oversized, b.oversized, "oversized");
    eq(a.fits_now, b.fits_now, "fits_now");
}

int main() {
    try {
        std::mt19937_64 rng(20260916);
        std::set<std::string, Less> pool_set;
        for (const auto& s : std::vector<std::string>{
                 "", "a", "aa", "aaa", "z",
                 std::string("a\0b", 3), std::string("\0", 1), std::string("\xff", 1),
                 std::string(8, '\0'), std::string(8, '\xff'), std::string(4096, 'q')})
            pool_set.insert(s);
        for (int i = 0; i < 2200; ++i) {
            std::string s = i % 3 ? "common/prefix/path/" : "";
            for (std::size_t j = 0, m = rng() % 100; j < m; ++j)
                s.push_back(static_cast<char>(rng() % 256));
            pool_set.insert(s);
        }
        const std::vector<std::string> pool(pool_set.begin(), pool_set.end());

        // Key sets: empty, one key, a prefix-heavy set, and the full pool. The
        // empty and single-key cases are where a group-count derivation most
        // easily diverges.
        std::vector<std::vector<Pair>> sets;
        sets.push_back({});
        sets.push_back({{std::string("solo"), 1}});
        sets.push_back({{std::string(""), 1}, {std::string("a"), 2}});
        {
            std::vector<Pair> v;
            for (std::size_t i = 0; i < pool.size(); i += 7) v.emplace_back(pool[i], i + 1);
            std::sort(v.begin(), v.end(), pair_less);
            sets.push_back(std::move(v));
        }
        {
            std::vector<Pair> v;
            for (std::size_t i = 0; i < pool.size(); ++i) v.emplace_back(pool[i], i + 1);
            std::sort(v.begin(), v.end(), pair_less);
            sets.push_back(std::move(v));
        }

        std::size_t configs = 0, groups_checked = 0, plan_checks = 0, structure_checks = 0;

        for (const auto& region_keys : {std::size_t(32), std::size_t(64), std::size_t(2048)}) {
        for (const auto slots : {std::uint32_t(1), std::uint32_t(2), std::uint32_t(16), std::uint32_t(48)}) {
        for (const auto block_bytes : {std::uint32_t(256), std::uint32_t(1024), std::uint32_t(4096)}) {
        for (const auto load : {0.5, 0.75}) {
        for (const auto route : {Route::Full, Route::Interval, Route::Residual}) {
        for (const auto mk : {ModelKind::Context16, ModelKind::Position16, ModelKind::Unigram}) {
            Config c;
            c.region_keys = region_keys;
            c.slots = slots;
            c.block_bytes = block_bytes;
            c.load = load;
            c.canonical = route;
            c.model_kind = mk;
            c.multiblock = false;

            // A block must be able to hold its own header and slots.
            if (block_bytes <= sizeof(Header) + std::size_t(slots) * sizeof(Entry) + 63) continue;

            for (const auto& keys : sets) {
                Index probe(c);
                probe.bulk_load(keys);

                Index stats_only(c);
                const auto dp = stats_only.demand_profile(keys);

                require(dp.size() == probe.region_count(),
                        "demand_profile region count differs from the probe");

                for (std::size_t i = 0; i < probe.region_count(); ++i) {
                    const auto& real = probe.region(i).group_stats();
                    const auto& fast = dp[i];
                    require(real.size() == fast.size(),
                            "group count differs at region " + std::to_string(i));
                    for (std::size_t g = 0; g < real.size(); ++g) {
                        same_stats(real[g], fast[g],
                                   "region " + std::to_string(i) + " group " + std::to_string(g));
                        ++groups_checked;
                    }
                }
                ++configs;

                // ---- plan layer: the allocators are functions of the statistics ----
                // A statistics field that happened to match but was read differently
                // would not survive this: the plans are compared element by element,
                // together with the allocators' own decision logs.
                std::vector<std::uint64_t> budget;
                std::vector<Alloc> d_real, d_fast, s_real, s_fast;
                std::vector<std::vector<std::uint16_t>> caps_real, caps_fast;
                for (std::size_t i = 0; i < dp.size(); ++i) {
                    const auto& stats_real = probe.region(i).group_stats();
                    const auto& stats_fast = dp[i];
                    budget.push_back(std::uint64_t(stats_real.size()) * 1024);

                    d_real.push_back(demand_single(stats_real, budget[i]));
                    d_fast.push_back(demand_single(stats_fast, budget[i]));
                    require(d_real.back().sg == d_fast.back().sg &&
                                d_real.back().bb == d_fast.back().bb,
                            "D plan differs at region " + std::to_string(i));

                    StrictLog lr, lf;
                    std::vector<std::uint16_t> kr, kf;
                    s_real.push_back(demand_strict(stats_real, d_real.back(), lr, kr));
                    s_fast.push_back(demand_strict(stats_fast, d_fast.back(), lf, kf));
                    require(s_real.back().sg == s_fast.back().sg &&
                                s_real.back().bb == s_fast.back().bb,
                            "S plan differs at region " + std::to_string(i));
                    require(kr == kf, "S sub-block caps differ at region " + std::to_string(i));
                    require(lr.candidates == lf.candidates && lr.funded == lf.funded &&
                                lr.unfunded == lf.unfunded && lr.slack_available == lf.slack_available &&
                                lr.slack_spent == lf.slack_spent && lr.shaved == lf.shaved,
                            "S allocator log differs at region " + std::to_string(i));
                    caps_real.push_back(std::move(kr));
                    caps_fast.push_back(std::move(kf));
                    ++plan_checks;
                }

                // ---- structure layer: the final indexes must agree ----
                Config cm = c;
                cm.multiblock = true;
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
                {
                    Index a(cm), b(cm);
                    a.bulk_load_with_plan(keys, sg_of(d_real), bb_of(d_real));
                    b.bulk_load_with_plan(keys, sg_of(d_fast), bb_of(d_fast));
                    require(structure_digest(a) == structure_digest(b),
                            "D structure differs between the two statistics sources");
                    ++structure_checks;
                }
                {
                    Index a(cm), b(cm);
                    a.bulk_load_with_plan(keys, sg_of(s_real), bb_of(s_real), caps_real);
                    b.bulk_load_with_plan(keys, sg_of(s_fast), bb_of(s_fast), caps_fast);
                    require(structure_digest(a) == structure_digest(b),
                            "S structure differs between the two statistics sources");
                    ++structure_checks;
                }
            }
        }}}}}}

        // ---- the refused case ----------------------------------------------
        {
            Config c;
            c.multiblock = true;
            bool threw = false;
            try { Index x(c); x.demand_profile({}); }
            catch (const std::invalid_argument&) { threw = true; }
            require(threw, "a multi-block Config must be refused, not approximated");
            // and the single-block path must accept the same shape of input
            Config c2 = c;
            c2.multiblock = false;
            Index y(c2);
            require(y.demand_profile({}).size() == 1, "empty input yields one region");
        }

        std::cout << "{\"status\":\"passed\""
                  << ",\"configurations\":" << configs
                  << ",\"groups_compared\":" << groups_checked
                  << ",\"plan_checks\":" << plan_checks
                  << ",\"structure_checks\":" << structure_checks
                  << ",\"seed\":20260916}" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

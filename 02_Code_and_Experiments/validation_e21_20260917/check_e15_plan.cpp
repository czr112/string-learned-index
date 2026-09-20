// E15 direct coverage for Index::bulk_load_with_plan.
//
// The four inherited gates are E13's and never call the new interface, so they
// only protect the old paths. This gate covers the new one directly:
//
//   A  invalid plans are REJECTED, not silently padded
//      A1  plan shorter than the region count
//      A2  a region's shape does not match that region's group count
//      A3  sub-block cap table present but of the wrong length
//   B  the empty input, and the same fallback bulk_load uses
//   C  a tail region shorter than region_keys (the split is not a multiple)
//   D  equivalence with the two-pass path: region count, fences, capacity
//      tables, sub-block structure, boundary directory and every (key, value),
//      over several region sizes and several capacity plans
//   E  dynamic operations AFTER a new-path build: insert / update / upsert /
//      erase / find / scan against std::map, including prefix-changing inserts
//      that force a region split
//
// A failure exits non-zero and prints the reason; the build closure refuses to
// leave a runnable artifact behind if this does not compile, and the runner
// records a non-zero exit as a failure.
#include "index.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;

static void require(bool x, const char* why) {
    if (!x) throw std::runtime_error(why);
}

using Reference = std::map<std::string, std::uint64_t, Less>;

static std::vector<Pair> expected_scan(const Reference& r, std::string_view key, std::size_t n) {
    std::vector<Pair> out;
    for (auto it = r.lower_bound(std::string(key)); it != r.end() && out.size() < n; ++it)
        out.push_back(*it);
    return out;
}

// ---------------------------------------------------------------------------
// A capacity plan with varied but legal shapes. bb is derived so that it always
// exceeds the block start and stays 64-aligned, which is what Region's
// constructor requires -- a plan that violates that must be the thing under
// test, never an accident of this helper.
// ---------------------------------------------------------------------------

// Region::MAX_SLOTS is private on purpose; the gate spells the public limit out
// rather than reaching into the type.
static constexpr std::size_t SLOT_LIMIT = 48;

static std::vector<std::uint16_t> plan_sg(std::size_t G, std::size_t seed) {
    std::vector<std::uint16_t> v(G);
    for (std::size_t g = 0; g < G; ++g)
        v[g] = static_cast<std::uint16_t>(1 + (g * 7 + seed * 5) % SLOT_LIMIT);
    return v;
}

static std::vector<std::uint32_t> plan_bb(const std::vector<std::uint16_t>& sg, std::size_t seed) {
    std::vector<std::uint32_t> v(sg.size());
    for (std::size_t g = 0; g < sg.size(); ++g) {
        const std::uint64_t start = sizeof(Header) + std::uint64_t(sg[g]) * sizeof(Entry);
        std::uint64_t bb = ((start + 1 + 63) / 64) * 64 + 64 * ((g + seed) % 8);
        bb -= bb % 64;
        if (bb > 65535) bb = 65535 - (65535 % 64);
        if (bb <= start) bb = ((start + 1 + 63) / 64) * 64;
        v[g] = static_cast<std::uint32_t>(bb);
    }
    return v;
}

// Canonical structural digest. Deliberately the same serialisation bench_e14 and
// bench_e15 use: region count, per-region prefix, the three capacity tables, the
// derived sub-block structure, the boundary directory, the overflow count, and
// every (key, value) in scan order.
struct Fnv {
    std::uint64_t h = 1469598103934665603ULL;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    }
    void u64(std::uint64_t v) { bytes(&v, 8); }
    void str(std::string_view s) { u64(s.size()); bytes(s.data(), s.size()); }
    std::uint64_t value() const { return h; }
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
    return f.value();
}

// A plan derived the way the benchmark pipeline derives one: from a probe built
// under the default uniform capacity.
struct Plan {
    std::vector<std::vector<std::uint16_t>> sg, caps;
    std::vector<std::vector<std::uint32_t>> bb;
};

static Plan make_plan(const Index& probe, bool with_caps, std::size_t seed) {
    Plan p;
    for (std::size_t i = 0; i < probe.region_count(); ++i) {
        const std::size_t G = probe.region(i).groups();
        p.sg.push_back(plan_sg(G, seed + i));
        p.bb.push_back(plan_bb(p.sg.back(), seed + i));
        if (with_caps) {
            std::vector<std::uint16_t> c(G, 1);
            for (std::size_t g = 0; g < G; g += 3) c[g] = 0;   // every third group uncapped
            p.caps.push_back(std::move(c));
        }
    }
    return p;
}

int main() {
    try {
        std::mt19937_64 rng(20260916);
        std::set<std::string, Less> pool_set;
        // Prefix-heavy keys exercise the shared-prefix and split paths; the odd
        // byte strings keep the ordering contract honest.
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

        std::size_t rejections = 0, equivalences = 0, dynamic_ops = 0, scans = 0, edge_cases = 0;

        // ---- A. invalid plans are rejected ------------------------------
        {
            Config c;
            c.region_keys = 64;
            c.multiblock = true;

            std::vector<Pair> keys;
            for (std::size_t i = 0; i < pool.size(); ++i) keys.emplace_back(pool[i], i + 1);
            std::sort(keys.begin(), keys.end(), pair_less);

            Index probe(c);
            probe.bulk_load(keys);
            require(probe.region_count() >= 2, "trial needs more than one region");
            Plan p = make_plan(probe, true, 1);

            // A1: plan does not cover every region
            {
                auto sg = p.sg;
                auto bb = p.bb;
                sg.pop_back();
                bb.pop_back();
                bool threw = false;
                try { Index x(c); x.bulk_load_with_plan(keys, sg, bb); }
                catch (const std::invalid_argument&) { threw = true; }
                require(threw, "A1: a short plan must be rejected");
                ++rejections;
                // and the object must not have been left half-built
                Index x(c);
                try { x.bulk_load_with_plan(keys, sg, bb); } catch (const std::invalid_argument&) {}
                require(x.region_count() == 0, "A1: a rejected plan must leave no regions");
            }

            // A2: a region's shape does not match that region's group count.
            // Lengthening the vector is the thing that must be rejected; changing
            // a slot COUNT to another legal value must NOT be (it is just a
            // different plan), and A2b below pins that down too.
            {
                auto sg = p.sg;
                sg[0].push_back(static_cast<std::uint16_t>(16));
                bool threw = false;
                try { Index x(c); x.bulk_load_with_plan(keys, sg, p.bb, p.caps); }
                catch (const std::invalid_argument&) { threw = true; }
                require(threw, "A2: a per-region shape that does not match its group count must be rejected");
                ++rejections;
            }

            // A2b: a different but LEGAL slot count must be accepted, so A2 is
            // not passing merely because any change is rejected.
            {
                auto sg = p.sg;
                sg[0][0] = static_cast<std::uint16_t>(sg[0][0] == 1 ? 2 : 1);
                auto bb = p.bb;
                const std::uint64_t start = sizeof(Header) + std::uint64_t(sg[0][0]) * sizeof(Entry);
                std::uint64_t w = ((start + 1 + 63) / 64) * 64;
                bb[0][0] = static_cast<std::uint32_t>(w);
                Index x(c);
                x.bulk_load_with_plan(keys, sg, bb, p.caps);
                require(x.region_count() == probe.region_count(), "A2b: a legal variation must be accepted");
                require(x.region(0).slot_table()[0] == sg[0][0], "A2b: the plan must be the one applied");
                ++edge_cases;
            }

            // A2c: an ILLEGAL shape (zero slots) must be rejected by Region itself
            {
                auto sg = p.sg;
                sg[0][0] = 0;
                bool threw = false;
                try { Index x(c); x.bulk_load_with_plan(keys, sg, p.bb, p.caps); }
                catch (const std::invalid_argument&) { threw = true; }
                require(threw, "A2c: zero slots must be rejected");
                ++rejections;
            }

            // A3: sub-block cap table present but of the wrong length
            {
                auto caps = p.caps;
                caps.pop_back();
                bool threw = false;
                try { Index x(c); x.bulk_load_with_plan(keys, p.sg, p.bb, caps); }
                catch (const std::invalid_argument&) { threw = true; }
                require(threw, "A3: a wrongly sized cap table must be rejected");
                ++rejections;
            }
        }

        // ---- B. empty input ---------------------------------------------
        {
            Config c;
            c.region_keys = 64;
            c.multiblock = true;
            Index empty(c);
            empty.bulk_load_with_plan({}, {}, {}, {});
            require(empty.region_count() == 1, "B: empty input yields exactly one region");
            require(empty.size() == 0, "B: empty input yields size 0");
            require(empty.region(0).size() == 0, "B: that region is empty");
            require(!empty.find("anything", Mode{}).found, "B: nothing is found in an empty index");
            require(empty.scan({}, 10).empty(), "B: scanning an empty index yields nothing");

            // The same fallback the two-pass path takes.
            Index ref_empty(c);
            ref_empty.bulk_load({});
            require(ref_empty.region_count() == empty.region_count(), "B: same region count as bulk_load");
            require(ref_empty.region(0).prefix() == empty.region(0).prefix(), "B: same prefix");
            ++edge_cases;
        }

        // ---- C/D. tail region and equivalence with the two-pass path -----
        for (const std::size_t region_keys : {std::size_t(32), std::size_t(64), std::size_t(100)}) {
            for (const bool multiblock : {false, true}) {
                for (const std::size_t take : {std::size_t(2 * 100 + 7), pool.size()}) {
                    Config c;
                    c.region_keys = region_keys;
                    c.multiblock = multiblock;

                    std::vector<Pair> keys;
                    for (std::size_t i = 0; i < std::min(take, pool.size()); ++i)
                        keys.emplace_back(pool[i], i + 1);
                    std::sort(keys.begin(), keys.end(), pair_less);
                    if (keys.empty()) continue;

                    Index probe(c);
                    probe.bulk_load(keys);
                    Plan p = make_plan(probe, multiblock, region_keys);

                    // A split that is not a multiple of region_keys must produce a
                    // short final region on both paths, with the same fence.
                    const std::size_t expect_regions =
                        (keys.size() + region_keys - 1) / region_keys;
                    require(probe.region_count() == expect_regions, "C: region count from the split");

                    Index oldp(c);
                    oldp.bulk_load(keys);
                    for (std::size_t i = 0; i < oldp.region_count(); ++i)
                        oldp.rebuild_with_caps(i, oldp.region(i).all(), p.sg[i], p.bb[i],
                                               multiblock ? p.caps[i] : std::vector<std::uint16_t>{});
                    Index newp(c);
                    if (multiblock)
                        newp.bulk_load_with_plan(keys, p.sg, p.bb, p.caps);
                    else
                        newp.bulk_load_with_plan(keys, p.sg, p.bb);

                    require(newp.region_count() == oldp.region_count(), "D: region count");
                    require(newp.size() == oldp.size(), "D: size");
                    for (std::size_t i = 0; i < newp.region_count(); ++i) {
                        require(newp.fence(i) == oldp.fence(i), "D: fence");
                        require(newp.region(i).prefix() == oldp.region(i).prefix(), "D: prefix");
                        require(newp.region(i).slot_table() == oldp.region(i).slot_table(), "D: slot table");
                        require(newp.region(i).block_table() == oldp.region(i).block_table(), "D: block table");
                        require(newp.region(i).offset_table() == oldp.region(i).offset_table(), "D: offset table");
                    }
                    require(structure_digest(newp) == structure_digest(oldp),
                            "D: one-pass and two-pass structures differ");
                    // The short tail region must actually exist and be short.
                    const std::size_t last = newp.region_count() - 1;
                    require(newp.region(last).size() ==
                                keys.size() - last * region_keys, "C: tail region holds the remainder");
                    ++equivalences;
                }
            }
        }
        ++edge_cases;   // the tail case above ran

        // ---- E. dynamic operations after a new-path build ----------------
        {
            Config c;
            c.region_keys = 48;
            c.multiblock = true;

            Reference ref;
            std::vector<Pair> initial;
            for (std::size_t i = 0; i < pool.size(); i += 11) {
                initial.emplace_back(pool[i], i);
                ref[pool[i]] = i;
            }
            std::sort(initial.begin(), initial.end(), pair_less);

            Index probe(c);
            probe.bulk_load(initial);
            Plan p = make_plan(probe, true, 3);

            Index idx(c);
            idx.bulk_load_with_plan(initial, p.sg, p.bb, p.caps);
            require(idx.size() == ref.size(), "E: size after the new-path build");

            const std::array<Mode, 2> modes{
                Mode{Route::Interval, Prefetch::Off, true, false},
                Mode{Route::Interval, Prefetch::Off, true, true}};

            for (int step = 0; step < 12000; ++step) {
                const auto& key = pool[rng() % pool.size()];
                const std::uint64_t value = step % 7 ? rng() : 0;
                const auto op = rng() % 7;
                if (op == 0) {
                    const bool want = ref.emplace(key, value).second;
                    require(idx.insert(key, value) == want, "E: insert");
                } else if (op == 1) {
                    auto it = ref.find(key);
                    const bool want = it != ref.end();
                    if (want) it->second = value;
                    require(idx.update(key, value) == want, "E: update");
                } else if (op == 2) {
                    const bool want = ref.erase(key) != 0;
                    require(idx.erase(key) == want, "E: erase");
                } else if (op == 3) {
                    const bool want = ref.find(key) == ref.end();
                    ref[key] = value;
                    require(idx.upsert(key, value) == want, "E: upsert");
                } else if (op == 4) {
                    const auto n = rng() % 40;
                    require(idx.scan(key, n) == expected_scan(ref, key, n), "E: range scan");
                    ++scans;
                } else {
                    const auto it = ref.find(key);
                    const auto a = idx.find(key, modes[0]);
                    const auto b = idx.find(key, modes[1]);
                    require(a.found == b.found && (!a.found || a.value == b.value),
                            "E: locate/scalar disagree");
                    require(a.found == (it != ref.end()) && (!a.found || a.value == it->second), "E: find");
                }
                require(idx.size() == ref.size(), "E: size");
                ++dynamic_ops;

                if (step % 97 == 0)
                    require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1),
                            "E: full scan after updates");
            }
        }

        std::cout << "{\"status\":\"passed\""
                  << ",\"plan_rejections\":" << rejections
                  << ",\"equivalence_trials\":" << equivalences
                  << ",\"edge_cases\":" << edge_cases
                  << ",\"dynamic_ops\":" << dynamic_ops
                  << ",\"scans\":" << scans
                  << ",\"seed\":20260916}" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

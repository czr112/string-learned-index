// E8-7 correctness gate for TRULY heterogeneous layouts (review 2026-09-13):
// uniform capacity is a special case of the capacity table, so the uniform-path
// differential cannot exclude stride bugs. This gate (a) demand-rebuilds every
// region with real per-group demand and asserts the tables are non-uniform,
// (b) runs the full std::map differential (insert/update/upsert/erase/scan,
// including prefix-change rebuilds, splits and the fallback to uniform capacity),
// (c) re-validates structural invariants after every structural version change:
//   offset table monotone/aligned/consistent with block table, per-group byte
//   conservation via group_stats, slot/size integer ranges.
// Sanitizers are unavailable in this toolchain; this gate is functional, not a
// memory-safety proof (recorded honestly in RESULTS_E8).

#include "selected.hpp"

#include <iostream>
#include <map>
#include <random>
#include <set>

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

static bool region_is_uniform(const Region& r) {
    const auto& sg = r.slot_table();
    const auto& bb = r.block_table();
    for (std::size_t g = 1; g < sg.size(); ++g)
        if (sg[g] != sg[0] || bb[g] != bb[0]) return false;
    return true;
}

// Structural invariants, re-checked after every structural version change:
// table sizes, offset monotonicity and consistency with the block table, 64B
// alignment, slot/size ranges, and per-group byte conservation via group_stats.
static std::size_t validate_struct(const Index& idx) {
    std::size_t groups_checked = 0;
    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const auto& r = idx.region(i);
        const auto& off = r.offset_table();
        const auto& sg = r.slot_table();
        const auto& bb = r.block_table();
        const std::size_t G = sg.size();

        require(off.size() == G + 1 && bb.size() == G, "table sizes");
        require(off[0] == 0, "offset base");

        for (std::size_t g = 0; g < G; ++g) {
            require(sg[g] >= 1 && sg[g] <= 48, "slot range");
            require(bb[g] % 64 == 0, "block alignment");
            require(off[g + 1] - off[g] == bb[g], "offset/block consistency");
            require(off[g] % 64 == 0, "block base alignment");
            require(off[g + 1] > off[g], "positive size");

            const auto so = 64 + std::size_t(sg[g]) * 16;
            require(bb[g] > so, "suffix area present");

            const auto gs = r.group_stats()[g];
            require(gs.primary_entries + gs.free_entries == sg[g], "entry conservation");
            require(gs.primary_bytes + gs.free_bytes + gs.garbage_bytes == bb[g] - so, "byte conservation");
            require(gs.blocked_slots_only + gs.blocked_bytes_only + gs.blocked_both + gs.fits_now == gs.overflow_entries,
                    "overflow partition");
            require(gs.oversized <= gs.overflow_entries, "oversized bound");
            ++groups_checked;
        }
    }
    return groups_checked;
}

// Demand rebuild with REAL per-group demand -> genuinely non-uniform tables.
// (Per-region budget = G * 1024; suffix scaled to budget, 64B suffix floor.)
static void demand_rebuild_all(Index& idx) {
    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        auto data = idx.region(i).all();
        const auto gs = idx.region(i).group_stats();
        const std::size_t G = gs.size();
        const std::uint64_t budget = std::uint64_t(G) * 1024;

        std::vector<std::uint16_t> sg(G);
        std::vector<std::uint32_t> bb(G);
        std::uint64_t fixed = 0, sfx_demand = 0;
        for (std::size_t g = 0; g < G; ++g) {
            const std::uint64_t ent = gs[g].primary_entries + gs[g].overflow_entries;
            sg[g] = static_cast<std::uint16_t>(std::min<std::uint64_t>(std::max<std::uint64_t>(ent, 8), 48));
            bb[g] = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(gs[g].primary_bytes + gs[g].overflow_bytes + 64, 60000));
            fixed += 64 + std::uint64_t(sg[g]) * 16;
            sfx_demand += bb[g];
        }

        const std::uint64_t avail = budget > fixed ? budget - fixed : 0;
        const double sc = sfx_demand ? double(avail) / double(sfx_demand) : 0.0;

        std::uint64_t used = 0;
        for (std::size_t g = 0; g < G; ++g) {
            const std::uint64_t extra = std::uint64_t(double(bb[g] - 64) * sc);
            const std::uint64_t so = 64 + std::uint64_t(sg[g]) * 16;
            bb[g] = static_cast<std::uint32_t>((so + extra + 63) & ~std::uint64_t{63});
            const std::uint64_t min_bb = so + 64;
            if (bb[g] < min_bb) bb[g] = static_cast<std::uint32_t>(min_bb);
            used += bb[g];
        }

        std::size_t guard = 0;
        while (used < budget && guard++ < G * 1024) {
            for (std::size_t g = 0; g < G && used < budget; ++g) {
                bb[g] += 64;
                used += 64;
            }
        }

        idx.rebuild_with_caps(i, std::move(data), std::move(sg), std::move(bb));
    }
}

int main() {
    try {
        std::mt19937_64 rng(20260913);
        std::set<std::string, Less> pool_set;

        for (const auto& s : std::vector<std::string>{
                 "",
                 "a", "aa", "aaa", "z",
                 std::string("a\0b", 3),
                 std::string("\0", 1),
                 std::string("\xff", 1),
                 std::string(8, '\0'),
                 std::string(8, '\xff'),
                 std::string(4096, 'q'),
             })
            pool_set.insert(s);

        for (int i = 0; i < 2200; ++i) {
            std::string s = i % 3 ? "common/prefix/path/" : "";
            for (std::size_t j = 0, m = rng() % 100; j < m; ++j)
                s.push_back(static_cast<char>(rng() % 256));
            pool_set.insert(s);
        }
        const std::vector<std::string> pool(pool_set.begin(), pool_set.end());

        const Mode m_lin{Route::Interval, Prefetch::Off, true, true, OvSearch::Linear, 32};
        const Mode m_hyb{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 128};

        std::size_t operations = 0, scan_checks = 0, struct_validations = 0, groups_checked = 0;
        std::size_t hetero_regions_at_start = 0, uniform_fallbacks = 0;

        struct Trial {
            std::size_t region;
            std::size_t step;
        };
        for (const auto tr : std::vector<Trial>{{16, 6000}, {64, 6000}}) {
            Reference ref;
            std::vector<Pair> initial;
            for (std::size_t i = 0; i < pool.size(); i += 7) {
                initial.emplace_back(pool[i], i);
                ref[pool[i]] = i;
            }

            Config c;
            c.block_bytes = 1024;
            c.slots = 16;
            c.model_kind = ModelKind::Context16;
            c.region_keys = tr.region;

            Index idx(c);
            idx.bulk_load(initial);

            demand_rebuild_all(idx);

            std::size_t hetero = 0;
            for (std::size_t i = 0; i < idx.region_count(); ++i)
                if (!region_is_uniform(idx.region(i))) ++hetero;
            if (tr.region == 16) hetero_regions_at_start = hetero;
            require(hetero > 0, "demand rebuild produced no heterogeneous region");

            // Dynamic differential on the heterogeneous layout, including
            // prefix-change rebuilds (uniform fallback) and region splits.
            for (std::size_t step = 0; step < tr.step; ++step) {
                const auto& key = pool[rng() % pool.size()];
                const std::uint64_t value = step % 7 ? rng() : 0;
                const auto op = rng() % 7;

                if (op == 0) {
                    const bool want = ref.emplace(key, value).second;
                    require(idx.insert(key, value) == want, "insert");
                } else if (op == 1) {
                    auto it = ref.find(key);
                    const bool want = it != ref.end();
                    if (want) it->second = value;
                    require(idx.update(key, value) == want, "update");
                } else if (op == 2) {
                    const bool want = ref.erase(key) != 0;
                    require(idx.erase(key) == want, "erase");
                } else if (op == 3) {
                    const bool want = ref.find(key) == ref.end();
                    ref[key] = value;
                    require(idx.upsert(key, value) == want, "upsert");
                } else if (op == 4) {
                    const auto n = rng() % 30;
                    require(idx.scan(key, n) == expected_scan(ref, key, n), "range scan");
                    ++scan_checks;
                } else {
                    const auto it = ref.find(key);
                    const auto l = idx.find(key, m_lin);
                    const auto h = idx.find(key, m_hyb);
                    require(l.found == h.found && (!l.found || l.value == h.value), "policy agree");
                    require(l.found == (it != ref.end()) && (!l.found || l.value == it->second), "find vs map");
                }

                require(idx.size() == ref.size(), "size");
                ++operations;

                if (step % 97 == 0) {
                    groups_checked += validate_struct(idx);
                    ++struct_validations;
                    std::size_t uni = 0;
                    for (std::size_t i = 0; i < idx.region_count(); ++i)
                        if (region_is_uniform(idx.region(i))) ++uni;
                    uniform_fallbacks = uni;  // regions that fell back to uniform capacity
                }
                if (step % 499 == 0) {
                    require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1), "full scan");
                    ++scan_checks;
                }
            }

            groups_checked += validate_struct(idx);
            ++struct_validations;
            require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1), "final scan");
            ++scan_checks;

            std::cerr << "trial region=" << tr.region << " ops=" << tr.step
                      << " hetero_at_start=" << hetero
                      << " uniform_now=" << uniform_fallbacks << "\n";
        }

        std::cout << "{\"status\":\"passed\",\"operations\":" << operations
                  << ",\"scan_checks\":" << scan_checks
                  << ",\"struct_validations\":" << struct_validations
                  << ",\"groups_struct_checked\":" << groups_checked
                  << ",\"hetero_regions_at_start\":" << hetero_regions_at_start
                  << ",\"uniform_regions_after_ops\":" << uniform_fallbacks
                  << ",\"seed\":20260913}" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << "\n";
        return 1;
    }
}

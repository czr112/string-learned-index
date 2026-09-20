// E9 correctness gate for multi-block groups (2026-09-15).
//
// Multi-block is a capacity mode, not a new grouping: the model, the logical
// group id g(s) and the overflow policy are untouched, so the differential here
// checks the same semantics the E5 gate checks, only on layouts where groups
// span several sub-blocks. Specifically:
//
//   (a) build a genuinely multi-block layout and assert nb > 1 really occurs;
//   (b) validate sub-block structure: rank order M1 (every key of sub-block i is
//       smaller than every key of sub-block i+1, which range scan depends on),
//       sortedness inside a sub-block, the derived count, per-sub-block byte
//       conservation, and agreement with the group-level totals;
//   (c) run the full std::map differential on that layout, including
//       prefix-change rebuilds and region splits;
//   (d) assert that single-sub-block and multi-sub-block groups coexist, so a
//       mixed layout is exercised rather than assumed;
//   (e) scan from a start key that falls inside a MIDDLE sub-block.
//
// Sanitizers are unavailable in this toolchain; this gate is functional, not a
// memory-safety proof (recorded honestly in RESULTS_E9).

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

// (b) Structural invariants of a multi-block layout, re-checked after every
// structural version change.
static std::size_t validate_multiblock(const Index& idx, std::size_t& multi_groups,
                                       std::size_t& single_groups, std::size_t& max_subblocks) {
    std::size_t groups_checked = 0;

    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const auto& r = idx.region(i);
        const auto& off = r.offset_table();
        const auto& sg = r.slot_table();
        const auto& bb = r.block_table();
        const std::size_t G = sg.size();
        const auto gs_all = r.group_stats();

        require(off.size() == G + 1 && bb.size() == G, "table sizes");
        require(off[0] == 0, "offset base");

        for (std::size_t g = 0; g < G; ++g) {
            require(sg[g] >= 1 && sg[g] <= 48, "slot range");
            require(bb[g] % 64 == 0, "block alignment");
            require(off[g] % 64 == 0, "group base alignment");

            const std::size_t span = off[g + 1] - off[g];
            require(span % bb[g] == 0, "sub-blocks must be equal-sized");
            const std::size_t nb = span / bb[g];
            require(nb >= 1, "sub-block count");
            require(r.subblock_count(g) == nb, "derived sub-block count");

            const std::size_t start = 64 + std::size_t(sg[g]) * 16;
            require(bb[g] > start, "suffix area present");

            std::size_t entries_sum = 0, free_sum = 0;

            for (std::size_t k = 0; k < nb; ++k) {
                const auto st = r.subblock_stats(g, k);
                require(st.entries <= sg[g], "sub-block slot bound");
                require(st.entries == r.subblock_entries(g, k), "sub-block entry count");
                require(st.bytes + st.free_bytes + st.garbage_bytes == bb[g] - start,
                        "sub-block byte conservation");

                const auto keys = r.subblock_keys(g, k);
                require(keys.size() == st.entries, "sub-block key count");
                for (std::size_t j = 1; j < keys.size(); ++j)
                    require(compare(keys[j - 1], keys[j]) < 0, "sub-block sorted");

                entries_sum += st.entries;
                free_sum += sg[g] - st.entries;
            }

            // M1: rank order across sub-blocks. Range scan relies on this.
            for (std::size_t k = 1; k < nb; ++k) {
                const auto prev = r.subblock_keys(g, k - 1);
                const auto cur = r.subblock_keys(g, k);
                if (!prev.empty() && !cur.empty())
                    require(compare(prev.back(), cur.front()) < 0, "sub-block rank order M1");
            }

            const auto& gs = gs_all[g];
            require(gs.primary_entries == entries_sum, "group entry total");
            require(gs.free_entries == free_sum, "group free-slot total");

            if (nb > 1) ++multi_groups;
            else ++single_groups;
            max_subblocks = std::max(max_subblocks, nb);
            ++groups_checked;
        }
    }

    return groups_checked;
}

// Rebuild every region from explicit caps while multi-block mode derives the
// sub-block count from demand.
static void rebuild_all(Index& idx, std::uint16_t slots, std::uint32_t block_bytes) {
    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const std::size_t G = idx.region(i).slot_table().size();
        auto data = idx.region(i).all();
        idx.rebuild_with_caps(i, std::move(data),
                              std::vector<std::uint16_t>(G, slots),
                              std::vector<std::uint32_t>(G, block_bytes));
    }
}

int main() {
    try {
        std::mt19937_64 rng(20260915);
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
        // Scalar tag filter. A sub-block holds up to 48 slots, so the SIMD path
        // needs three 16-tag batches; comparing against scalar is what shows the
        // batches beyond the first 16 are really covered.
        const Mode m_scalar{Route::Interval, Prefetch::Off, true, false, OvSearch::Linear, 32};

        std::size_t operations = 0, scan_checks = 0, struct_validations = 0;
        std::size_t groups_checked = 0, multi_groups = 0, single_groups = 0;
        std::size_t max_subblocks = 0, middle_scan_checks = 0;
        std::size_t ops_multiblock_groups = 0, ops_singleblock_groups = 0;
        std::size_t wide_subblocks = 0, wide_checks = 0, empty_block_cases = 0, compactions_seen = 0;
        std::size_t cap_single = 0, cap_open = 0, demand_capped = 0;

        struct Trial {
            std::size_t region;
            std::size_t step;
        };
        for (const auto tr : std::vector<Trial>{{64, 6000}, {32, 6000}}) {
            Reference ref;
            std::vector<Pair> initial;
            for (std::size_t i = 0; i < pool.size(); ++i) {
                initial.emplace_back(pool[i], i);
                ref[pool[i]] = i;
            }

            Config c;
            c.block_bytes = 2048;
            c.slots = 48;
            c.model_kind = ModelKind::Context16;
            c.region_keys = tr.region;
            c.multiblock = true;

            Index idx(c);
            idx.bulk_load(initial);
            rebuild_all(idx, 48, 2048);

            std::size_t multi = 0, single = 0, deepest = 0;
            const auto checked = validate_multiblock(idx, multi, single, deepest);
            groups_checked += checked;
            ++struct_validations;
            ops_multiblock_groups += multi;
            ops_singleblock_groups += single;
            max_subblocks = std::max(max_subblocks, deepest);

            require(multi > 0, "no multi-block group was produced");

            // (e) Start a scan from a key that lives in a later sub-block, so the
            // cursor has to skip whole sub-blocks before emitting anything.
            for (std::size_t i = 0; i < idx.region_count() && middle_scan_checks < 60; ++i) {
                const auto& r = idx.region(i);
                for (std::uint32_t g = 0; g < r.groups() && middle_scan_checks < 60; ++g) {
                    const std::size_t nb = r.subblock_count(g);
                    if (nb < 2) continue;

                    const auto keys = r.subblock_keys(g, nb / 2);   // a later sub-block
                    if (keys.empty()) continue;

                    // subblock_keys returns suffixes; rebuild the full key so it
                    // can be used as a scan start.
                    const auto start = r.prefix() + keys[keys.size() / 2];
                    require(idx.scan(start, 25) == expected_scan(ref, start, 25),
                            "scan from later sub-block");
                    ++middle_scan_checks;
                    ++scan_checks;
                }
            }

            // (f) Sub-blocks holding more than 16 entries: look up every one of
            // their keys with and without the SIMD tag filter. A sub-block has up
            // to 48 slots, so this is what proves the tag batches past the first
            // 16 are really covered -- a claim the report makes in words.
            for (std::size_t i = 0; i < idx.region_count(); ++i) {
                const auto& r = idx.region(i);
                for (std::uint32_t g = 0; g < r.groups(); ++g) {
                    for (std::size_t k = 0, nb = r.subblock_count(g); k < nb; ++k) {
                        if (r.subblock_entries(g, k) <= 16) continue;
                        ++wide_subblocks;
                        for (const auto& suf : r.subblock_keys(g, k)) {
                            const auto full = r.prefix() + suf;
                            const auto simd = idx.find(full, m_lin);
                            const auto scal = idx.find(full, m_scalar);
                            require(simd.found && scal.found && simd.value == scal.value,
                                    "wide sub-block simd/scalar");
                            ++wide_checks;
                        }
                    }
                }
            }

            // (g) Empty one whole sub-block by erasing its keys, then put them
            // back. This covers the empty-sub-block state inside a non-empty
            // group and the re-placement that follows it.
            for (std::size_t i = 0; i < idx.region_count() && empty_block_cases < 5; ++i) {
                for (std::uint32_t g = 0; g < idx.region(i).groups() && empty_block_cases < 5; ++g) {
                    const auto& r = idx.region(i);
                    std::size_t nb = r.subblock_count(g);
                    if (nb < 2) continue;

                    std::size_t k = nb;
                    while (k > 0 && r.subblock_entries(g, --k) == 0) {}
                    if (r.subblock_entries(g, k) == 0) continue;

                    std::vector<Pair> live;
                    for (const auto& suf : r.subblock_keys(g, k)) {
                        const auto full = r.prefix() + suf;
                        const auto f = idx.find(full, m_scalar);
                        require(f.found, "sub-block key lookup");
                        live.emplace_back(full, f.value);
                    }

                    for (const auto& kv : live) {
                        require(idx.erase(kv.first), "erase sub-block key");
                        require(ref.erase(kv.first) == 1, "erase reference");
                    }
                    require(idx.size() == ref.size(), "size after sub-block wipe");
                    validate_multiblock(idx, multi, single, deepest);
                    require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1),
                            "scan after sub-block wipe");
                    ++scan_checks;

                    for (const auto& kv : live) {
                        ref[kv.first] = kv.second;
                        require(idx.insert(kv.first, kv.second), "reinsert sub-block key");
                    }
                    require(idx.size() == ref.size(), "size after sub-block reinsert");
                    validate_multiblock(idx, multi, single, deepest);
                    require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1),
                            "scan after sub-block reinsert");
                    ++scan_checks;
                    ++empty_block_cases;
                }
            }

            // (h) The per-group sub-block cap. The strict-budget allocator relies on
            // it to pin unfunded groups to a single block, so the cap must be
            // exercised with BOTH branches present in one layout: the production
            // runs happened to fund every candidate and never covered it.
            // The differential below then runs on this capped layout, which is what
            // puts erase, reinsert and compaction under the cap.
            const std::uint64_t blocks_uncapped = idx.memory_breakdown().blocks;

            std::vector<std::vector<std::uint16_t>> caps(idx.region_count());
            for (std::size_t i = 0; i < idx.region_count(); ++i) {
                const std::size_t G = idx.region(i).slot_table().size();
                caps[i].assign(G, 0);                       // 0 = uncapped
                for (std::size_t g = 0; g < G; g += 2) caps[i][g] = 1;
            }

            for (std::size_t i = 0; i < idx.region_count(); ++i) {
                const std::size_t G = caps[i].size();
                idx.rebuild_with_caps(i, idx.region(i).all(),
                                      std::vector<std::uint16_t>(G, 48),
                                      std::vector<std::uint32_t>(G, 2048), caps[i]);
            }

            const std::uint64_t blocks_capped = idx.memory_breakdown().blocks;
            require(blocks_capped <= blocks_uncapped, "capping grew the block total");

            for (std::size_t i = 0; i < idx.region_count(); ++i) {
                const auto& r = idx.region(i);
                const auto gs = r.group_stats();
                for (std::uint32_t g = 0; g < r.groups(); ++g) {
                    if (caps[i][g] != 1) { ++cap_open; continue; }
                    ++cap_single;
                    require(r.subblock_count(g) == 1, "sub-block cap not applied");
                    if (gs[g].primary_entries + gs[g].overflow_entries > 48) ++demand_capped;
                }
            }
            require(cap_single > 0 && cap_open > 0, "cap branches not both present");
            require(demand_capped > 0, "no capped group actually needed a second sub-block");

            groups_checked += validate_multiblock(idx, multi, single, deepest);
            ++struct_validations;
            require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1),
                    "scan under a sub-block cap");
            ++scan_checks;

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
                    const auto sc = idx.find(key, m_scalar);
                    require(l.found == h.found && (!l.found || l.value == h.value), "policy agree");
                    require(l.found == sc.found && (!l.found || l.value == sc.value), "simd/scalar agree");
                    require(l.found == (it != ref.end()) && (!l.found || l.value == it->second), "find vs map");
                }

                require(idx.size() == ref.size(), "size");
                ++operations;

                if (step % 97 == 0) {
                    groups_checked += validate_multiblock(idx, multi, single, deepest);
                    ++struct_validations;
                }
                if (step % 499 == 0) {
                    require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1), "full scan");
                    ++scan_checks;
                }
            }

            groups_checked += validate_multiblock(idx, multi, single, deepest);
            ++struct_validations;
            require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1), "final scan");
            ++scan_checks;

            compactions_seen += idx.compaction_count();
            std::cerr << "trial region=" << tr.region << " ops=" << tr.step
                      << " multiblock_groups=" << multi
                      << " singleblock_groups=" << single
                      << " max_subblocks=" << deepest
                      << " compactions=" << idx.compaction_count() << "\n";
        }

        multi_groups = ops_multiblock_groups;
        single_groups = ops_singleblock_groups;
        require(multi_groups > 0, "no multi-block group was produced");
        require(single_groups > 0, "expected a mixed layout (some single-block groups)");
        require(middle_scan_checks > 0, "no scan started from a later sub-block");
        require(wide_checks > 0, "no sub-block with more than 16 entries was probed");
        require(empty_block_cases > 0, "no sub-block was ever emptied and refilled");
        require(compactions_seen > 0, "cross-sub-block compaction never ran");
        require(cap_single > 0 && cap_open > 0, "the sub-block cap was never exercised on both branches");
        require(demand_capped > 0, "the sub-block cap never constrained a group that needed it");

        std::cout << "{\"status\":\"passed\",\"operations\":" << operations
                  << ",\"scan_checks\":" << scan_checks
                  << ",\"later_subblock_scans\":" << middle_scan_checks
                  << ",\"struct_validations\":" << struct_validations
                  << ",\"groups_struct_checked\":" << groups_checked
                  << ",\"multiblock_group_observations\":" << multi_groups
                  << ",\"singleblock_group_observations\":" << single_groups
                  << ",\"max_subblocks\":" << max_subblocks
                  << ",\"wide_subblocks_probed\":" << wide_subblocks
                  << ",\"wide_subblock_simd_scalar_checks\":" << wide_checks
                  << ",\"subblock_wipe_refill_cases\":" << empty_block_cases
                  << ",\"capped_groups\":" << cap_single
                  << ",\"uncapped_groups\":" << cap_open
                  << ",\"capped_groups_with_second_block_demand\":" << demand_capped
                  << ",\"compactions\":" << compactions_seen
                  << ",\"seed\":20260915}" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << "\n";
        return 1;
    }
}

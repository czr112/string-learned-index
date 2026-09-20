// E11 correctness gate for sub-block range location (2026-09-16).
//
// The locate path visits at most one primary sub-block per group, chosen from a
// boundary directory. That is a change to WHERE the primary search looks, so the
// gate compares the two lookup paths query by query on the SAME index: found,
// value and hit source must agree exactly. Random tests alone would not reach the
// interesting positions, so the key sets below are built on purpose.
//
// The condition that matters most: M1 orders the PRIMARY sub-blocks among
// themselves and says nothing about where overflow records sit. A locate that
// returns "no candidate block" therefore cannot mean "not in this group" -- the
// overflow container must still be searched. The "below every primary key, but
// present in overflow" family below exists to catch exactly that mistake.

#include "data.hpp"
#include "switches.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <set>
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

// The directory must describe the sub-blocks exactly: one entry per non-empty
// sub-block, in sub-block order, holding that sub-block's true minimum.
static std::size_t validate_dirs(const Index& idx, std::size_t& multi_groups) {
    std::size_t groups_checked = 0;

    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const auto& r = idx.region(i);
        for (std::uint32_t g = 0; g < r.groups(); ++g) {
            const std::size_t nb = r.subblock_count(g);
            if (nb < 2) {
                require(!r.has_dir(g), "single-block group carries a directory");
                continue;
            }
            ++multi_groups;

            const auto dir = r.boundary_dir(g);
            std::vector<std::size_t> nonempty;
            for (std::size_t k = 0; k < nb; ++k)
                if (r.subblock_entries(g, k) > 0) nonempty.push_back(k);

            require(dir.size() == nonempty.size(), "directory size != non-empty sub-blocks");
            for (std::size_t k = 0; k < dir.size(); ++k) {
                require(dir[k].second == nonempty[k], "directory sub-block order");
                require(dir[k].second < nb, "directory sub-block index range");

                const auto keys = r.subblock_keys(g, dir[k].second);
                require(!keys.empty(), "boundary points at an empty sub-block");
                require(keys.front() == dir[k].first, "boundary is not the sub-block minimum");

                if (k)
                    require(compare(dir[k - 1].first, dir[k].first) < 0,
                            "boundaries must increase strictly");
            }
            ++groups_checked;
        }
    }

    return groups_checked;
}

// Look up one key both ways and require identical answers.
struct Agree {
    std::size_t checked = 0;
    std::size_t below_every_primary = 0;   // locate found no candidate block
    std::size_t overflow_source = 0;       // answer came from the overflow container
    std::size_t mismatch = 0;
};

static void agree_on(const Index& idx, std::string_view key, Mode plain, Mode locate, Agree& a) {
    const Result p = idx.find(key, plain);
    const Result l = idx.find(key, locate);
    ++a.checked;

    if (l.candidate_blocks == 0) ++a.below_every_primary;
    if (l.source == 2) ++a.overflow_source;

    if (p.found != l.found || (p.found && p.value != l.value) || p.source != l.source) {
        ++a.mismatch;
        std::cerr << "MISMATCH key_len=" << key.size() << " plain(found=" << p.found
                  << " src=" << unsigned(p.source) << ") locate(found=" << l.found
                  << " src=" << unsigned(l.source) << ")\n";
    }
}

int main() {
    try {
        std::mt19937_64 rng(20260916);
        std::set<std::string, Less> pool_set;

        for (const auto& s : std::vector<std::string>{
                 "", "a", "aa", "aaa", "z",
                 std::string("a\0b", 3), std::string("\0", 1), std::string("\xff", 1),
                 std::string(8, '\0'), std::string(8, '\xff'), std::string(4096, 'q'),
             })
            pool_set.insert(s);

        for (int i = 0; i < 2200; ++i) {
            std::string s = i % 3 ? "common/prefix/path/" : "";
            for (std::size_t j = 0, m = rng() % 100; j < m; ++j)
                s.push_back(static_cast<char>(rng() % 256));
            pool_set.insert(s);
        }
        const std::vector<std::string> pool(pool_set.begin(), pool_set.end());

        const Mode plain{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 128, false};
        const Mode locate{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 128, true};

        Agree a;
        std::size_t multi_groups = 0, groups_checked = 0;
        std::size_t boundary_keys = 0, between_keys = 0, outside_keys = 0;
        std::size_t ovf_records_checked = 0, ovf_below_all_primary = 0, forced_cases = 0, forced_groups = 0;
        std::size_t scans = 0, ops = 0;

        struct Trial {
            std::size_t region;
            std::uint32_t block_bytes;
            bool cap_every_other;
        };

        for (const auto tr : std::vector<Trial>{{64, 2048, true}, {32, 2048, true}, {64, 1024, false}}) {
            Reference ref;
            std::vector<Pair> initial;
            for (const auto& k : pool) {
                initial.emplace_back(k, ref.size() + 1);
                ref[k] = initial.back().second;
            }

            Config c;
            e25sw::pin_frozen_switches(c);
            c.block_bytes = tr.block_bytes;
            c.slots = 48;
            c.model_kind = ModelKind::Context16;
            c.region_keys = tr.region;
            c.multiblock = true;

            Index idx(c);
            idx.bulk_load(initial);

            std::vector<std::vector<std::uint16_t>> caps(idx.region_count());
            for (std::size_t i = 0; i < idx.region_count(); ++i) {
                const std::size_t G = idx.region(i).slot_table().size();
                caps[i].assign(G, 0);
                if (tr.cap_every_other)
                    for (std::size_t g = 0; g < G; g += 2) caps[i][g] = 1;
                idx.rebuild_with_caps(i, idx.region(i).all(),
                                      std::vector<std::uint16_t>(G, 48),
                                      std::vector<std::uint32_t>(G, tr.block_bytes), caps[i]);
            }

            groups_checked += validate_dirs(idx, multi_groups);
            require(multi_groups > 0, "no multi-block group in this trial");

            // ---- targeted key families ----------------------------------------
            for (std::size_t i = 0; i < idx.region_count(); ++i) {
                const auto& r = idx.region(i);
                const auto& prefix = r.prefix();

                for (std::uint32_t g = 0; g < r.groups(); ++g) {
                    const std::size_t nb = r.subblock_count(g);
                    if (nb < 2) continue;

                    std::vector<std::vector<std::string>> blk(nb);
                    for (std::size_t k = 0; k < nb; ++k) blk[k] = r.subblock_keys(g, k);
                    const auto dir = r.boundary_dir(g);

                    // (a) exactly each sub-block's minimum and maximum
                    for (std::size_t k = 0; k < nb; ++k) {
                        if (blk[k].empty()) continue;
                        agree_on(idx, prefix + blk[k].front(), plain, locate, a);
                        agree_on(idx, prefix + blk[k].back(), plain, locate, a);
                        boundary_keys += 2;

                        // (b) between this sub-block's max and the next non-empty one
                        for (std::size_t j = k + 1; j < nb; ++j) {
                            if (blk[j].empty()) continue;
                            const std::string between = blk[k].back() + '\0';
                            if (compare(between, blk[j].front()) < 0) {
                                agree_on(idx, prefix + between, plain, locate, a);
                                ++between_keys;
                            }
                            break;
                        }
                    }

                    // (c) below every primary key of the group, and above all of them
                    for (std::size_t k = 0; k < nb; ++k) {
                        if (blk[k].empty() || blk[k].front().empty()) break;
                        // A strict prefix of the group's smallest primary key sorts
                        // below every primary key of the group.
                        const std::string lower = blk[k].front().substr(0, blk[k].front().size() - 1);
                        agree_on(idx, prefix + lower, plain, locate, a);
                        ++outside_keys;
                        break;
                    }
                    for (std::size_t k = nb; k-- > 0;) {
                        if (blk[k].empty()) continue;
                        agree_on(idx, prefix + blk[k].back() + '\xff', plain, locate, a);
                        ++outside_keys;
                        break;
                    }

                    // (d) every overflow record of this group. This is the family a
                    // locate returning "no candidate block" must still find: M1 says
                    // nothing about where overflow records sit, so skipping the
                    // primary blocks may never be taken as "not in this group".
                    const auto ovf = r.overflow_keys(g);
                    for (const auto& suf : ovf) {
                        agree_on(idx, prefix + suf, plain, locate, a);
                        ++ovf_records_checked;
                        if (!dir.empty() && compare(suf, dir.front().first) < 0)
                            ++ovf_below_all_primary;
                    }
                }
            }

            // (e) Force the case the change hinges on: empty a LEADING sub-block so
            // that records still sitting in OVERFLOW now sort below every primary
            // key. "No candidate block" must still find them -- M1 orders the
            // primary sub-blocks among themselves and says nothing about where
            // overflow records sit. Left un-forced, this family did not occur at
            // all in the first run of this gate.
            for (std::size_t i = 0; i < idx.region_count() && forced_cases < 300; ++i) {
                const auto& r0 = idx.region(i);
                const auto& prefix = r0.prefix();

                for (std::uint32_t g = 0; g < r0.groups() && forced_cases < 300; ++g) {
                    const std::size_t nb = r0.subblock_count(g);
                    if (nb < 2) continue;
                    if (r0.overflow_keys(g).empty()) continue;

                    // Which sub-blocks actually hold entries. Later ones are often
                    // empty -- they exist because the suffix area ran out, not
                    // because records live there -- so wiping them changes nothing.
                    std::vector<std::size_t> nonempty;
                    for (std::size_t k = 0; k < nb; ++k)
                        if (r0.subblock_entries(g, k) > 0) nonempty.push_back(k);
                    if (nonempty.size() < 2) continue;   // keep at least one primary block
                    if (forced_groups++ > 40) break;

                    // Wipe the leading non-empty sub-blocks one at a time and check
                    // after each: the boundary rises, so more overflow records end up
                    // sorting below every primary key.
                    for (std::size_t j = 0; j + 1 < nonempty.size() && forced_cases < 300; ++j) {
                        for (const auto& suf : r0.subblock_keys(g, nonempty[j])) {
                            const std::string full = prefix + suf;
                            require(idx.erase(full), "forced-case erase");
                            require(ref.erase(full) == 1, "forced-case erase ref");
                        }
                        require(idx.size() == ref.size(), "size after wiping a leading sub-block");

                        groups_checked += validate_dirs(idx, multi_groups);

                        const auto dir = r0.boundary_dir(g);
                        for (const auto& suf : r0.overflow_keys(g)) {
                            agree_on(idx, prefix + suf, plain, locate, a);
                            ++ovf_records_checked;
                            if (!dir.empty() && compare(suf, dir.front().first) < 0) {
                                ++ovf_below_all_primary;
                                ++forced_cases;
                            }
                        }
                    }
                    // A key below everything must also be answered correctly.
                    agree_on(idx, prefix, plain, locate, a);
                }
            }

            // ---- random differential with maintenance in between --------------
            for (std::size_t step = 0; step < 4000; ++step) {
                const auto& key = pool[rng() % pool.size()];
                const std::uint64_t value = step % 7 ? rng() : 0;
                const auto op = rng() % 6;

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
                    const auto n = rng() % 30;
                    require(idx.scan(key, n) == expected_scan(ref, key, n), "range scan");
                    ++scans;
                } else {
                    agree_on(idx, key, plain, locate, a);
                }
                ++ops;

                if (step % 211 == 0) {
                    groups_checked += validate_dirs(idx, multi_groups);
                    for (const auto& kv : ref) agree_on(idx, kv.first, plain, locate, a);
                }
                if (step % 977 == 0) {
                    require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1),
                            "full scan");
                    ++scans;
                }
            }

            groups_checked += validate_dirs(idx, multi_groups);
            require(idx.scan({}, ref.size() + 1) == expected_scan(ref, {}, ref.size() + 1), "final scan");
            ++scans;

            std::cerr << "trial region=" << tr.region << " block=" << tr.block_bytes
                      << " capped=" << (tr.cap_every_other ? "yes" : "no")
                      << " groups=" << idx.region(0).groups() << "\n";
        }

        require(a.mismatch == 0, "locate and traversal paths disagree");
        require(a.below_every_primary > 0, "no query ever fell below every primary key");
        require(a.overflow_source > 0, "no query was ever answered from overflow");
        require(boundary_keys > 0 && between_keys > 0 && outside_keys > 0, "a key family was empty");
        require(ovf_records_checked > 0, "no overflow record was queried");

        std::cout << "{\"status\":\"passed\",\"comparisons\":" << a.checked
                  << ",\"mismatches\":" << a.mismatch
                  << ",\"boundary_keys\":" << boundary_keys
                  << ",\"between_subblock_keys\":" << between_keys
                  << ",\"outside_all_primary_keys\":" << outside_keys
                  << ",\"overflow_records_queried\":" << ovf_records_checked
                  << ",\"overflow_records_below_all_primary\":" << ovf_below_all_primary
                  << ",\"forced_overflow_below_all_primary\":" << forced_cases
                  << ",\"queries_below_every_primary\":" << a.below_every_primary
                  << ",\"queries_answered_from_overflow\":" << a.overflow_source
                  << ",\"dir_groups_struct_checked\":" << groups_checked
                  << ",\"multiblock_groups\":" << multi_groups
                  << ",\"dynamic_ops\":" << ops
                  << ",\"scans\":" << scans
                  << ",\"seed\":20260916}" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << "\n";
        return 1;
    }
}

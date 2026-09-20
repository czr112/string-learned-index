// E33/SC1: the scan contract -- what the group-ascending traversal actually promises.
//
// Region::scan walks a region's groups in ASCENDING GROUP-ID ORDER and, inside one
// group, merges that group's rank-ordered sub-blocks with its overflow container in key
// order. So the scan's output is lexicographic ONLY IF the mapping key -> group id is
// monotone non-decreasing in key order within the region. The start cursor rests on the
// same thing: it skips every group below the start key because everything there is
// assumed to be smaller.
//
// Invariant M1 says only that a group's OWN sub-blocks are rank-ordered. It says nothing
// across groups, and nothing in the tree asserted the monotonicity -- the model is a
// conditional-CDF walk, which SHOULD be monotone, but "should be" is not an assertion,
// and a future model swap could break every scan silently with every other gate still
// passing. This gate checks the CONTRACT, on the three standard datasets.
//
//   PART 1 -- the real configuration, four items:
//     1a  monotonicity, measured: every key in key order, per region, the group ids must
//         be non-decreasing. Every key is ALSO looked up through the query path and must
//         come back with the value bulk_load placed, because a mapping that is monotone
//         but is not the mapping used at build time would satisfy the first half while
//         breaking every scan.
//     1b  a full scan (empty start, count = size) returns records in strictly ascending
//         key order and exactly the reference's content, element by element against a
//         std::map -- not by checksum.
//     1c  scans from many starts (the first key, the last key, a key beyond the last key,
//         the minimum key of EVERY group, and keys absent from the index) at counts 1, 2
//         and 16, each exactly the reference's lower_bound(start) truncated to that
//         count. That is the "arbitrary start" and "fewer than n" coverage.
//     1d  the same after the state changes: erase the first record of several groups, the
//         last record of several others, one in the middle, and enough to EMPTY a middle
//         sub-block; then insert enough keys to force overflow. The scan must still equal
//         the reference, which is what proves the overflow merge lands at the right
//         position.
//
//   PART 2 -- the negative control that makes Part 1 non-vacuous. With the diagnostic-only
//     rotation hook on (Region::sar_rotate_groups), the group ids used for placement and
//     for the scan rotate by one: deterministic, in range, a bijection, and deliberately
//     NOT monotone. A second index is built from the same plan and the same operation
//     sequence, and at least one of the full-scan properties must now fail. If none does,
//     the hook did not perturb the mapping and the gate reports FAILURE -- because Part 1
//     would then have tested nothing. Two full-scan forms are checked, the empty-start
//     one the plan calls for and a drain from the first key: the empty-start form alone
//     is blind in regions whose recorded prefix is non-empty, and that is a property of
//     the dataset, not of the hook.
//
//   PART 3 -- the other direction. With the hook off again, a second index built from the
//     same plan and the same operation sequence must be identical to the first, region by
//     region and sub-block by sub-block, and must return the same scans.
//
// The hook is NOT a Config switch: the four switches.hpp tables are untouched, so the
// frozen configuration cannot move. It lives behind -DE25_DIAG in the same style as the
// other Region diagnostics, and this gate is built with that macro.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifndef E25_DIAG
#error "check_scan_contract must be built with -DE25_DIAG: its negative control needs Region::sar_rotate_groups"
#endif

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

// ---------------------------------------------------------------------------
// The capacity plan, built exactly as the other gates build it.
// ---------------------------------------------------------------------------

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

static std::vector<std::vector<std::uint16_t>> sg_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint16_t>> out;

    for (const auto& one : v) out.push_back(one.sg);

    return out;
}

static std::vector<std::vector<std::uint32_t>> bb_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint32_t>> out;

    for (const auto& one : v) out.push_back(one.bb);

    return out;
}

// ---------------------------------------------------------------------------
// Read-only addressing used to build the erase set.
// ---------------------------------------------------------------------------

struct GroupRef {
    std::size_t region;
    std::uint32_t group;
};

static std::vector<GroupRef> live_groups(const Index& idx) {
    std::vector<GroupRef> out;

    for (std::size_t r = 0; r < idx.region_count(); ++r) {
        const Region& reg = idx.region(r);
        for (std::uint32_t g = 0; g < reg.groups(); ++g) {
            std::size_t entries = 0;
            const std::size_t nb = reg.subblock_count(g);
            for (std::size_t i = 0; i < nb; ++i) entries += reg.subblock_entries(g, i);
            if (entries) out.push_back(GroupRef{r, g});
        }
    }

    return out;
}

static std::string group_min_key(const Index& idx, const GroupRef& ref) {
    const Region& reg = idx.region(ref.region);
    const std::size_t nb = reg.subblock_count(ref.group);

    for (std::size_t i = 0; i < nb; ++i) {
        if (reg.subblock_entries(ref.group, i) == 0) continue;
        const auto keys = reg.subblock_keys(ref.group, i);
        return reg.prefix() + keys.front();
    }

    throw std::runtime_error("group has no records");
}

static std::string group_max_key(const Index& idx, const GroupRef& ref) {
    const Region& reg = idx.region(ref.region);
    const std::size_t nb = reg.subblock_count(ref.group);

    for (std::size_t i = nb; i > 0; --i) {
        const std::size_t blk = i - 1;
        if (reg.subblock_entries(ref.group, blk) == 0) continue;
        const auto keys = reg.subblock_keys(ref.group, blk);
        return reg.prefix() + keys.back();
    }

    throw std::runtime_error("group has no records");
}

static std::string group_middle_key(const Index& idx, const GroupRef& ref) {
    const Region& reg = idx.region(ref.region);
    const std::size_t nb = reg.subblock_count(ref.group);

    for (std::size_t i = 0; i < nb; ++i) {
        const std::size_t n = reg.subblock_entries(ref.group, i);
        if (n == 0) continue;
        const auto keys = reg.subblock_keys(ref.group, i);
        return reg.prefix() + keys[n / 2];
    }

    throw std::runtime_error("group has no records");
}

// Every key of the group's MIDDLE sub-block, so the gate can empty it. A group with
// fewer than three sub-blocks has no middle to empty and returns nothing; the caller
// counts those instead of assuming they exist.
static std::vector<std::string> middle_subblock_keys(const Index& idx, const GroupRef& ref) {
    const Region& reg = idx.region(ref.region);
    const std::size_t nb = reg.subblock_count(ref.group);
    std::vector<std::string> out;

    if (nb < 3) return out;

    const auto keys = reg.subblock_keys(ref.group, nb / 2);
    out.reserve(keys.size());

    for (const auto& k : keys) out.push_back(reg.prefix() + k);

    return out;
}

// ---------------------------------------------------------------------------
// The operation sequence: Part 1d applies it to the real index, Parts 2 and 3 replay it.
// ---------------------------------------------------------------------------

struct Op {
    int kind;              // 1 = insert, 2 = erase
    std::string key;
    std::uint64_t value;
};

struct OpPlan {
    std::vector<Op> ops;
    std::size_t erases = 0;              // ops[0..erases) are erases, the rest are inserts
    std::size_t groups_touched = 0;
    std::size_t deep_groups = 0;         // groups that have a middle sub-block at all
    std::size_t groups_emptied = 0;      // groups whose middle sub-block was emptied
    std::size_t groups_overflowed = 0;   // groups the inserts were aimed at
};

static void push_erase(std::vector<Op>& ops, std::unordered_set<std::string>& used,
                       const std::string& key) {
    if (!used.insert(key).second) return;

    ops.push_back(Op{2, key, 0});
}

static void push_insert(std::vector<Op>& ops, std::unordered_set<std::string>& used,
                        const std::string& key, std::uint64_t value) {
    if (!used.insert(key).second) return;

    ops.push_back(Op{1, key, value});
}

static OpPlan build_ops(const Index& idx, std::uint32_t ov_threshold) {
    OpPlan plan;
    std::unordered_set<std::string> used;
    const auto refs = live_groups(idx);
    const std::size_t want = std::min<std::size_t>(24, refs.size());
    const std::size_t e1 = want / 4;
    const std::size_t e2 = want / 2;
    const std::size_t e3 = (want * 3) / 4;
    std::size_t last = SIZE_MAX;
    std::size_t deep_cursor = 0;

    // Groups that HAVE a middle sub-block, so "empty a middle sub-block" is coverage
    // rather than a case the gate quietly skipped: a group with one or two sub-blocks
    // has no middle to empty. In the freshly built state every sub-block is non-empty,
    // so the first deep group provides victims.
    std::vector<GroupRef> deep;

    for (const auto& gr : refs)
        if (idx.region(gr.region).subblock_count(gr.group) >= 3) deep.push_back(gr);

    plan.deep_groups = deep.size();

    for (std::size_t j = 0; j < want; ++j) {
        const std::size_t pick = j * refs.size() / want;
        if (pick == last) continue;
        last = pick;
        const GroupRef gr = refs[pick];
        ++plan.groups_touched;

        if (j < e1) {
            push_erase(plan.ops, used, group_min_key(idx, gr));
        } else if (j < e2) {
            push_erase(plan.ops, used, group_max_key(idx, gr));
        } else if (j < e3) {
            push_erase(plan.ops, used, group_middle_key(idx, gr));
        } else if (j + 3 < want) {
            if (!deep.empty()) {
                const auto victims = middle_subblock_keys(idx, deep[deep_cursor % deep.size()]);
                ++deep_cursor;
                if (!victims.empty()) ++plan.groups_emptied;
                for (const auto& k : victims) push_erase(plan.ops, used, k);
            }
        } else {
            // Enough fresh records aimed at one group to overrun its planned area, so the
            // overflow container is forced to exist and to hold records whose merge
            // position a wrong scan would get wrong. The key is the group's maximum plus
            // an 0x02 byte, which no generated key carries, so it is provably fresh and
            // it sorts immediately after that key.
            const std::string base = group_max_key(idx, gr);
            ++plan.groups_overflowed;
            for (std::uint32_t k = 0; k < ov_threshold + 64; ++k) {
                std::string fresh = base;
                fresh.push_back('\x02');
                fresh += std::to_string(1000 + k);
                push_insert(plan.ops, used, fresh, 0x9000ULL + plan.ops.size());
            }
        }
    }

    for (const auto& o : plan.ops) {
        if (o.kind != 2) break;
        ++plan.erases;
    }

    return plan;
}

// ---------------------------------------------------------------------------
// Comparisons. Every one of them is element by element; a checksum would let two
// different indexes agree.
// ---------------------------------------------------------------------------

struct ScanStats {
    std::uint64_t scans = 0;
    std::uint64_t records = 0;
    std::uint64_t short_scans = 0;
};

struct ScanCounts {
    std::uint64_t regions = 0;
    std::uint64_t subblocks = 0;
    std::uint64_t subblock_keys = 0;
    std::uint64_t boundary_entries = 0;
};

struct MonotoneReport {
    std::uint64_t keys_walked = 0;
    std::uint64_t pairs_checked = 0;
    std::uint64_t violations = 0;
    std::uint64_t placement_hits = 0;
    std::uint64_t regions_single_group = 0;
};

struct PairCmp {
    bool operator()(const Pair& a, const Pair& b) const {
        const int c = compare(a.first, b.first);
        if (c) return c < 0;

        return a.second < b.second;
    }
};

static std::string describe(const std::string& key) {
    static const char* HEX = "0123456789abcdef";
    std::string out;
    out.reserve(key.size() + 8);

    for (unsigned char ch : key) {
        if (ch >= 32 && ch < 127) {
            out.push_back(static_cast<char>(ch));
        } else {
            out += "\\x";
            out.push_back(HEX[ch >> 4]);
            out.push_back(HEX[ch & 15]);
        }
    }

    return out;
}

static void compare_scan(const Index& idx, const Reference& ref, const std::string& start,
                         std::size_t count, const char* what, ScanStats& stats) {
    const auto got = idx.scan(start, count);
    std::vector<Pair> want;
    want.reserve(count);

    for (auto it = ref.lower_bound(start); it != ref.end() && want.size() < count; ++it)
        want.emplace_back(it->first, it->second);

    ++stats.scans;

    if (got.size() != want.size())
        throw std::runtime_error(std::string(what) + ": scan length differs for start=" +
                                 describe(start) + " count=" + std::to_string(count));

    for (std::size_t i = 0; i < want.size(); ++i) {
        if (got[i].first != want[i].first || got[i].second != want[i].second)
            throw std::runtime_error(std::string(what) + ": scan record differs at " +
                                     std::to_string(i) + " for start=" + describe(start));
        ++stats.records;
    }

    if (got.size() < count) ++stats.short_scans;
}

static void check_full_scan(const Index& idx, const Reference& ref, const char* what,
                            ScanStats& stats) {
    const auto got = idx.scan(std::string_view{}, idx.size());
    ++stats.scans;

    if (got.size() != ref.size())
        throw std::runtime_error(std::string(what) + ": full scan length " +
                                 std::to_string(got.size()) + " != reference " +
                                 std::to_string(ref.size()));

    auto it = ref.begin();

    for (std::size_t i = 0; i < got.size(); ++i, ++it) {
        if (got[i].first != it->first || got[i].second != it->second)
            throw std::runtime_error(std::string(what) + ": full scan record " +
                                     std::to_string(i) + " differs from the reference");
        if (i && compare(got[i - 1].first, got[i].first) >= 0)
            throw std::runtime_error(std::string(what) + ": full scan is not strictly ascending at " +
                                     std::to_string(i));
        ++stats.records;
    }
}

// The same truncation rule as compare_scan, but as a boolean, for the negative control:
// there the scan is EXPECTED to disagree, so the disagreement has to be counted rather
// than thrown.
static bool scan_matches(const Index& idx, const Reference& ref, const std::string& start,
                         std::size_t count) {
    const auto got = idx.scan(start, count);
    auto it = ref.lower_bound(start);

    for (std::size_t i = 0; i < count; ++i) {
        if (it == ref.end()) return i == got.size();
        if (i >= got.size()) return false;
        if (got[i].first != it->first || got[i].second != it->second) return false;
        ++it;
    }

    return got.size() == count;
}

static bool ascending(const std::vector<Pair>& v) {
    for (std::size_t i = 1; i < v.size(); ++i)
        if (compare(v[i - 1].first, v[i].first) >= 0) return false;

    return true;
}

// The multiset of RECORDS, not of keys and not in order: an out-of-order result that
// holds exactly the right records is caught by ascending(), and a result that lost or
// duplicated records is caught here.
static bool same_multiset(const std::vector<Pair>& got, const Reference& ref) {
    std::multiset<Pair, PairCmp> mine(got.begin(), got.end());
    std::multiset<Pair, PairCmp> theirs;

    for (const auto& kv : ref) theirs.emplace(kv.first, kv.second);

    return mine == theirs;
}

// Part 1a. The mapping must be monotone within a region, and it must be the mapping the
// records were PLACED under -- the second half is not decoration: a monotone mapping that
// is not the placement mapping satisfies the first half and breaks every scan, so each
// key is also looked up through the full query path and must come back with the value
// bulk_load placed.
static MonotoneReport check_monotone(const Index& idx, const std::vector<Pair>& keys) {
    MonotoneReport rep;
    std::size_t prev_region = SIZE_MAX;
    std::uint32_t prev_group = 0;
    const Mode mode{Route::Full, Prefetch::Off, true, false};

    for (const auto& kv : keys) {
        const std::string& key = kv.first;
        const std::size_t region = idx.locate(key);
        const Region& reg = idx.region(region);
        const std::uint32_t group = reg.group_of(std::string_view(key).substr(reg.prefix_length()));

        if (group >= reg.groups())
            throw std::runtime_error("predicted group out of range for key " + describe(key));

        if (region == prev_region) {
            ++rep.pairs_checked;
            if (group < prev_group) ++rep.violations;
        }

        prev_region = region;
        prev_group = group;

        const Result found = idx.find_known(region, key, mode);

        if (!found.found || found.value != kv.second)
            throw std::runtime_error("the query group is not the placement group for key " +
                                     describe(key));

        ++rep.placement_hits;
        ++rep.keys_walked;
    }

    for (std::size_t r = 0; r < idx.region_count(); ++r)
        if (idx.region(r).groups() == 1) ++rep.regions_single_group;

    if (rep.violations)
        throw std::runtime_error("canonical_group is not monotone in key order: " +
                                 std::to_string(rep.violations) + " violating adjacent pairs");

    return rep;
}

// Part 1c. `key + 0x01` sorts immediately after `key` and is absent from the index --
// no generated key carries that byte -- which is exactly the "start between two keys"
// case the scan's cursor has to get right.
static std::vector<std::string> start_keys(const Index& idx, const std::vector<Pair>& keys) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;

    auto add = [&](const std::string& k) {
        if (seen.insert(k).second) out.push_back(k);
    };

    add(std::string());               // the very beginning
    add(keys.front().first);          // the first key
    add(keys.back().first);           // the last key
    add(keys.back().first + "\x01");  // strictly after every key

    // The minimum key of EVERY group, so "a key inside every group" is complete rather
    // than sampled.
    for (const auto& gr : live_groups(idx)) add(group_min_key(idx, gr));

    // Absent keys spread across the key space.
    const std::size_t stride = std::max<std::size_t>(1, keys.size() / 512);

    for (std::size_t i = 0; i < keys.size(); i += stride) add(keys[i].first + "\x01");

    return out;
}

// Parts 1 and 3: every operation must do what it says. A silently refused insert would
// let the next checkpoint pass against a state the index is not in.
static void apply_ops_strict(Index& idx, const std::vector<Op>& ops, std::size_t from,
                             std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        if (ops[i].kind == 1) {
            if (!idx.insert(ops[i].key, ops[i].value))
                throw std::runtime_error("insert was refused: " + describe(ops[i].key));
        } else {
            if (!idx.erase(ops[i].key))
                throw std::runtime_error("erase was refused: " + describe(ops[i].key));
        }
    }
}

// Part 3. Region by region, group by group, sub-block by sub-block: the same keys in the
// same slots, the same boundary directories. Values are covered by the element-wise scan
// comparison that follows, which is why no per-sub-block value accessor is needed.
static ScanCounts assert_same_structure(const Index& a, const Index& c, const char* what) {
    ScanCounts n;

    if (a.region_count() != c.region_count())
        throw std::runtime_error(std::string(what) + ": region count differs");
    if (a.size() != c.size())
        throw std::runtime_error(std::string(what) + ": record count differs");
    if (a.overflow_count() != c.overflow_count())
        throw std::runtime_error(std::string(what) + ": overflow count differs");
    if (a.memory() != c.memory())
        throw std::runtime_error(std::string(what) + ": reported memory differs");

    const auto ma = a.memory_breakdown();
    const auto mc = c.memory_breakdown();

    if (ma.blocks != mc.blocks || ma.model != mc.model || ma.prefix != mc.prefix ||
        ma.overflow != mc.overflow || ma.tables != mc.tables ||
        ma.directory != mc.directory || ma.regions_self != mc.regions_self ||
        ma.fences != mc.fences || ma.self != mc.self)
        throw std::runtime_error(std::string(what) + ": memory breakdown differs");

    for (std::size_t r = 0; r < a.region_count(); ++r) {
        const Region& ra = a.region(r);
        const Region& rc = c.region(r);

        if (ra.prefix() != rc.prefix())
            throw std::runtime_error(std::string(what) + ": region prefix differs");
        if (ra.groups() != rc.groups())
            throw std::runtime_error(std::string(what) + ": group count differs");
        if (ra.size() != rc.size())
            throw std::runtime_error(std::string(what) + ": region record count differs");
        ++n.regions;

        for (std::uint32_t g = 0; g < ra.groups(); ++g) {
            const std::size_t nba = ra.subblock_count(g);
            const std::size_t nbc = rc.subblock_count(g);

            if (nba != nbc)
                throw std::runtime_error(std::string(what) + ": sub-block count differs");

            for (std::size_t i = 0; i < nba; ++i) {
                const auto ka = ra.subblock_keys(g, i);
                const auto kc = rc.subblock_keys(g, i);

                if (ka.size() != kc.size())
                    throw std::runtime_error(std::string(what) + ": sub-block size differs");
                ++n.subblocks;

                for (std::size_t j = 0; j < ka.size(); ++j) {
                    if (ka[j] != kc[j])
                        throw std::runtime_error(std::string(what) + ": sub-block key differs");
                    ++n.subblock_keys;
                }
            }

            const auto da = ra.boundary_dir(g);
            const auto dc = rc.boundary_dir(g);

            if (da.size() != dc.size())
                throw std::runtime_error(std::string(what) + ": boundary directory size differs");

            for (std::size_t j = 0; j < da.size(); ++j) {
                if (da[j].first != dc[j].first || da[j].second != dc[j].second)
                    throw std::runtime_error(std::string(what) + ": boundary entry differs");
                ++n.boundary_entries;
            }
        }
    }

    return n;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_scan_contract KIND N [TRANCOCSV] [RUNTAG]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        const std::string frozen_rel =
            "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string text = ss.str();
            const auto position = text.find("\"frozen_hybrid_threshold\":");
            if (position == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(text.substr(position + 26)));
        }

        // The FROZEN combination, stated field by field rather than inherited from a
        // Config default -- the same five switches every other gate pins.
        Config cfg = e25sw::baseline_config(thr);
        cfg.write_fast_locate = true;
        cfg.reuse_locate_ctx = true;
        cfg.compact_reuse_tag = true;
        cfg.compact_needs_garbage = true;
        cfg.scan_reserve_result = true;

        // The hook must be off before Part 1 starts, or the "real configuration" half
        // would be measuring the perturbed mapping instead.
        if (Region::sar_rotate_groups)
            throw std::runtime_error("the rotation hook was already on before Part 1");

        std::cerr << "check_scan_contract frozen[" << e25sw::switches(cfg) << "]" << std::endl;

        Pipeline plan;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        Reference ref;
        for (const auto& kv : keys) ref[kv.first] = kv.second;

        auto a = std::make_unique<Index>(cfg);
        a->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        // ---- Part 1a -------------------------------------------------------------
        const auto mono = check_monotone(*a, keys);

        std::cout << "{\"phase\":\"monotone\",\"kind\":\"" << kind << "\""
                  << ",\"sampling\":\"full\""
                  << ",\"keys_walked\":" << mono.keys_walked
                  << ",\"adjacent_pairs\":" << mono.pairs_checked
                  << ",\"violations\":" << mono.violations
                  << ",\"placement_hits\":" << mono.placement_hits
                  << ",\"regions\":" << a->region_count()
                  << ",\"regions_single_group\":" << mono.regions_single_group
                  << ",\"region0_prefix_len\":" << a->region(0).prefix_length()
                  << "}" << std::endl;

        // ---- Parts 1b and 1c -----------------------------------------------------
        const auto starts = start_keys(*a, keys);
        const std::size_t CS[3] = {1, 2, 16};
        ScanStats totals;

        auto checkpoint = [&](const char* name, const Reference& want, bool all_starts) {
            ScanStats stats;

            if (a->size() != want.size())
                throw std::runtime_error(std::string(name) + ": index size != reference size");

            check_full_scan(*a, want, name, stats);

            const std::size_t stride = all_starts ? 1 : std::max<std::size_t>(1, starts.size() / 48);
            std::size_t probes = 0;

            for (std::size_t i = 0; i < starts.size(); i += stride) {
                for (std::size_t c = 0; c < 3; ++c) compare_scan(*a, want, starts[i], CS[c], name, stats);
                ++probes;
                if (!all_starts && probes >= 48) break;
            }

            totals.scans += stats.scans;
            totals.records += stats.records;
            totals.short_scans += stats.short_scans;

            std::cout << "{\"phase\":\"scan\",\"checkpoint\":\"" << name << "\""
                      << ",\"live\":" << want.size()
                      << ",\"starts\":" << probes
                      << ",\"counts\":\"1,2,16\""
                      << ",\"scans\":" << stats.scans
                      << ",\"short_scans\":" << stats.short_scans
                      << ",\"records_compared\":" << stats.records
                      << "}" << std::endl;
        };

        // ---- Part 1d -------------------------------------------------------------
        const auto op_plan = build_ops(*a, thr);

        for (std::size_t i = op_plan.erases; i < op_plan.ops.size(); ++i)
            if (op_plan.ops[i].kind != 1)
                throw std::runtime_error("the op plan is not erase-then-insert");

        std::vector<Reference> refs;
        {
            Reference cur = ref;
            refs.push_back(cur);

            for (std::size_t i = 0; i < op_plan.ops.size(); ++i) {
                if (op_plan.ops[i].kind == 1) {
                    if (cur.count(op_plan.ops[i].key))
                        throw std::runtime_error("the op plan inserts a key that is already present");
                    cur[op_plan.ops[i].key] = op_plan.ops[i].value;
                } else {
                    if (cur.erase(op_plan.ops[i].key) != 1)
                        throw std::runtime_error("the op plan erases a key that is not present");
                }

                if (i + 1 == op_plan.erases) refs.push_back(cur);
                if (i + 1 == op_plan.ops.size()) refs.push_back(cur);
            }
        }

        checkpoint("initial", refs[0], true);

        apply_ops_strict(*a, op_plan.ops, 0, op_plan.erases);
        const std::uint64_t ov_before_inserts = a->overflow_count();
        checkpoint("erased", refs[1], false);

        apply_ops_strict(*a, op_plan.ops, op_plan.erases, op_plan.ops.size());
        const std::uint64_t ov_after_inserts = a->overflow_count();
        checkpoint("overflow", refs[2], false);

        std::cout << "{\"phase\":\"ops\",\"groups_touched\":" << op_plan.groups_touched
                  << ",\"deep_groups\":" << op_plan.deep_groups
                  << ",\"groups_emptied\":" << op_plan.groups_emptied
                  << ",\"groups_overflowed\":" << op_plan.groups_overflowed
                  << ",\"erases\":" << op_plan.erases
                  << ",\"inserts\":" << (op_plan.ops.size() - op_plan.erases)
                  << ",\"overflow_before\":" << ov_before_inserts
                  << ",\"overflow_after\":" << ov_after_inserts
                  << "}" << std::endl;

        if (op_plan.deep_groups && !op_plan.groups_emptied)
            throw std::runtime_error(
                "no middle sub-block was emptied: the emptied-sub-block case was not exercised");

        if (ov_after_inserts <= ov_before_inserts)
            throw std::runtime_error(
                "the inserts forced no overflow: the merge-at-the-right-position claim was not exercised");

        // ---- Part 2: the negative control ----------------------------------------
        Region::sar_rotate_groups = true;

        auto b = std::make_unique<Index>(cfg);
        b->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        std::size_t neg_erase_missed = 0;

        for (const auto& o : op_plan.ops) {
            if (o.kind == 1) {
                b->insert(o.key, o.value);
            } else {
                if (!b->erase(o.key)) ++neg_erase_missed;
            }
        }

        const auto neg_full = b->scan(std::string_view{}, b->size());
        const auto neg_drain = b->scan(keys.front().first, b->size());
        const bool full_asc_ok = ascending(neg_full);
        const bool full_multiset_ok = same_multiset(neg_full, refs[2]);
        const bool drain_asc_ok = ascending(neg_drain);
        const bool drain_multiset_ok = same_multiset(neg_drain, refs[2]);

        std::size_t neg_probes = 0;
        std::size_t neg_mismatches = 0;
        // 4096, not 16: with the rotation the scan from a start key still returns the
        // RIGHT records until it leaves the start region, so a short probe would not see
        // the perturbation. The wrap into the next region is where the group order stops
        // matching key order, and reaching it takes more than one region worth of records.
        const std::size_t neg_probe_count = 4096;
        {
            const std::size_t stride = std::max<std::size_t>(1, starts.size() / 48);

            for (std::size_t i = 0; i < starts.size() && neg_probes < 48; i += stride, ++neg_probes)
                if (!scan_matches(*b, refs[2], starts[i], neg_probe_count)) ++neg_mismatches;
        }

        std::vector<std::string> failed;

        if (!full_asc_ok) failed.push_back("full_ascending");
        if (!full_multiset_ok) failed.push_back("full_multiset");
        if (!drain_asc_ok) failed.push_back("drain_ascending");
        if (!drain_multiset_ok) failed.push_back("drain_multiset");
        if (neg_mismatches) failed.push_back("start_mismatches");

        std::string failed_text;
        for (const auto& f : failed) {
            if (!failed_text.empty()) failed_text += ",";
            failed_text += f;
        }

        std::cout << "{\"phase\":\"negative_control\",\"hook\":\"rotate_groups\""
                  << ",\"full_ascending_ok\":" << (full_asc_ok ? 1 : 0)
                  << ",\"full_multiset_ok\":" << (full_multiset_ok ? 1 : 0)
                  << ",\"drain_ascending_ok\":" << (drain_asc_ok ? 1 : 0)
                  << ",\"drain_multiset_ok\":" << (drain_multiset_ok ? 1 : 0)
                  << ",\"start_probes\":" << neg_probes
                  << ",\"start_probe_count\":" << neg_probe_count
                  << ",\"start_mismatches\":" << neg_mismatches
                  << ",\"erase_missed\":" << neg_erase_missed
                  << ",\"region0_prefix_len\":" << a->region(0).prefix_length()
                  << ",\"failed\":\"" << failed_text << "\"}" << std::endl;

        if (!full_asc_ok || !full_multiset_ok) {
            std::cerr << "negative control fired on the full scan: " << failed_text << std::endl;
        } else {
            std::cerr << "negative control fired on other probes: " << failed_text << std::endl;
        }

        if (failed.empty())
            throw std::runtime_error(
                "the rotation hook did not perturb the mapping: the scan contract was not tested");

        // ---- Part 3: with the hook off, nothing changes ---------------------------
        Region::sar_rotate_groups = false;

        auto c = std::make_unique<Index>(cfg);
        c->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
        apply_ops_strict(*c, op_plan.ops, 0, op_plan.ops.size());

        const auto ident = assert_same_structure(*a, *c, "hook-off index");

        const auto scan_a = a->scan(std::string_view{}, a->size());
        const auto scan_c = c->scan(std::string_view{}, c->size());

        if (scan_a.size() != scan_c.size())
            throw std::runtime_error("hook-off index: full scan length differs");

        for (std::size_t i = 0; i < scan_a.size(); ++i)
            if (scan_a[i].first != scan_c[i].first || scan_a[i].second != scan_c[i].second)
                throw std::runtime_error("hook-off index: full scan record differs at " +
                                         std::to_string(i));

        std::size_t probe_cmp = 0;
        {
            const std::size_t stride = std::max<std::size_t>(1, starts.size() / 48);

            for (std::size_t i = 0; i < starts.size() && probe_cmp < 48; i += stride, ++probe_cmp) {
                const auto one = a->scan(starts[i], 16);
                const auto two = c->scan(starts[i], 16);

                if (one.size() != two.size())
                    throw std::runtime_error("hook-off index: probe length differs");

                for (std::size_t j = 0; j < one.size(); ++j)
                    if (one[j].first != two[j].first || one[j].second != two[j].second)
                        throw std::runtime_error("hook-off index: probe record differs");
            }
        }

        std::cout << "{\"phase\":\"hook_off_identity\""
                  << ",\"structure_identical\":1"
                  << ",\"scan_identical\":1"
                  << ",\"regions\":" << ident.regions
                  << ",\"subblocks\":" << ident.subblocks
                  << ",\"subblock_keys_compared\":" << ident.subblock_keys
                  << ",\"boundary_entries_compared\":" << ident.boundary_entries
                  << ",\"probes_compared\":" << probe_cmp
                  << "}" << std::endl;

        std::cout << "{\"gate\":\"check_scan_contract\",\"kind\":\"" << kind << "\""
                  << ",\"run_tag\":\"" << run_tag << "\""
                  << ",\"keys\":" << N
                  << ",\"regions\":" << a->region_count()
                  << ",\"result\":\"pass\""
                  << ",\"monotone_pairs\":" << mono.pairs_checked
                  << ",\"monotone_violations\":" << mono.violations
                  << ",\"placement_hits\":" << mono.placement_hits
                  << ",\"start_keys\":" << starts.size()
                  << ",\"total_scans\":" << totals.scans
                  << ",\"total_records_compared\":" << totals.records
                  << ",\"total_short_scans\":" << totals.short_scans
                  << ",\"subblock_keys_compared\":" << ident.subblock_keys
                  << ",\"boundary_entries_compared\":" << ident.boundary_entries
                  << "}" << std::endl;

        std::cerr << kind << " scan contract gate done" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

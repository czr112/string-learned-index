// Gate for the BYTE ORDER of the equal-depth directory backends (2026-10-11).
//
// WHY THIS GATE EXISTS. The TRUNCDIR round's gates passed while predict_group() compared bytes
// as SIGNED char (`sep[i] < q[i]`), which on x86 puts every byte >= 0x80 below every byte < 0x80.
// Placement and lookup share that wrong map, so a find-only check cannot see it; the three
// corpora are ASCII, so a corpus differential cannot see it either. The audit that found it is
// 07_Eval_Integration/AUDIT_DEPTH_DIR_ORDER_20261011.md, and this file is the P0 acceptance gate
// the plan (docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md section 3) requires: it FAILS on the
// signed-order source and passes once the search compares unsigned bytes.
//
// WHAT IT CHECKS, for EqualDepthDir AND EqualDepthDirTrunc:
//   (1) the audit's four-key counterexample (0x01, 0x7f, 0x80, 0xff), swept over all 256
//       single-byte queries, against an independent INTEGER count of the thresholds;
//   (2) the same sweep for byte keys that share a prefix and cross 0x7f/0x80 inside the suffix,
//       on a region with many groups, plus the coverage count that proves the suite really
//       exercises the bytes where the signed and the unsigned order disagree;
//   (3) region level: group range, non-decreasing placement over the whole byte domain and the
//       range invariant (bracket) for probes, on a region whose keys include NUL, 0x7f, 0x80,
//       0xff, shared prefixes, duplicates and a 4096-byte key;
//   (4) index level: every stored key found with its exact value, absent probes miss, scans from
//       arbitrary starts come back in unsigned order and match an independent reference row for
//       row, across insert / erase / re-insert churn that forces splits and prefix rebuilds, and
//       again after the input vector is released;
//   (5) NEGATIVE CONTROL: a replica that keeps the SIGNED comparison must be reported by the same
//       reference on the same fixture. A checker that cannot fail is not a check.
//
// NO TIMING IS TAKEN ANYWHERE. Everything here is semantics.
//
// Usage: check_depth_dir_order KIND N [TRANCOCSV]     KIND: mixed|skew|tranco

#include "data.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace validation;

namespace {

std::uint64_t checks = 0, failures = 0;

void require(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

// ---------------------------------------------------------------- unsigned byte order
// Written here so nothing below depends on the comparison the production code happens to use.
int ub_cmp(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char x = static_cast<unsigned char>(a[i]);
        const unsigned char y = static_cast<unsigned char>(b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return (a.size() > b.size()) - (a.size() < b.size());
}

std::size_t lcp(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

std::string byte_str(unsigned b) { return std::string(1, static_cast<char>(b)); }

// ---------------------------------------------------------------- the frozen reference
// Section 2 of TRUNCDIR_ENTRY_DESIGN.md, re-derived from the text with unsigned comparisons:
// threshold j sits at rank floor(j*n/G); the stored form is the shortest prefix of s_j that
// still beats the key at rank-1, or s_j itself where no shorter prefix can win.
struct RefDir {
    std::vector<std::string_view> full;
    std::vector<std::string_view> stored;
    std::vector<std::size_t> rank;
    std::size_t shortened = 0;
    std::size_t fallbacks = 0;
};

RefDir ref_fit(const std::vector<std::string_view>& keys, std::uint32_t G, std::size_t plen,
               bool truncate) {
    RefDir out;
    if (keys.empty() || G <= 1) return out;
    const std::size_t n = keys.size();
    for (std::uint32_t j = 1; j < G; ++j) {
        const std::size_t r = std::min(n - 1, (std::size_t(j) * n) / G);
        const std::string_view s = keys[r].substr(std::min(plen, keys[r].size()));
        std::string_view t = s;
        if (truncate && r > 0) {
            const std::string_view left = keys[r - 1].substr(std::min(plen, keys[r - 1].size()));
            const std::size_t c = lcp(left, s);
            if (c < s.size()) {
                t = s.substr(0, c + 1);
                ++out.shortened;
            } else {
                ++out.fallbacks;
            }
        }
        out.full.push_back(s);
        out.stored.push_back(t);
        out.rank.push_back(r);
    }
    return out;
}

std::uint32_t count_group(const std::vector<std::string_view>& seps, std::string_view q,
                          std::uint32_t G) {
    std::uint32_t c = 0;
    for (const auto& s : seps)
        if (ub_cmp(s, q) <= 0) ++c;
    return std::min<std::uint32_t>(G ? G - 1 : 0, c);
}

// The SHAPE the production search had before the fix: identical binary search, plain char
// comparison. Used ONLY as the negative control below.
std::uint32_t signed_group(const std::vector<std::string_view>& seps, std::string_view q,
                           std::uint32_t G) {
    std::size_t lo = 0, hi = seps.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const std::string_view sep = seps[mid];
        const std::size_t n = std::min(q.size(), sep.size());
        std::size_t i = 0;
        while (i < n && q[i] == sep[i]) ++i;
        const bool sep_le_q = (i == n) ? (sep.size() <= q.size()) : (sep[i] < q[i]);  // signed
        if (sep_le_q) lo = mid + 1; else hi = mid;
    }
    return static_cast<std::uint32_t>(std::min<std::size_t>(G ? G - 1 : 0, lo));
}
// ---------------------------------------------------------------- (1) the audit fixture
void check_minimal_counterexample() {
    std::vector<std::string> owned;
    for (unsigned b : {1u, 127u, 128u, 255u}) owned.push_back(byte_str(b));
    std::vector<std::string_view> keys;
    for (const auto& k : owned) keys.push_back(k);

    for (int form = 0; form < 2; ++form) {
        const bool trunc = form == 1;
        const std::string tag = trunc ? "truncated" : "full";
        const auto model = equivdir::RegionModel::fit(keys.data(), keys.size(), 0, 4, trunc);
        require(model.threshold_count() == 3, tag + ": the fixture stores three thresholds");

        const RefDir ref = ref_fit(keys, 4, 0, trunc);
        std::vector<std::string_view> seps;
        for (std::size_t i = 0; i < model.threshold_count(); ++i) seps.push_back(model.separator(i));
        require(seps.size() == ref.stored.size(), tag + ": threshold count matches the reference");
        std::size_t sep_mismatch = 0;
        for (std::size_t i = 0; i < seps.size() && i < ref.stored.size(); ++i)
            if (seps[i] != ref.stored[i]) ++sep_mismatch;
        require(sep_mismatch == 0, tag + ": the stored thresholds equal the reference's");

        std::uint64_t mismatches = 0, inversions = 0, signed_disagreements = 0;
        std::uint32_t previous = 0;
        for (unsigned b = 0; b < 256; ++b) {
            const std::string q = byte_str(b);
            const std::uint32_t expected = count_group(ref.stored, q, 4);
            const std::uint32_t actual = model.predict_group(q);
            if (actual != expected) ++mismatches;
            if (b && actual < previous) ++inversions;
            previous = actual;
            if (signed_group(ref.stored, q, 4) != expected) ++signed_disagreements;
        }
        require(mismatches == 0,
                tag + ": every byte 0x00..0xff is placed by the independent count (" +
                    std::to_string(mismatches) + " mismatches)");
        require(inversions == 0, tag + ": the placement never decreases over 0x00..0xff");
        require(signed_disagreements > 0,
                tag + ": the signed-order replica is caught by the same reference (coverage)");
    }
}

// ---------------------------------------------------------------- (2) prefix sweep
// Byte keys that share a prefix and decide their order at a byte crossing 0x7f/0x80, so the
// search really has to compare through the boundary. Many groups, so the binary search branches.
void check_prefix_sweep() {
    std::vector<std::string> owned;
    for (unsigned b : {0u, 1u, 0x3fu, 0x40u, 0x7eu, 0x7fu, 0x80u, 0x81u, 0xbfu, 0xc0u, 0xfeu,
                       0xffu, 0x7fu, 0x80u})
        owned.push_back(std::string("aa") + byte_str(b));
    std::sort(owned.begin(), owned.end(), [](const std::string& a, const std::string& b) {
        return ub_cmp(a, b) < 0;
    });
    owned.erase(std::unique(owned.begin(), owned.end()), owned.end());
    std::vector<std::string_view> keys;
    for (const auto& k : owned) keys.push_back(k);

    for (int form = 0; form < 2; ++form) {
        const bool trunc = form == 1;
        const std::string tag = trunc ? "truncated" : "full";
        const std::uint32_t G = 8;
        const std::size_t plen = 2;   // "aa" is shared by every key of the fixture
        const auto model = equivdir::RegionModel::fit(keys.data(), keys.size(), plen, G, trunc);
        const RefDir ref = ref_fit(keys, G, plen, trunc);

        std::vector<std::string_view> seps;
        for (std::size_t i = 0; i < model.threshold_count(); ++i) seps.push_back(model.separator(i));
        require(seps.size() >= 4, tag + ": the fixture holds enough thresholds to branch");

        std::uint64_t mismatches = 0, inversions = 0, signed_disagreements = 0;
        std::uint32_t previous = 0;
        for (unsigned b = 0; b < 256; ++b) {
            const std::string q = std::string("aa") + byte_str(b);
            const std::string_view suffix(q.data() + plen, q.size() - plen);
            const std::uint32_t expected = count_group(ref.stored, suffix, G);
            const std::uint32_t actual = model.predict_group(q);
            if (actual != expected) ++mismatches;
            if (b && actual < previous) ++inversions;
            previous = actual;
            if (signed_group(ref.stored, suffix, G) != expected) ++signed_disagreements;
        }
        require(mismatches == 0,
                tag + ": the prefixed sweep matches the independent count (" +
                    std::to_string(mismatches) + " mismatches)");
        require(inversions == 0, tag + ": no group inversion over the prefixed sweep");
        require(signed_disagreements > 0,
                tag + ": the sweep covers the bytes where signed and unsigned disagree");
    }
}

// ---------------------------------------------------------------- (3) region level
void check_region_semantics(ModelBackend backend, const std::string& tag) {
    Config c = e25sw::baseline_config(32);
    c.model_backend = backend;
    c.fixed_groups = 8;

    std::vector<Pair> keys;
    auto add = [&keys](const std::string& k) { keys.emplace_back(k, keys.size() + 1); };
    add("");
    add(std::string(1, '\0'));
    add(std::string("\0\0", 2));
    add(byte_str(0x7f));
    add(byte_str(0x80));
    add(byte_str(0xff));
    for (unsigned b : {0u, 0x7fu, 0x80u, 0xffu}) add(std::string("pfx") + byte_str(b));
    for (unsigned b : {1u, 0x80u, 0xffu}) add(std::string("pfx") + byte_str(b) + "tail");
    add(std::string(4096, 'L'));
    add(std::string(4095, 'L') + static_cast<char>(0xff));
    for (int i = 0; i < 6; ++i) add("dup" + std::string(1, static_cast<char>('a' + i)));
    std::sort(keys.begin(), keys.end(), [](const Pair& a, const Pair& b) {
        return ub_cmp(a.first, b.first) < 0;
    });

    Region r(keys, c);
    require(r.uses_depth_dir(), tag + ": the region is on the directory backend");
    const std::uint32_t G = r.groups();

    std::vector<std::string_view> views;
    for (const auto& kv : keys) views.push_back(kv.first);
    const RefDir ref = ref_fit(views, G, r.prefix_length(), backend == ModelBackend::EqualDepthDirTrunc);

    std::uint64_t placement_bad = 0, range_bad = 0, order_bad = 0;
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const std::string_view suffix =
            std::string_view(keys[i].first).substr(std::min(r.prefix_length(), keys[i].first.size()));
        const std::uint32_t expected = count_group(ref.stored, suffix, G);
        const std::uint32_t actual = r.placement_group(keys[i].first);
        if (actual != expected) ++placement_bad;
        if (actual >= G) ++range_bad;
        if (i && actual < previous) ++order_bad;
        previous = actual;
    }
    require(placement_bad == 0, tag + ": placement equals the independent count on every key");
    require(range_bad == 0, tag + ": every group is inside [0, groups)");
    require(order_bad == 0, tag + ": placement is non-decreasing over the region's keys");

    // The range invariant, on probes over the whole byte domain: with g = count(probe), every key
    // whose group is below g is below the probe and every key above g is above it.
    std::vector<std::string> probe_owned;
    for (unsigned b = 0; b < 256; ++b) probe_owned.push_back(std::string("pfx") + byte_str(b));
    for (unsigned b = 0; b < 256; b += 17) probe_owned.push_back(std::string(1, static_cast<char>(b)));
    std::uint64_t bracket_bad = 0;
    for (const auto& p : probe_owned) {
        const std::string_view suffix =
            std::string_view(p).substr(std::min(r.prefix_length(), p.size()));
        if (ub_cmp(p, keys.front().first) < 0 || ub_cmp(keys.back().first, p) < 0) continue;
        const std::uint32_t g = count_group(ref.stored, suffix, G);
        for (const auto& kv : keys) {
            const std::string_view ks =
                std::string_view(kv.first).substr(std::min(r.prefix_length(), kv.first.size()));
            const std::uint32_t kg = count_group(ref.stored, ks, G);
            const int cmp = ub_cmp(kv.first, p);
            if (kg < g && !(cmp < 0)) ++bracket_bad;
            if (kg > g && !(cmp > 0)) ++bracket_bad;
        }
    }
    require(bracket_bad == 0, tag + ": the range bracket holds on every probe");
}
// ---------------------------------------------------------------- (4) index level
struct RefIndex {
    std::vector<Pair> rows;   // sorted by unsigned bytes, live keys only

    void sort_it() {
        std::sort(rows.begin(), rows.end(), [](const Pair& a, const Pair& b) {
            return ub_cmp(a.first, b.first) < 0;
        });
    }
    void put(const std::string& k, std::uint64_t v) {
        for (auto& r : rows)
            if (r.first == k) { r.second = v; return; }
        rows.emplace_back(k, v);
        sort_it();
    }
    void erase(const std::string& k) {
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&k](const Pair& r) { return r.first == k; }),
                   rows.end());
    }
    const Pair* find(const std::string& k) const {
        for (const auto& r : rows)
            if (r.first == k) return &r;
        return nullptr;
    }
};

std::vector<Pair> byte_domain_keys() {
    std::vector<Pair> out;
    std::uint64_t v = 1;
    auto add = [&out, &v](std::string k) { out.emplace_back(std::move(k), v++); };
    for (unsigned b = 0; b < 256; ++b) add(std::string("k") + byte_str(b));
    for (unsigned b : {0u, 0x7fu, 0x80u, 0xffu}) {
        add(std::string("k") + byte_str(b) + "x");
        add(std::string("k") + byte_str(b) + std::string(1, '\0'));
    }
    out.emplace_back(std::string(), v++);
    out.emplace_back(std::string(1, '\0'), v++);
    out.emplace_back(std::string("\0\0", 2), v++);
    for (unsigned b = 0x70; b <= 0x90; ++b) add(std::string("pfx") + byte_str(b));
    // Long keys, including one that ends on a high byte: they do not fit a sub-block, so they
    // exercise the oversized placement, the scan materialisation and the erase path with the
    // directory mapping.
    add(std::string(4095, 'L') + "M");
    add(std::string(4095, 'L') + byte_str(0xff));
    return out;
}

void check_index_semantics(ModelBackend backend, const std::string& tag) {
    Config c = e25sw::baseline_config(32);
    c.model_backend = backend;
    c.multiblock = true;
    c.reuse_group_ids = true;
    c.region_keys = 64;   // several regions, each with several groups

    const Mode full{Route::Full, Prefetch::Off, true, false, OvSearch::Hybrid, 32, false};
    const Mode interval{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

    RefIndex ref;
    auto idx = std::make_unique<Index>(c);
    {
        // CALLING CONVENTION, asserted rather than assumed: bulk_load takes the keys ALREADY in
        // the index's own order, which is unsigned-byte order. `std::string::operator<` compares
        // char, and char is signed here, so it is NOT a valid sort for keys with bytes >= 0x80;
        // the ASCII corpora hide that, which is why the fixture is sorted with the index's own
        // compare() and the result is checked below.
        std::vector<Pair> input = byte_domain_keys();
        std::sort(input.begin(), input.end(),
                  [](const Pair& a, const Pair& b) { return compare(a.first, b.first) < 0; });
        bool ascending = true;
        for (std::size_t i = 1; i < input.size(); ++i)
            if (!(compare(input[i - 1].first, input[i].first) < 0)) ascending = false;
        require(ascending, tag + ": the fixture is ascending in the index's unsigned order");
        for (const auto& kv : input) ref.rows.push_back(kv);
        ref.sort_it();
        // The input itself exists only inside this scope: after it ends, nothing in the index may
        // still point into it (the lifetime rule the LITS round had to fix, checked here for the
        // directory's arena and the demand preset).
        idx->bulk_load(input);
    }

    require(idx->size() == ref.rows.size(), tag + ": the index holds exactly the input keys");

    std::uint64_t find_bad = 0, absent_bad = 0;
    for (const auto& kv : ref.rows) {
        const auto a = idx->find(kv.first, full);
        const auto b = idx->find(kv.first, interval);
        if (!a.found || a.value != kv.second) ++find_bad;
        if (!b.found || b.value != kv.second) ++find_bad;
        const std::string absent = kv.first + std::string("\x80\x81", 2);
        if (idx->find(absent, interval).found) ++absent_bad;
    }
    require(find_bad == 0, tag + ": every stored key is found with its exact value (both routes)");
    require(absent_bad == 0, tag + ": keys extended past every stored key miss");

    // Scans, against the independent ordered reference, from the beginning and from arbitrary
    // starts (stored keys and keys that are not stored).
    auto scan_matches = [&](const std::string& start) {
        const std::vector<Pair> got = idx->scan(start, ref.rows.size() + 8);
        std::size_t first = 0;
        while (first < ref.rows.size() && ub_cmp(ref.rows[first].first, start) < 0) ++first;
        const std::size_t want = ref.rows.size() - first;
        if (got.size() != want) return false;
        for (std::size_t i = 0; i < want; ++i)
            if (got[i].first != ref.rows[first + i].first ||
                got[i].second != ref.rows[first + i].second)
                return false;
        return true;
    };
    require(scan_matches(std::string()), tag + ": a scan from the beginning returns every row in order");
    for (std::size_t i = 0; i < ref.rows.size(); i += 17)
        require(scan_matches(ref.rows[i].first),
                tag + ": a scan from a stored key returns the tail in unsigned order");
    for (unsigned b : {0u, 0x7fu, 0x80u, 0xbfu, 0xffu}) {
        const std::string start = std::string("k") + byte_str(b) + "m";   // never stored
        require(scan_matches(start), tag + ": a scan from an unstored start lands on the first key above it");
    }

    // Churn: inserts, erases, value updates and re-inserts, then the same checks again. This is
    // where placements have to go through the directory's map a second time instead of the
    // initial build.
    std::vector<std::string> fresh;
    for (int i = 0; i < 200; ++i) {
        const std::string k = "fresh" + std::to_string(i) + byte_str(static_cast<unsigned>(i % 256));
        fresh.push_back(k);
        const std::uint64_t v = 0x50000000ULL + i;
        if (!idx->insert(k, v)) { require(false, tag + ": a fresh insert was refused"); }
        ref.put(k, v);
    }
    std::size_t erased = 0;
    for (std::size_t i = 0; i < ref.rows.size() && erased < 100; ++i) {
        if (ref.rows[i].first.rfind("k", 0) != 0) continue;   // erase single-byte-family keys
        const std::string victim = ref.rows[i].first;
        if (!idx->erase(victim)) { require(false, tag + ": an erase of a live key was refused"); }
        ref.erase(victim);
        --i;
        ++erased;
    }
    require(erased == 100, tag + ": the churn erased the keys it meant to");
    for (std::size_t i = 0; i < 50; ++i) {
        const std::string k = fresh[i];
        const std::uint64_t v = 0x60000000ULL + i;
        ref.put(k, v);
        if (!idx->update(k, v)) { require(false, tag + ": an update of a live key was refused"); }
    }
    for (std::size_t i = 0; i < 50; ++i) {
        const std::string k = fresh[100 + i];
        idx->erase(k);
        ref.erase(k);
        const std::uint64_t v = 0x70000000ULL + i;
        if (!idx->insert(k, v)) { require(false, tag + ": a re-insert was refused"); }
        ref.put(k, v);
    }

    require(idx->size() == ref.rows.size(), tag + ": the size follows the churn exactly");
    std::uint64_t post_bad = 0;
    for (const auto& kv : ref.rows) {
        const auto a = idx->find(kv.first, interval);
        if (!a.found || a.value != kv.second) ++post_bad;
    }
    require(post_bad == 0, tag + ": after the churn every live key is found with its exact value");
    require(scan_matches(std::string()),
            tag + ": after the churn a full scan still matches the reference row for row");
    for (std::size_t i = 0; i < ref.rows.size(); i += 23)
        require(scan_matches(ref.rows[i].first),
                tag + ": after the churn a scan from a stored key still matches");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_depth_dir_order KIND N [TRANCOCSV]");
        const std::string kind = argv[1];
        const std::string trcsv = argc > 3 ? argv[3] : "";

        check_minimal_counterexample();
        check_prefix_sweep();
        check_region_semantics(ModelBackend::EqualDepthDir, "EqualDepthDir");
        check_region_semantics(ModelBackend::EqualDepthDirTrunc, "EqualDepthDirTrunc");
        check_index_semantics(ModelBackend::EqualDepthDir, "EqualDepthDir");
        check_index_semantics(ModelBackend::EqualDepthDirTrunc, "EqualDepthDirTrunc");

        // The two corpora are ASCII, so they add a differential over real key distributions
        // rather than the high-byte coverage above: every stored key must still be found and the
        // two forms must answer identically on them.
        const std::size_t n = std::stoull(argv[2]);
        if (n > 0) {
            std::vector<Pair> keys = make_data(kind, n, trcsv);
            std::sort(keys.begin(), keys.end(),
                      [](const Pair& a, const Pair& b) { return a.first < b.first; });
            for (int form = 0; form < 2; ++form) {
                Config c = e25sw::baseline_config(32);
                c.model_backend = form ? ModelBackend::EqualDepthDirTrunc
                                       : ModelBackend::EqualDepthDir;
                Index idx(c);
                idx.bulk_load(keys);
                const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32,
                                 false};
                std::uint64_t bad = 0;
                for (const auto& kv : keys) {
                    const auto r = idx.find(kv.first, qmode);
                    if (!r.found || r.value != kv.second) ++bad;
                }
                require(bad == 0, std::string(form ? "truncated" : "full") +
                                      ": every corpus key is found on " + kind);
            }
        }

        std::cout << "checks=" << checks << " failures=" << failures << "\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << "\n";
        return 2;
    }
}

// Gate for the TRUNCDIR candidate (ModelBackend::EqualDepthDirTrunc), 2026-10-11.
//
// WHAT THIS GATE IS. TRUNCDIR_ENTRY_DESIGN.md is the frozen spec of this round: the
// EqualDepthDir separator directory storing, for each threshold, the SHORTEST prefix of its
// suffix that still compares greater than the key immediately to its left (section 2), with
// the routing equivalence, monotonicity and range-invariant lemmas that justify it (section 3).
// This file is the section-5 gate: an independent implementation of the section-2 rule, the
// per-region invariant and bracketing checks, two negative controls that must fire, the size
// statistics the round reports, and the same check shapes the untruncated gate uses (synthetic
// depth property, duplicate keys, domain and refusals, dynamic insert/erase, full-corpus
// per-key routing against an independent reference).
//
// WHAT IT IS NOT. No timing is taken anywhere here, and no count printed by this file is a
// speedup. Every reference below is written from the frozen text rather than from the model:
// this file never reads the model's internals, and the model's answers, the live Region's
// placement and this file's counts are compared against each other instead of trusted.
//
// The two negative controls are the evidence that the checks are not vacuous:
//   (a) cut one stored separator one byte too far -- it no longer beats its left key, and the
//       construction property of section 3 must report it;
//   (b) swap two adjacent stored separators -- the non-decreasing check of lemma B must report
//       it, and the search over the permuted list must disagree with the order-independent
//       count of the same list, which is the failure mode lemma B exists to rule out.
//
// Usage: check_depth_dir_trunc KIND N [TRANCOCSV]     KIND: mixed|skew|tranco

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

bool monotone(const std::vector<std::uint32_t>& groups) {
    for (std::size_t i = 1; i < groups.size(); ++i)
        if (groups[i] < groups[i - 1]) return false;
    return true;
}

// ---------------------------------------------------------------- the index's own order
// Unsigned bytes, shorter-is-smaller on a tie. Written here so the checks below do not depend
// on the comparison the model happens to use.
int ub_compare(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    const int c = n ? std::memcmp(a.data(), b.data(), n) : 0;
    if (c) return c < 0 ? -1 : 1;
    return (a.size() > b.size()) - (a.size() < b.size());
}

std::size_t lcp(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

// ---------------------------------------------------------------- section-2 reference
//
// One region's directory, rebuilt from the frozen text only: the ranks of section 2, the full
// separators s_j, the stored separators t_j, and the two counters the round reports
// (duplicate-run fallbacks and thresholds that really got shorter).
struct DirShape {
    std::uint32_t groups = 0;
    std::size_t n = 0;
    std::size_t prefix_len = 0;
    bool duplicate_suffixes = false;
    std::vector<std::string> key;     // full keys, ascending
    std::vector<std::string> suffix;  // x_i = key_i with the region prefix stripped
    std::vector<std::size_t> rank;    // r_j for j = 1..G-1 (entry j-1)
    std::vector<std::string> full;    // s_j
    std::vector<std::string> trunc;   // t_j of section 2
};

DirShape build_shape(const std::vector<Pair>& sorted, std::size_t prefix_len,
                     std::uint32_t groups) {
    DirShape sh;
    sh.groups = groups;
    sh.n = sorted.size();
    for (const auto& kv : sorted) sh.key.push_back(kv.first);
    if (sh.n > 0) {
        // The region prefix really is shared by every key the region accepts; clamp it the way
        // the model does so a caller-supplied prefix cannot hide a deciding byte.
        sh.prefix_len = std::min(prefix_len, lcp(sh.key.front(), sh.key.back()));
        for (const auto& k : sh.key)
            sh.suffix.push_back(k.substr(std::min(sh.prefix_len, k.size())));
        for (std::size_t i = 1; i < sh.n; ++i)
            if (ub_compare(sh.suffix[i - 1], sh.suffix[i]) == 0) sh.duplicate_suffixes = true;
    }
    if (groups <= 1 || sh.n <= 1) return sh;   // the model's boundary statuses store nothing

    for (std::uint32_t j = 1; j < groups; ++j)
        sh.rank.push_back(std::min(sh.n - 1, (std::size_t(j) * sh.n) / groups));

    for (std::size_t idx = 0; idx < sh.rank.size(); ++idx) {
        const std::size_t r = sh.rank[idx];
        const std::string& s = sh.suffix[r];
        sh.full.push_back(s);
        std::string t = s;
        if (r > 0) {
            const std::size_t c = lcp(sh.suffix[r - 1], s);
            if (c < s.size()) t = s.substr(0, c + 1);   // shortest prefix that still beats it
            // else: s_j is a prefix of its left key (a run of equal suffixes at the band
            // boundary), so no shorter prefix can win and s_j is kept -- counted below.
        }
        sh.trunc.push_back(std::move(t));
    }
    return sh;
}

std::uint64_t fallback_count(const DirShape& sh) {
    std::uint64_t n = 0;
    for (std::size_t idx = 0; idx < sh.rank.size(); ++idx) {
        const std::size_t r = sh.rank[idx];
        if (r > 0 && lcp(sh.suffix[r - 1], sh.full[idx]) == sh.full[idx].size()) ++n;
    }
    return n;
}

std::uint64_t shortened_count(const DirShape& sh) {
    std::uint64_t n = 0;
    for (std::size_t idx = 0; idx < sh.full.size(); ++idx)
        if (sh.trunc[idx].size() < sh.full[idx].size()) ++n;
    return n;
}

std::uint64_t bytes_of(const std::vector<std::string>& v) {
    std::uint64_t n = 0;
    for (const auto& s : v) n += s.size();
    return n;
}

// ---------------------------------------------------------------- the count of section 3
// g = #{ j : t_j <= q } with the answer clamped into [0, G-1]. Order-independent by
// construction: this is the reference the search is compared against, not a search itself.
std::uint32_t count_group(const std::vector<std::string>& seps, std::uint32_t groups,
                          std::string_view q) {
    std::size_t c = 0;
    for (const auto& s : seps)
        if (ub_compare(s, q) <= 0) ++c;
    const std::size_t last = groups ? std::size_t(groups) - 1 : 0;
    return static_cast<std::uint32_t>(std::min(c, last));
}

// The search SHAPE the model uses (first entry greater than q, clamped), kept separate from the
// count because the negative control (b) exists precisely to show the two can disagree when the
// list is not non-decreasing.
std::uint32_t binary_group(const std::vector<std::string>& seps, std::uint32_t groups,
                           std::string_view q) {
    std::size_t lo = 0, hi = seps.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (ub_compare(seps[mid], q) <= 0) lo = mid + 1; else hi = mid;
    }
    const std::size_t last = groups ? std::size_t(groups) - 1 : 0;
    return static_cast<std::uint32_t>(std::min(lo, last));
}

// The same count for every stored key at once: keys and separators are both ascending, so one
// two-pointer sweep is exact and does not read like a search anyone could mistake for the model.
std::vector<std::uint32_t> counts_for_keys(const std::vector<std::string>& seps,
                                           const DirShape& sh) {
    std::vector<std::uint32_t> out(sh.n, 0);
    const std::size_t last = sh.groups ? std::size_t(sh.groups) - 1 : 0;
    std::size_t j = 0;
    for (std::size_t i = 0; i < sh.n; ++i) {
        while (j < seps.size() && ub_compare(seps[j], sh.suffix[i]) <= 0) ++j;
        out[i] = static_cast<std::uint32_t>(std::min(j, last));
    }
    return out;
}

// Group membership derived from the RANKS of section 2, not from any placement API:
// band(i) = #{ j in 1..G-1 : floor(j*n/G) <= i }.
std::vector<std::uint32_t> band_of_key(const DirShape& sh) {
    std::vector<std::uint32_t> out(sh.n, 0);
    std::vector<std::uint32_t> steps(sh.n, 0);
    for (std::size_t r : sh.rank)
        if (r < sh.n) ++steps[r];
    std::uint32_t acc = 0;
    for (std::size_t i = 0; i < sh.n; ++i) {
        acc += steps[i];
        out[i] = acc;
    }
    return out;
}

// ---------------------------------------------------------------- section-3 invariants
struct InvariantReport {
    std::uint64_t thresholds = 0;
    std::uint64_t prefix_violations = 0;        // t_j is not a prefix of s_j, or exceeds it
    std::uint64_t order_violations = 0;         // lemma B: t_j > t_{j+1}
    std::uint64_t construction_violations = 0;  // t_j neither beats its left key nor is the fallback
    std::uint64_t tail_violations = 0;          // a rank below r_j holds a suffix that does not lose
    std::uint64_t fallbacks = 0;
    std::uint64_t shortened = 0;
};

// Checks ONE candidate separator list (the model's, the frozen text's, or a deliberately
// corrupted copy) against the keys the region is built from. The caller decides whether a
// non-zero counter is a failure or the expected result of a negative control.
InvariantReport check_separator_list(const DirShape& sh, const std::vector<std::string>& seps) {
    InvariantReport rep;
    rep.thresholds = seps.size();
    if (seps.size() != sh.rank.size()) {
        ++rep.prefix_violations;   // a list of the wrong length cannot be the frozen directory
        return rep;
    }
    for (std::size_t idx = 0; idx < seps.size(); ++idx) {
        const std::string& t = seps[idx];
        const std::string& s = sh.full[idx];
        if (t.size() > s.size() || s.compare(0, t.size(), t) != 0 || ub_compare(t, s) > 0)
            ++rep.prefix_violations;
        if (t.size() < s.size()) ++rep.shortened;

        const std::size_t r = sh.rank[idx];
        if (r > 0) {
            const std::string& left = sh.suffix[r - 1];
            if (lcp(left, s) == s.size()) {
                // The single case section 2 keeps s_j itself: a run of equal suffixes at the
                // boundary of a band, where no shorter prefix can beat the left key. The
                // stored value must then be s_j, and the equal key at rank r-1 is the only one
                // allowed not to compare strictly below it.
                if (t != s || ub_compare(left, t) != 0) {
                    ++rep.construction_violations;
                } else {
                    ++rep.fallbacks;
                    // Below a fallback threshold the keys may be EQUAL to t (that run of equal
                    // suffixes is what forced the fallback), so the rule here is that none of
                    // them is greater -- the strict form belongs to the branch below.
                    for (std::size_t i = 0; i < r; ++i)
                        if (ub_compare(sh.suffix[i], t) > 0) ++rep.tail_violations;
                }
            } else {
                if (!(ub_compare(t, left) > 0)) ++rep.construction_violations;
                // Every rank strictly below r_j must hold a suffix that loses to t_j. With
                // distinct suffixes this is the tight statement of "the prefix still separates
                // the bands"; the equal case is the fallback branch above.
                for (std::size_t i = 0; i < r; ++i)
                    if (!(ub_compare(sh.suffix[i], t) < 0)) ++rep.tail_violations;
            }
        }
        if (idx + 1 < seps.size() && ub_compare(t, seps[idx + 1]) > 0) ++rep.order_violations;
    }
    return rep;
}

// ---------------------------------------------------------------- lemma C bracketing
struct BracketReport {
    std::uint64_t probes = 0;
    std::uint64_t before_violations = 0;   // a key in a group before g that is not below q
    std::uint64_t after_violations = 0;    // a key in a group after g that is not above q
    std::uint64_t equal_before = 0;        // a stored key equal to q behind g (duplicate runs only)
    std::uint64_t equal_after = 0;
    std::uint64_t stored_probes = 0;
    std::uint64_t stored_group_mismatch = 0;   // placement of a stored key differs from the answer
};

// Group membership comes from the ranks of section 2 alone (band(i) = #{ j : r_j <= i }), so
// the group a key belongs to is [r_g, r_{g+1}) and the two sets to check are the keys before and
// after that interval -- contiguous in the sorted keys, and therefore represented exactly by
// their extremal member. `full_scan` walks every member anyway for small regions, where the
// equivalence itself is cheap to demonstrate.
BracketReport bracket_check(const DirShape& sh, const std::vector<std::string>& seps,
                            const std::vector<std::string>& probes, bool full_scan,
                            const Region* live) {
    BracketReport rep;
    if (sh.groups <= 1 || seps.empty() || sh.n == 0) return rep;
    for (const std::string& q : probes) {
        ++rep.probes;
        const std::uint32_t g = count_group(seps, sh.groups, q);
        const std::size_t lo = (g == 0) ? 0 : sh.rank[std::size_t(g) - 1];
        const std::size_t hi = (std::size_t(g) + 1 < sh.groups) ? sh.rank[std::size_t(g)] : sh.n;

        if (full_scan) {
            for (std::size_t i = 0; i < lo; ++i) {
                const int c = ub_compare(sh.suffix[i], q);
                if (c < 0) continue;
                if (c == 0) ++rep.equal_before; else ++rep.before_violations;
            }
            for (std::size_t i = hi; i < sh.n; ++i) {
                const int c = ub_compare(sh.suffix[i], q);
                if (c > 0) continue;
                if (c == 0) ++rep.equal_after; else ++rep.after_violations;
            }
        } else {
            if (lo > 0) {                          // widest key of the groups before g
                const int c = ub_compare(sh.suffix[lo - 1], q);
                if (c == 0) ++rep.equal_before; else if (c > 0) ++rep.before_violations;
            }
            if (hi < sh.n) {                       // narrowest key of the groups after g
                const int c = ub_compare(sh.suffix[hi], q);
                if (c == 0) ++rep.equal_after; else if (c < 0) ++rep.after_violations;
            }
        }

        // A probe that is a stored key must be answered by the group that holds that key; the
        // placement API is asked, so the claim is about the live index, not about this file.
        const auto it = std::lower_bound(sh.suffix.begin(), sh.suffix.end(), q,
            [](const std::string& a, const std::string& b) { return ub_compare(a, b) < 0; });
        if (it != sh.suffix.end() && ub_compare(*it, q) == 0) {
            ++rep.stored_probes;
            if (live) {
                const std::string full_key = sh.key.front().substr(0, sh.prefix_len) + q;
                if (live->placement_group(full_key) != g) ++rep.stored_group_mismatch;
            }
        }
    }
    return rep;
}

// Probes of section 5: every stored separator prefix (and one byte past it, and one byte short,
// and a last-byte mutation), every full separator, and the byte-domain corners.
std::vector<std::string> make_probes(const DirShape& sh) {
    std::vector<std::string> probes;
    probes.emplace_back();
    probes.push_back(std::string(1, '\0'));
    probes.push_back(std::string("\0\0", 2));
    probes.push_back(std::string(1, static_cast<char>(0x80)));
    probes.push_back(std::string(1, static_cast<char>(0xff)));
    probes.push_back(std::string("\0mid\0", 5));
    probes.push_back(std::string("\x80", 1) + "tail");
    for (std::size_t idx = 0; idx < sh.trunc.size(); ++idx) {
        const std::string& t = sh.trunc[idx];
        const std::string& s = sh.full[idx];
        probes.push_back(t);
        if (t.size() < s.size()) {
            probes.push_back(t + s[t.size()]);
        } else {
            probes.push_back(t + std::string(1, '\0'));
            probes.push_back(t + std::string(1, static_cast<char>(0xff)));
        }
        if (!t.empty()) {
            probes.push_back(t.substr(0, t.size() - 1));
            std::string flipped = t;
            flipped.back() = static_cast<char>(t.back() == '\0' ? 0x01 : 0x00);
            probes.push_back(std::move(flipped));
        }
        probes.push_back(s);
    }
    return probes;
}

// ---------------------------------------------------------------- negative controls
// (a) One byte too far: cut t_j back to the longest prefix that still cannot beat the left key.
//     In both branches of section 2 that produces a value that fails the construction property:
//     a proper prefix of the left key, a value equal to it, or a proper prefix of the fallback.
std::vector<std::string> cut_too_far(const DirShape& sh, std::size_t idx) {
    std::vector<std::string> out = sh.trunc;
    const std::size_t r = sh.rank[idx];
    const std::string& s = sh.full[idx];
    if (r == 0 || s.empty()) return out;
    const std::size_t cut = std::min(lcp(sh.suffix[r - 1], s), s.size() - 1);
    out[idx] = s.substr(0, cut);
    return out;
}

// What the two controls actually observed, so the transcript carries the evidence rather than
// only a green failure count.
struct NegativeControls {
    std::uint64_t runs = 0;
    std::uint64_t cut_violations = 0;      // construction violations the too-short cut produced
    std::uint64_t swap_order = 0;          // lemma-B violations the swap produced
    std::uint64_t swap_disagreements = 0;  // probes where the search disagreed with the count
};

NegativeControls g_neg;

void check_negative_controls(const DirShape& sh, const std::string& tag) {
    if (sh.rank.empty()) {
        require(false, "negative controls need a fitted region: " + tag);
        return;
    }
    ++g_neg.runs;
    const auto clean = check_separator_list(sh, sh.trunc);
    require(clean.construction_violations == 0 && clean.order_violations == 0,
            "the frozen separators pass the invariant checker: " + tag);

    std::size_t cut_idx = sh.rank.size();
    for (std::size_t idx = 0; idx < sh.rank.size(); ++idx)
        if (sh.rank[idx] > 0 && !sh.full[idx].empty()) { cut_idx = idx; break; }
    require(cut_idx < sh.rank.size(), "cut control has a threshold with a left key: " + tag);
    if (cut_idx < sh.rank.size()) {
        const auto corrupted = cut_too_far(sh, cut_idx);
        const auto rep = check_separator_list(sh, corrupted);
        g_neg.cut_violations += rep.construction_violations;
        require(rep.construction_violations > 0,
                "one byte too far is caught by the construction property: " + tag);
    }

    std::size_t swap_idx = sh.rank.size();
    for (std::size_t idx = 0; idx + 1 < sh.trunc.size(); ++idx)
        if (ub_compare(sh.trunc[idx], sh.trunc[idx + 1]) < 0) { swap_idx = idx; break; }
    require(swap_idx + 1 < sh.trunc.size(),
            "swap control has a strictly rising adjacent pair: " + tag);
    if (swap_idx + 1 < sh.trunc.size()) {
        auto swapped = sh.trunc;
        std::swap(swapped[swap_idx], swapped[swap_idx + 1]);
        const auto rep = check_separator_list(sh, swapped);
        g_neg.swap_order += rep.order_violations;
        require(rep.order_violations > 0,
                "swapping adjacent separators is caught by lemma B: " + tag);
        std::vector<std::string> probes = sh.suffix;
        probes.push_back(swapped[swap_idx]);
        probes.push_back(swapped[swap_idx + 1]);
        std::uint64_t disagreements = 0;
        for (const auto& q : probes)
            if (binary_group(swapped, sh.groups, q) != count_group(swapped, sh.groups, q))
                ++disagreements;
        g_neg.swap_disagreements += disagreements;
        require(disagreements > 0,
                "the search over the permuted list disagrees with the order-free count: " + tag);
    }
}

// ---------------------------------------------------------------- synthetic depth property
void check_synthetic_depth() {
    Config trunc_cfg = e25sw::baseline_config(32);
    trunc_cfg.model_backend = ModelBackend::EqualDepthDirTrunc;
    trunc_cfg.fixed_groups = 8;
    Config full_cfg = trunc_cfg;
    full_cfg.model_backend = ModelBackend::EqualDepthDir;
    e25sw::require_only_switches_differ(full_cfg, trunc_cfg, {"model_backend"});

    std::vector<Pair> keys;
    for (int i = 0; i < 100; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "synthetic%04d", i);
        keys.emplace_back(buf, i + 1);
    }
    std::sort(keys.begin(), keys.end());

    Region::reset_depth_dir_stats();
    Region rt(keys, trunc_cfg);
    Region ru(keys, full_cfg);
    require(rt.uses_depth_dir() && ru.uses_depth_dir(),
            "synthetic regions are on the directory backend");
    require(rt.depth_dir_truncated() && !ru.depth_dir_truncated(),
            "synthetic regions report the stored form they were built with");
    require(rt.depth_dir_status() == equivdir::Status::Ok, "synthetic truncated region is fitted");
    require(rt.groups() == 8 && ru.groups() == 8, "synthetic regions have the requested 8 groups");

    const std::size_t plen = lcp(keys.front().first, keys.back().first);
    const DirShape sh = build_shape(keys, plen, rt.groups());
    require(sh.rank.size() == 7, "synthetic shape has 7 thresholds");

    std::vector<std::string_view> views;
    for (const auto& kv : keys) views.push_back(kv.first);
    const auto mt = equivdir::RegionModel::fit(views.data(), views.size(), plen, rt.groups(), true);
    const auto mu = equivdir::RegionModel::fit(views.data(), views.size(), plen, rt.groups(), false);
    require(mt.truncated() && !mu.truncated(), "the model reports the form the flag asked for");
    require(mt.threshold_count() == sh.rank.size() && mu.threshold_count() == sh.rank.size(),
            "the model stores one threshold per rank boundary");
    for (std::size_t idx = 0; idx < sh.rank.size(); ++idx) {
        require(mt.separator(idx) == sh.trunc[idx],
                "the model stores the section-2 separator " + std::to_string(idx));
        require(mu.separator(idx) == sh.full[idx],
                "the untruncated model stores the full separator " + std::to_string(idx));
    }
    require(mt.separator_bytes() == bytes_of(sh.trunc), "the model reports its stored bytes");
    require(mu.separator_bytes() == bytes_of(sh.full), "the plain model reports the full bytes");

    const auto inv = check_separator_list(sh, sh.trunc);
    require(inv.prefix_violations == 0 && inv.order_violations == 0 &&
            inv.construction_violations == 0 && inv.tail_violations == 0,
            "synthetic separators satisfy the section-3 invariants");
    require(inv.fallbacks == fallback_count(sh), "the fallback count agrees with the frozen rule");

    const auto ct = counts_for_keys(sh.trunc, sh);
    const auto cf = counts_for_keys(sh.full, sh);
    std::vector<std::size_t> load(8, 0);
    std::uint64_t routing_differ = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto& k = keys[i].first;
        require(mt.predict_group(k) == ct[i], "the truncated model answers the independent count");
        require(mu.predict_group(k) == cf[i], "the plain model answers the independent count");
        require(rt.placement_group(k) == ct[i], "the live region places the key by the same count");
        require(ru.placement_group(k) == cf[i], "the plain live region places it by the full count");
        if (ct[i] != cf[i]) ++routing_differ;
        ++load[std::min<std::size_t>(ct[i], 7)];
    }
    require(routing_differ == 0, "truncated and untruncated routing agree on every stored key");
    const std::size_t want[8] = {12, 13, 12, 13, 12, 13, 12, 13};
    for (std::size_t g = 0; g < 8; ++g)
        require(load[g] == want[g], "synthetic depth: group " + std::to_string(g) + " holds " +
                                        std::to_string(load[g]) + ", expected " +
                                        std::to_string(want[g]));

    std::vector<std::string> probes = sh.suffix;
    const auto extra = make_probes(sh);
    probes.insert(probes.end(), extra.begin(), extra.end());
    const auto br = bracket_check(sh, sh.trunc, probes, true, &rt);
    require(br.before_violations == 0 && br.after_violations == 0,
            "bracketing holds on every synthetic probe");
    require(br.stored_group_mismatch == 0, "stored-key probes are answered by their own group");
    require(br.equal_before == 0 && br.equal_after == 0,
            "no duplicate-run exception is needed on distinct synthetic keys");

    require(rt.placement_group("synthetic!!!!") == 0, "key below the range lands in group 0");
    require(rt.placement_group("syntheticZZZZ") == 7, "key above the range lands in the last group");

    const auto st = Region::depth_dir_stats();
    require(st.regions_truncated == 1, "the stats count the truncated region");
    require(st.threshold_bytes_total == bytes_of(sh.trunc) + bytes_of(sh.full),
            "the stats sum the stored bytes of both forms built here");
    require(st.regions_boundary == 0, "both synthetic regions are fitted, not boundary");

    check_negative_controls(sh, "synthetic");
}

// ---------------------------------------------------------------- equal keys
void check_duplicate_keys() {
    Config trunc_cfg = e25sw::baseline_config(32);
    trunc_cfg.model_backend = ModelBackend::EqualDepthDirTrunc;
    trunc_cfg.fixed_groups = 8;
    Config full_cfg = trunc_cfg;
    full_cfg.model_backend = ModelBackend::EqualDepthDir;

    // 100 keys over 5 distinct values: the band boundaries fall inside runs of equal suffixes,
    // so section 2's fallback branch is exercised for real and some groups MUST stay empty.
    std::vector<Pair> keys;
    for (int i = 0; i < 100; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "dup%d", i % 5);
        keys.emplace_back(buf, i + 1);
    }
    std::sort(keys.begin(), keys.end());

    const DirShape sh = build_shape(keys, lcp(keys.front().first, keys.back().first), 8);
    require(sh.duplicate_suffixes, "the duplicate test really has equal suffixes");
    require(fallback_count(sh) > 0, "the duplicate test exercises the fallback branch of section 2");

    Region::reset_depth_dir_stats();
    Region rt(keys, trunc_cfg);
    Region ru(keys, full_cfg);
    const auto st = Region::depth_dir_stats();
    require(st.regions_truncated == 1, "the duplicate region is the truncated form");
    require(st.regions_with_duplicate_thresholds >= 1,
            "the duplicate thresholds are counted, not hidden");
    require(st.threshold_bytes_total == bytes_of(sh.trunc) + bytes_of(sh.full),
            "the stored bytes of both duplicate regions are accounted for");

    std::vector<std::string_view> views;
    for (const auto& kv : keys) views.push_back(kv.first);
    const auto mt = equivdir::RegionModel::fit(views.data(), views.size(),
                                               sh.prefix_len, rt.groups(), true);
    for (std::size_t idx = 0; idx < sh.rank.size(); ++idx)
        require(mt.separator(idx) == sh.trunc[idx],
                "duplicate region: model stores the section-2 separator " + std::to_string(idx));

    const auto ct = counts_for_keys(sh.trunc, sh);
    const auto cf = counts_for_keys(sh.full, sh);
    std::uint64_t routing_differ = 0;
    std::vector<std::uint32_t> groups;
    std::vector<std::size_t> load(rt.groups(), 0);
    std::uint64_t placement_bad = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto g = rt.placement_group(keys[i].first);
        if (g >= rt.groups()) ++placement_bad;
        if (g != ct[i]) ++placement_bad;
        if (ct[i] != cf[i]) ++routing_differ;
        groups.push_back(g);
        ++load[g];
    }
    require(placement_bad == 0, "duplicate-key placement equals the independent count and stays in range");
    require(routing_differ == 0,
            "lemma A holds on duplicate keys: 0 keys routed differently when truncated");
    require(monotone(groups), "duplicate-key map is non-decreasing");
    std::size_t empty = 0;
    for (auto l : load) if (!l) ++empty;
    require(empty > 0, "duplicate keys leave at least one empty group (reported, not hidden)");

    const auto inv = check_separator_list(sh, sh.trunc);
    require(inv.construction_violations == 0 && inv.order_violations == 0 &&
            inv.tail_violations == 0 && inv.prefix_violations == 0,
            "duplicate separators satisfy the section-3 invariants");
    require(inv.fallbacks == fallback_count(sh),
            "the checker counts exactly the fallbacks the frozen rule produces");

    std::vector<std::string> probes;
    for (const auto& s : sh.suffix) probes.push_back(s);
    const auto extra = make_probes(sh);
    probes.insert(probes.end(), extra.begin(), extra.end());
    const auto br = bracket_check(sh, sh.trunc, probes, true, &rt);
    require(br.before_violations == 0 && br.after_violations == 0,
            "the range brackets hold on duplicate keys");
    require(br.stored_group_mismatch == 0, "stored duplicate keys are answered by their own group");

    check_negative_controls(sh, "duplicates");
}

// ---------------------------------------------------------------- domain + refusals
void check_domain_and_refusals() {
    std::vector<std::string> domain;
    domain.emplace_back("");
    domain.push_back(std::string(1, '\0'));
    domain.push_back(std::string("\0\0", 2));
    domain.push_back(std::string(1, static_cast<char>(0x80)));
    domain.push_back(std::string(1, static_cast<char>(0xff)));
    domain.push_back(std::string(1, static_cast<char>(0x80)) + "tail");
    domain.push_back(std::string(4096, 'L'));
    domain.push_back(std::string(4095, 'L') + "M");
    for (int i = 0; i < 40; ++i)
        domain.push_back(std::string("common-prefix-64-bytes-common-prefix-64-bytes-common-prefix") +
                         static_cast<char>('a' + i % 26) + char('0' + i % 10));
    for (int i = 0; i < 20; ++i)
        domain.push_back(std::string("a") + "0123456" + static_cast<char>('a' + i));

    std::sort(domain.begin(), domain.end());
    domain.erase(std::unique(domain.begin(), domain.end()), domain.end());
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < domain.size(); ++i) pairs.emplace_back(domain[i], i + 1);

    Config trunc_cfg = e25sw::baseline_config(32);
    trunc_cfg.model_backend = ModelBackend::EqualDepthDirTrunc;
    trunc_cfg.multiblock = true;
    trunc_cfg.reuse_group_ids = true;
    trunc_cfg.region_keys = 8;   // several small regions, including single-key ones
    Config full_cfg = trunc_cfg;
    full_cfg.model_backend = ModelBackend::EqualDepthDir;

    Index it(trunc_cfg), iu(full_cfg);
    it.bulk_load(pairs);
    iu.bulk_load(pairs);
    require(it.region_count() == iu.region_count(),
            "both directory forms build the same domain region count");

    std::uint64_t form_bad = 0;
    for (std::size_t r = 0; r < it.region_count(); ++r) {
        if (!it.region(r).uses_depth_dir() || !it.region(r).depth_dir_truncated()) ++form_bad;
        if (!iu.region(r).uses_depth_dir() || iu.region(r).depth_dir_truncated()) ++form_bad;
    }
    require(form_bad == 0, "every live domain region carries the form its backend asked for");

    const Mode full_mode{Route::Full, Prefetch::Off, true, false, OvSearch::Hybrid, 32, false};
    const Mode interval{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
    std::uint64_t answer_differ = 0, placement_differ = 0;
    for (const auto& kv : pairs) {
        const auto a = it.find(kv.first, full_mode);
        const auto b = it.find(kv.first, interval);
        const auto c = iu.find(kv.first, interval);
        require(a.found && a.value == kv.second, "domain find (full route)");
        require(b.found && b.value == kv.second, "domain find (interval route)");
        if (!(c.found && c.value == kv.second)) ++answer_differ;
        const auto rtl = it.locate(kv.first), rul = iu.locate(kv.first);
        if (rtl != rul) {
            ++answer_differ;
        } else if (it.region(rtl).placement_group(kv.first) !=
                   iu.region(rul).placement_group(kv.first)) {
            ++placement_differ;
        }
    }
    require(answer_differ == 0, "both directory forms find the same keys in the same regions");
    require(placement_differ == 0, "both directory forms place the same keys in the same groups");

    std::uint64_t absent_found = 0;
    for (const auto& kv : pairs) {
        const std::string absent = kv.first + "~absent";
        require(!it.find(absent, interval).found, "domain absent probe is a miss");
        if (iu.find(absent, interval).found) ++absent_found;
    }
    require(absent_found == 0, "absent probes miss on both directory forms");

    std::vector<Pair> three{{"aa", 1}, {"ab", 2}, {"ac", 3}};
    {
        Config bad = trunc_cfg;
        bad.canonical = Route::Interval;
        bool threw = false;
        try { Index t(bad); t.bulk_load(three); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "Route::Interval is refused by the truncated directory backend");
    }
    {
        Config bad = trunc_cfg;
        bad.model_kind = ModelKind::Unigram;
        bool threw = false;
        try { Index t(bad); t.bulk_load(three); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "ModelKind::Unigram is refused by the truncated directory backend");
    }
}

// ---------------------------------------------------------------- dynamic
void check_dynamic(const std::string& kind, const std::string& trcsv) {
    std::vector<Pair> keys = make_data(kind, 32768, trcsv);
    std::sort(keys.begin(), keys.end(),
              [](const Pair& a, const Pair& b) { return a.first < b.first; });

    Config trunc_cfg = e25sw::baseline_config(32);
    trunc_cfg.model_backend = ModelBackend::EqualDepthDirTrunc;
    Config full_cfg = trunc_cfg;
    full_cfg.model_backend = ModelBackend::EqualDepthDir;

    Index it(trunc_cfg), iu(full_cfg);
    it.bulk_load(keys);
    iu.bulk_load(keys);
    const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

    std::vector<std::string> added;
    std::uint64_t bad = 0, differ = 0;
    for (std::size_t i = 0; i < 1500; ++i) {
        const std::string k = "insert-" + std::to_string(i) + "-" + keys[i % keys.size()].first;
        added.push_back(k);
        if (!it.insert(k, 900000 + i)) ++bad;
        if (!iu.insert(k, 900000 + i)) ++bad;
    }
    for (std::size_t i = 0; i < 700; ++i) {
        if (!it.erase(added[i])) ++bad;
        if (!iu.erase(added[i])) ++bad;
    }
    for (std::size_t i = 700; i < added.size(); ++i) {
        const auto a = it.find(added[i], qmode);
        const auto b = iu.find(added[i], qmode);
        if (!a.found || a.value != 900000 + i) ++bad;
        if (a.found != b.found || a.value != b.value) ++differ;
    }
    std::uint64_t survivors = 0, survivors_full = 0;
    for (std::size_t i = 0; i < 700; ++i) {
        if (it.find(added[i], qmode).found) ++survivors;
        if (iu.find(added[i], qmode).found) ++survivors_full;
    }
    for (const auto& kv : keys)
        if (!it.find(kv.first, qmode).found) ++bad;

    require(bad == 0, "dynamic insert/erase/re-find answers are exact on the truncated form");
    require(survivors == 0 && survivors_full == 0, "erased keys are gone from both forms");
    require(differ == 0, "both forms answer identically after the update stream");
}

// ---------------------------------------------------------------- corpus differential
struct Probes {
    std::uint64_t blocks = 0, cmps = 0, ovf = 0;
};

std::uint64_t percentile(const std::vector<std::uint64_t>& v, double q) {
    if (v.empty()) return 0;
    std::vector<std::uint64_t> s = v;
    std::sort(s.begin(), s.end());
    const std::size_t idx =
        std::min(s.size() - 1, static_cast<std::size_t>(q * double(s.size() - 1)));
    return s[idx];
}

struct CorpusSummary {
    std::uint64_t regions = 0;
    std::uint64_t regions_fitted = 0;
    std::uint64_t regions_truncated = 0;      // live regions whose stored form is shorter
    std::uint64_t thresholds = 0;
    std::uint64_t full_bytes = 0;
    std::uint64_t trunc_bytes = 0;
    std::uint64_t fallbacks = 0;
    std::uint64_t shortened = 0;
    std::uint64_t routing_equal = 0;
    std::uint64_t routing_differ = 0;
    std::uint64_t band_mismatch = 0;          // rank band vs count; duplicate runs only
    std::uint64_t stored_probes = 0;
    std::uint64_t bracket_violations = 0;
    std::uint64_t inv_prefix = 0;
    std::uint64_t inv_order = 0;
    std::uint64_t inv_construction = 0;
    std::uint64_t inv_tail = 0;
    std::uint64_t equal_before = 0;
    std::uint64_t equal_after = 0;
    std::uint64_t slices = 0;
    std::vector<std::uint64_t> region_trunc_bytes;
    std::vector<std::uint64_t> sep_len_trunc;
};

void check_corpus(const std::string& kind, const std::string& trcsv, std::uint32_t thr,
                  std::size_t kN) {
    std::vector<Pair> keys = make_data(kind, kN, trcsv);
    std::sort(keys.begin(), keys.end(),
              [](const Pair& a, const Pair& b) { return a.first < b.first; });
    const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

    Config ref_cfg = e25sw::baseline_config(thr);
    ref_cfg.model_backend = ModelBackend::Context16;
    Config full_cfg = ref_cfg;
    full_cfg.model_backend = ModelBackend::EqualDepthDir;
    Config trunc_cfg = ref_cfg;
    trunc_cfg.model_backend = ModelBackend::EqualDepthDirTrunc;
    e25sw::require_only_switches_differ(ref_cfg, trunc_cfg, {"model_backend"});
    e25sw::require_only_switches_differ(full_cfg, trunc_cfg, {"model_backend"});

    // The counters below are static and accumulate over every directory ever fitted, so the
    // truncated arm is built first and its counters are read before the other two arms add to
    // them: the live figure is a single-arm figure.
    Region::reset_depth_dir_stats();
    Index ref(ref_cfg), full(full_cfg), trunc(trunc_cfg);
    trunc.bulk_load(keys);
    const auto live_stats = Region::depth_dir_stats();
    full.bulk_load(keys);
    ref.bulk_load(keys);
    require(ref.region_count() == full.region_count() && full.region_count() == trunc.region_count(),
            kind + ": the three arms build the same region count");

    CorpusSummary sum;

    // The live regions must be the form each backend asked for, and the truncated one must
    // really be the smaller of the two on the corpus actually loaded.
    std::uint64_t form_bad = 0, live_full_bytes = 0, live_trunc_bytes = 0;
    for (std::size_t r = 0; r < trunc.region_count(); ++r) {
        const Region& rt = trunc.region(r);
        const Region& ru = full.region(r);
        if (!rt.uses_depth_dir() || !rt.depth_dir_truncated()) ++form_bad;
        if (!ru.uses_depth_dir() || ru.depth_dir_truncated()) ++form_bad;
        const std::uint64_t bt = rt.depth_dir_separator_bytes();
        const std::uint64_t bu = ru.depth_dir_separator_bytes();
        live_full_bytes += bu;
        live_trunc_bytes += bt;
        sum.full_bytes += bu;
        sum.trunc_bytes += bt;
        sum.region_trunc_bytes.push_back(bt);
        if (rt.depth_dir_status() == equivdir::Status::Ok) ++sum.regions_fitted;
        if (bt < bu) ++sum.regions_truncated;
    }
    require(form_bad == 0, kind + ": every live region carries the form its backend asked for");
    require(live_trunc_bytes < live_full_bytes,
            kind + ": the truncated directory is smaller on the corpus as loaded");
    require(sum.regions_truncated > 0, kind + ": at least one live region got shorter");
    sum.regions = trunc.region_count();
    require(live_stats.regions_truncated == trunc.region_count(),
            kind + ": the directory stats count every fitted region of the truncated arm");
    require(live_stats.threshold_bytes_total == live_trunc_bytes,
            kind + ": the stats' byte total is the live truncated figure");

    // Per-key answers: the reference arm must find every key, and the two directory forms must
    // answer identically (values and regions), because the truncated form only changes the bytes
    // the mapping is stored in.
    std::uint64_t ref_bad = 0, trunc_bad = 0, answer_differ = 0, region_differ = 0;
    std::uint64_t live_route_differ = 0;
    Probes pr, pf, pt;
    for (const auto& kv : keys) {
        const auto a = ref.find(kv.first, qmode);
        const auto b = full.find(kv.first, qmode);
        const auto c = trunc.find(kv.first, qmode);
        if (!a.found || a.value != kv.second) ++ref_bad;
        if (!b.found || b.value != kv.second) ++trunc_bad;
        if (!c.found || c.value != kv.second) ++trunc_bad;
        if (a.found != b.found || a.value != b.value) ++answer_differ;
        if (b.found != c.found || b.value != c.value) ++answer_differ;
        const std::size_t rf = full.locate(kv.first), rtr = trunc.locate(kv.first);
        if (rf != rtr) ++region_differ;
        // Lemma A on the LIVE index: the two forms must put the same key in the same group.
        else if (full.region(rf).placement_group(kv.first) !=
                 trunc.region(rtr).placement_group(kv.first)) ++live_route_differ;
        pr.blocks += a.subblocks_visited; pr.cmps += a.str_cmps_primary + a.str_cmps_overflow;
        pr.ovf += a.overflow_entered;
        pf.blocks += b.subblocks_visited; pf.cmps += b.str_cmps_primary + b.str_cmps_overflow;
        pf.ovf += b.overflow_entered;
        pt.blocks += c.subblocks_visited; pt.cmps += c.str_cmps_primary + c.str_cmps_overflow;
        pt.ovf += c.overflow_entered;
    }
    require(ref_bad == 0, kind + ": the reference backend finds every corpus key");
    require(trunc_bad == 0, kind + ": both directory forms find every corpus key");
    require(answer_differ == 0, kind + ": the two directory forms answer identically");
    require(region_differ == 0, kind + ": the two directory forms locate into the same regions");
    require(live_route_differ == 0,
            kind + ": the two live directory forms place every key in the same group (lemma A)");

    // Independent reference on region-sized slices: the same shapes the index builds, with the
    // section-2 separators recomputed from the keys alone, then compared against the model's
    // storage, against both live arms and against the order-free count.
    const std::size_t slice = trunc_cfg.region_keys ? trunc_cfg.region_keys : keys.size();
    for (std::size_t start = 0; start + slice <= keys.size(); start += slice) {
        std::vector<Pair> part(keys.begin() + start, keys.begin() + start + slice);
        Region rt(part, trunc_cfg);
        Region ru(part, full_cfg);
        require(rt.uses_depth_dir() && ru.uses_depth_dir(),
                kind + ": every slice region is on the directory backend");
        require(rt.depth_dir_truncated() && !ru.depth_dir_truncated(),
                kind + ": every slice region carries the form its backend asked for");
        require(rt.depth_dir_status() == equivdir::Status::Ok,
                kind + ": every slice region is fitted, so the section-2 rule is exercised");
        require(rt.groups() == ru.groups(),
                kind + ": both forms derive the same group count on the same keys");

        const std::size_t plen = lcp(part.front().first, part.back().first);
        const DirShape sh = build_shape(part, plen, rt.groups());

        std::vector<std::string_view> views;
        for (const auto& kv : part) views.push_back(kv.first);
        const auto mt = equivdir::RegionModel::fit(views.data(), views.size(), plen, rt.groups(), true);
        const auto mu = equivdir::RegionModel::fit(views.data(), views.size(), plen, rt.groups(), false);
        std::uint64_t sep_mismatch = 0;
        for (std::size_t idx = 0; idx < sh.rank.size(); ++idx) {
            if (mt.separator(idx) != sh.trunc[idx]) ++sep_mismatch;
            if (mu.separator(idx) != sh.full[idx]) ++sep_mismatch;
        }
        require(sep_mismatch == 0, kind + ": the model stores exactly the frozen separators");
        require(mt.separator_bytes() == bytes_of(sh.trunc) &&
                mu.separator_bytes() == bytes_of(sh.full),
                kind + ": the model reports its own arena bytes");
        require(rt.depth_dir_separator_bytes() == bytes_of(sh.trunc),
                kind + ": the live region's stored bytes are the frozen figure");
        require(mt.truncated() && !mu.truncated(), kind + ": the two fits differ only in the flag");

        const auto inv = check_separator_list(sh, sh.trunc);
        require(inv.prefix_violations == 0 && inv.order_violations == 0 &&
                inv.construction_violations == 0 && inv.tail_violations == 0,
                kind + ": slice separators satisfy the section-3 invariants");

        const auto ct = counts_for_keys(sh.trunc, sh);
        const auto cf = counts_for_keys(sh.full, sh);
        const auto band = band_of_key(sh);
        std::uint64_t model_bad = 0, live_bad = 0, differ = 0, mismatch = 0, mismatch_bad = 0;
        for (std::size_t i = 0; i < part.size(); ++i) {
            const auto& k = part[i].first;
            if (mt.predict_group(k) != ct[i]) ++model_bad;
            if (mu.predict_group(k) != cf[i]) ++model_bad;
            if (rt.placement_group(k) != ct[i]) ++live_bad;
            if (ru.placement_group(k) != cf[i]) ++live_bad;
            if (ct[i] != cf[i]) ++differ;
            if (band[i] != ct[i]) {
                ++mismatch;
                // The band a key is placed in and the count of separators below it can differ
                // only inside a run of equal suffixes: with distinct suffixes they are the same
                // number, which is the depth property the round claims.
                const bool duplicate_run =
                    (i > 0 && ub_compare(sh.suffix[i - 1], sh.suffix[i]) == 0) ||
                    (i + 1 < part.size() && ub_compare(sh.suffix[i], sh.suffix[i + 1]) == 0);
                if (!duplicate_run) ++mismatch_bad;
            }
        }
        require(model_bad == 0, kind + ": both models answer the independent count");
        require(live_bad == 0, kind + ": both live arms place keys by the independent count");
        require(differ == 0, kind + ": lemma A holds on every key of the slice");
        require(mismatch_bad == 0,
                kind + ": every band/count difference sits inside a run of equal suffixes");

        std::vector<std::string> probes = sh.suffix;
        const auto extra = make_probes(sh);
        probes.insert(probes.end(), extra.begin(), extra.end());
        const auto br = bracket_check(sh, sh.trunc, probes, part.size() <= 256, &rt);
        require(br.before_violations == 0 && br.after_violations == 0,
                kind + ": the range brackets hold on every probe of the slice");
        require(br.stored_group_mismatch == 0,
                kind + ": a probe that is a stored key is answered by the group holding it");
        if (!sh.duplicate_suffixes)
            require(br.equal_before == 0 && br.equal_after == 0,
                    kind + ": distinct suffixes need no duplicate-run exception");

        sum.thresholds += sh.rank.size();
        sum.fallbacks += fallback_count(sh);
        sum.shortened += shortened_count(sh);
        sum.routing_equal += part.size() - differ;
        sum.routing_differ += differ;
        sum.band_mismatch += mismatch;
        sum.stored_probes += br.stored_probes;
        sum.bracket_violations += br.before_violations + br.after_violations + br.stored_group_mismatch;
        sum.inv_prefix += inv.prefix_violations;
        sum.inv_order += inv.order_violations;
        sum.inv_construction += inv.construction_violations;
        sum.inv_tail += inv.tail_violations;
        sum.equal_before += br.equal_before;
        sum.equal_after += br.equal_after;
        for (const auto& t : sh.trunc) sum.sep_len_trunc.push_back(t.size());
        ++sum.slices;
        if (sum.slices == 1) check_negative_controls(sh, kind);
    }
    require(sum.slices > 0, kind + ": at least one region-sized slice was checked");
    require(sum.thresholds == live_stats.thresholds_total,
            kind + ": the independent threshold count matches the live counter");

    const double shrink = sum.full_bytes
        ? 100.0 * (1.0 - double(sum.trunc_bytes) / double(sum.full_bytes)) : 0.0;

    std::cout << "{\"kind\":\"" << kind << "\",\"n\":" << keys.size()
              << ",\"regions\":" << sum.regions
              << ",\"regions_fitted\":" << sum.regions_fitted
              << ",\"regions_shorter\":" << sum.regions_truncated
              << ",\"thresholds\":" << sum.thresholds
              << ",\"full_bytes\":" << sum.full_bytes
              << ",\"trunc_bytes\":" << sum.trunc_bytes
              << ",\"fallbacks\":" << sum.fallbacks
              << ",\"shortened\":" << sum.shortened
              << ",\"routing_equal\":" << sum.routing_equal
              << ",\"routing_differ\":" << sum.routing_differ
              << ",\"band_mismatch\":" << sum.band_mismatch
              << ",\"stored_probes\":" << sum.stored_probes
              << ",\"bracket_violations\":" << sum.bracket_violations
              << ",\"inv_prefix\":" << sum.inv_prefix
              << ",\"inv_order\":" << sum.inv_order
              << ",\"inv_construction\":" << sum.inv_construction
              << ",\"inv_tail\":" << sum.inv_tail
              << ",\"bracket_equal_before\":" << sum.equal_before
              << ",\"bracket_equal_after\":" << sum.equal_after
              << ",\"ref_probes\":" << pr.blocks << ",\"dir_probes\":" << pf.blocks
              << ",\"trunc_probes\":" << pt.blocks
              << ",\"ref_cmps\":" << pr.cmps << ",\"dir_cmps\":" << pf.cmps
              << ",\"trunc_cmps\":" << pt.cmps
              << ",\"ref_overflow\":" << pr.ovf << ",\"dir_overflow\":" << pf.ovf
              << ",\"trunc_overflow\":" << pt.ovf << "}\n";

    std::cout << "[truncdir] " << kind << " n=" << keys.size()
              << " regions=" << sum.regions << " fitted=" << sum.regions_fitted
              << " slices=" << sum.slices << "\n"
              << "  thresholds=" << sum.thresholds
              << " separator bytes: full=" << sum.full_bytes << " truncated=" << sum.trunc_bytes
              << " (" << (sum.full_bytes - sum.trunc_bytes) << " bytes, "
              << shrink << "% smaller)\n"
              << "  fallbacks=" << sum.fallbacks << " shortened=" << sum.shortened
              << " regions_truncated=" << sum.regions_truncated
              << " per-region truncated bytes P50=" << percentile(sum.region_trunc_bytes, 0.50)
              << " P95=" << percentile(sum.region_trunc_bytes, 0.95)
              << " max=" << percentile(sum.region_trunc_bytes, 1.0) << "\n"
              << "  per-separator truncated bytes P50=" << percentile(sum.sep_len_trunc, 0.50)
              << " P95=" << percentile(sum.sep_len_trunc, 0.95)
              << " max=" << percentile(sum.sep_len_trunc, 1.0) << "\n"
              << "  routing equal=" << sum.routing_equal << " differ=" << sum.routing_differ
              << " band_mismatch(duplicate runs)=" << sum.band_mismatch
              << " stored_probes=" << sum.stored_probes
              << " bracket_violations=" << sum.bracket_violations
              << "\n  invariant violations prefix/order/construction/tail=" << sum.inv_prefix
              << "/" << sum.inv_order << "/" << sum.inv_construction << "/" << sum.inv_tail
              << " duplicate-run exceptions before/after g=" << sum.equal_before
              << "/" << sum.equal_after << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_depth_dir_trunc KIND N [TRANCOCSV]");
        const std::string kind = argv[1];
        const std::string trcsv = argc > 3 ? argv[3] : "";

        std::uint32_t thr = 32;
        {
            const std::string frozen_rel =
                "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing: " + frozen_rel);
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        check_synthetic_depth();
        check_duplicate_keys();
        check_domain_and_refusals();
        check_dynamic(kind, trcsv);
        check_corpus(kind, trcsv, thr, std::stoull(argv[2]));

        std::cout << "[truncdir] negative controls: runs=" << g_neg.runs
                  << " cut_construction_violations=" << g_neg.cut_violations
                  << " swap_order_violations=" << g_neg.swap_order
                  << " swap_search_disagreements=" << g_neg.swap_disagreements << "\n";
        std::cout << "checks=" << checks << " failures=" << failures << "\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << "\n";
        return 2;
    }
}

// Gate + entry diagnostic for the EqualDepthDir backend (2026-10-11).
//
// THE CLAIMS. With the equal-depth separator directory wired into Region,
//   (1) the depth property holds EXACTLY: with n keys and G groups the groups hold
//       floor/ceil(n/G) keys each -- asserted against the closed-form sizes on a synthetic
//       region, and against an independent recount on real corpus slices;
//   (2) the mapping is ORDER-PRESERVING inside every region (placement group non-decreasing in
//       unsigned-byte key order) and stays inside [0, G), checked on the live index and
//       validated by a NEGATIVE CONTROL (a rotated group vector must be caught);
//   (3) every stored key is FOUND with its exact value -- placement and query go through
//       different functions, and a key whose query group differs from its placement group is a
//       MISS;
//   (4) the domain is the whole byte-string domain: empty key, embedded NUL, bytes >= 0x80,
//       4096-byte keys, duplicate keys, and the boundary statuses (n = 0 / 1, G = 1);
//   (5) the pairings the backend does not implement are REFUSED;
//   (6) the dynamic paths hold: insert / erase, then re-find every key and re-check monotonic.
//
// It also records the ENTRY numbers: group load balance (max / mean / empty) and the query
// counters (primary probes, full comparisons, overflow entries) for the reference backend and
// the directory on the SAME regions, plus the per-region probe ratio.
//
// NO TIMING IS TAKEN ANYWHERE. Counts here are not speedups.
//
// Usage: check_depth_dir KIND N [TRANCOCSV]     KIND: mixed|skew|tranco

#include "data.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
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

// Independent recount: thresholds at rank floor(j*n/G), group = number of separators <= key.
// Written from the contract, not from the implementation.
std::vector<std::uint32_t> reference_groups(const std::vector<Pair>& sorted, std::uint32_t G,
                                            std::size_t prefix_len) {
    std::vector<std::uint32_t> out(sorted.size(), 0);
    if (sorted.empty() || G <= 1) return out;

    std::vector<std::string_view> seps;
    for (std::uint32_t j = 1; j < G; ++j) {
        const std::size_t rank = std::min(sorted.size() - 1,
                                          (std::size_t(j) * sorted.size()) / G);
        seps.push_back(std::string_view(sorted[rank].first).substr(prefix_len));
    }
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const std::string_view q = std::string_view(sorted[i].first).substr(prefix_len);
        std::uint32_t g = 0;
        for (const auto& sep : seps)
            if (compare(sep, q) <= 0) ++g;
        out[i] = std::min<std::uint32_t>(G - 1, g);
    }
    return out;
}

std::size_t common_prefix_len(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

// ---------------------------------------------------------------- synthetic depth property
void check_synthetic_depth() {
    Config c = e25sw::baseline_config(32);
    c.model_backend = ModelBackend::EqualDepthDir;
    c.fixed_groups = 8;

    std::vector<Pair> keys;
    for (int i = 0; i < 100; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "synthetic%04d", i);
        keys.emplace_back(buf, i + 1);
    }
    std::sort(keys.begin(), keys.end());

    Region r(keys, c);
    require(r.uses_depth_dir(), "synthetic region is on the directory backend");
    require(r.groups() == 8, "synthetic region has the requested 8 groups");

    const auto expect = reference_groups(keys, 8, 0);
    std::vector<std::size_t> load(8, 0);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto got = r.placement_group(keys[i].first);
        require(got == expect[i], "synthetic placement group equals the independent recount");
        ++load[std::min<std::size_t>(got, 7)];
    }
    // Closed form: thresholds at ranks 12,25,37,50,62,75,87 for n=100, G=8.
    const std::size_t want[8] = {12, 13, 12, 13, 12, 13, 12, 13};
    for (std::size_t g = 0; g < 8; ++g)
        require(load[g] == want[g],
                "synthetic depth: group " + std::to_string(g) + " holds " +
                    std::to_string(load[g]) + ", expected " + std::to_string(want[g]));

    // Range probes: both stay INSIDE the region's prefix domain (a key shorter than the prefix
    // never reaches a model -- prefix_matches refuses it first), so these test the count rule,
    // not the domain.
    require(r.placement_group("synthetic!!!!") == 0, "key below the range lands in group 0");
    require(r.placement_group("syntheticZZZZ") == 7, "key above the range lands in the last group");
}

// ---------------------------------------------------------------- equal keys
void check_duplicate_keys() {
    Config c = e25sw::baseline_config(32);
    c.model_backend = ModelBackend::EqualDepthDir;
    c.fixed_groups = 8;

    // 100 keys over 5 distinct values: the thresholds repeat, so some groups MUST be empty --
    // and no map of the suffix alone could avoid that. Total and monotonicity must still hold.
    std::vector<Pair> keys;
    for (int i = 0; i < 100; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "dup%d", i % 5);
        keys.emplace_back(buf, i + 1);
    }
    std::sort(keys.begin(), keys.end());

    Region r(keys, c);
    std::vector<std::uint32_t> groups;
    std::vector<std::size_t> load(8, 0);
    for (const auto& kv : keys) {
        const auto g = r.placement_group(kv.first);
        require(g < 8, "duplicate-key group is inside the range");
        groups.push_back(g);
        ++load[g];
    }
    require(monotone(groups), "duplicate-key map is non-decreasing");
    std::size_t total = 0, empty = 0;
    for (auto l : load) { total += l; if (!l) ++empty; }
    require(total == keys.size(), "duplicate-key map accounts for every key");
    require(empty > 0, "duplicate keys leave at least one empty group (reported, not hidden)");
}

// ---------------------------------------------------------------- corpus differential
struct Probes {
    std::uint64_t blocks = 0, cmps = 0, ovf = 0, steps = 0;
};

void check_corpus(const std::string& kind, const std::string& trcsv, std::uint32_t thr,
                  std::size_t kN) {
    std::vector<Pair> keys = make_data(kind, kN, trcsv);
    std::sort(keys.begin(), keys.end(),
              [](const Pair& a, const Pair& b) { return a.first < b.first; });

    const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

    Config base = e25sw::baseline_config(thr);
    base.model_backend = ModelBackend::Context16;
    Config cand = base;
    cand.model_backend = ModelBackend::EqualDepthDir;
    e25sw::require_only_switches_differ(base, cand, {"model_backend"});

    Region::reset_depth_dir_stats();
    Index ref(base), dir(cand);
    ref.bulk_load(keys);
    dir.bulk_load(keys);
    require(ref.region_count() == dir.region_count(), "both backends build the same region count");

    std::uint64_t ref_bad = 0, dir_bad = 0, checksum_ref = 0, checksum_dir = 0;
    Probes pr, pd;
    std::vector<std::uint64_t> ref_region(ref.region_count(), 0), dir_region(dir.region_count(), 0);
    for (const auto& kv : keys) {
        const auto ra = ref.locate(kv.first);
        const auto rb = dir.locate(kv.first);
        const auto a = ref.find(kv.first, qmode);
        const auto b = dir.find(kv.first, qmode);
        ref_region[ra] += a.subblocks_visited;
        dir_region[rb] += b.subblocks_visited;
        if (!a.found || a.value != kv.second) ++ref_bad;
        if (!b.found || b.value != kv.second) ++dir_bad;
        checksum_ref = checksum_ref * 1000003ULL + (a.found ? a.value + 1 : 0) + a.steps;
        checksum_dir = checksum_dir * 1000003ULL + (b.found ? b.value + 1 : 0) + b.steps;
        pr.blocks += a.subblocks_visited; pr.cmps += a.str_cmps_primary + a.str_cmps_overflow;
        pr.ovf += a.overflow_entered; pr.steps += a.steps;
        pd.blocks += b.subblocks_visited; pd.cmps += b.str_cmps_primary + b.str_cmps_overflow;
        pd.ovf += b.overflow_entered; pd.steps += b.steps;
    }
    require(ref_bad == 0, "reference backend finds every corpus key");
    require(dir_bad == 0, "directory backend finds every corpus key");

    // Independent recount on region-sized corpus slices: the fit is checked against the
    // gate's own formula, on real keys with their real prefixes.
    const std::size_t slice = base.region_keys ? base.region_keys : keys.size();
    for (std::size_t start = 0; start + slice <= keys.size(); start += slice) {
        std::vector<Pair> part(keys.begin() + start, keys.begin() + start + slice);
        Region r(part, cand);
        const std::size_t plen = common_prefix_len(part.front().first, part.back().first);
        const auto expect = reference_groups(part, r.groups(), plen);
        for (std::size_t i = 0; i < part.size(); ++i)
            require(r.placement_group(part[i].first) == expect[i],
                    "corpus slice placement group equals the independent recount");
    }

    // Monotonicity per region, plus the negative control applied to real data.
    std::vector<std::vector<std::uint32_t>> groups(dir.region_count());
    for (const auto& kv : keys) groups[dir.locate(kv.first)].push_back(
        dir.region(dir.locate(kv.first)).placement_group(kv.first));
    for (std::size_t i = 0; i < groups.size(); ++i) {
        require(monotone(groups[i]), "corpus monotone in node " + std::to_string(i));
        if (groups[i].size() >= 4) {
            auto rotated = groups[i];
            rotated[rotated.size() / 2] = groups[i].back();
            if (groups[i].back() != groups[i][rotated.size() / 2])
                require(!monotone(rotated), "corpus negative control caught in node " +
                                                std::to_string(i));
        }
    }

    // Balance: the property this round exists for.
    struct Bal { std::uint64_t groups = 0, empty = 0, total = 0; std::size_t max = 0; };
    auto balance = [](const Index& idx) {
        Bal b;
        for (std::size_t r = 0; r < idx.region_count(); ++r)
            for (const auto& gs : idx.region(r).group_stats()) {
                const std::size_t load = gs.primary_entries + gs.overflow_entries;
                ++b.groups; b.total += load; b.max = std::max(b.max, load);
                if (!load) ++b.empty;
            }
        return b;
    };
    const Bal br = balance(ref), bd = balance(dir);

    std::vector<double> ratios;
    for (std::size_t i = 0; i < dir_region.size() && i < ref_region.size(); ++i) {
        if (!ref_region[i]) continue;
        ratios.push_back(double(dir_region[i]) / double(ref_region[i]));
    }
    std::sort(ratios.begin(), ratios.end());
    const auto at = [&ratios](double q) {
        return ratios.empty() ? 0.0 : ratios[std::min(ratios.size() - 1,
            static_cast<std::size_t>(q * double(ratios.size() - 1)))];
    };
    std::size_t worse = 0;
    for (double r : ratios) if (r > 1.0) ++worse;

    const auto st = Region::depth_dir_stats();

    std::cout << "{\"kind\":\"" << kind << "\",\"n\":" << keys.size()
              << ",\"regions\":" << dir.region_count()
              << ",\"dir_regions_fitted\":" << st.regions_fitted
              << ",\"dir_regions_boundary\":" << st.regions_boundary
              << ",\"dir_thresholds\":" << st.thresholds_total
              << ",\"dir_regions_with_duplicate_thresholds\":"
              << st.regions_with_duplicate_thresholds
              << ",\"checksum_ref\":" << checksum_ref << ",\"checksum_dir\":" << checksum_dir
              << ",\"ref_probes\":" << pr.blocks << ",\"dir_probes\":" << pd.blocks
              << ",\"ref_cmps\":" << pr.cmps << ",\"dir_cmps\":" << pd.cmps
              << ",\"ref_steps\":" << pr.steps << ",\"dir_steps\":" << pd.steps
              << ",\"ref_overflow\":" << pr.ovf << ",\"dir_overflow\":" << pd.ovf
              << ",\"ctx_group_max\":" << br.max
              << ",\"ctx_group_avg\":" << (br.groups ? double(br.total) / double(br.groups) : 0.0)
              << ",\"ctx_group_empty\":" << br.empty
              << ",\"dir_group_max\":" << bd.max
              << ",\"dir_group_avg\":" << (bd.groups ? double(bd.total) / double(bd.groups) : 0.0)
              << ",\"dir_group_empty\":" << bd.empty
              << ",\"probe_ratio_median\":" << at(0.5) << ",\"probe_ratio_p90\":" << at(0.9)
              << ",\"probe_ratio_worse_regions\":" << worse
              << ",\"probe_ratio_regions\":" << ratios.size() << "}\n";
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

    Config c = e25sw::baseline_config(32);
    c.model_backend = ModelBackend::EqualDepthDir;
    c.multiblock = true;
    c.reuse_group_ids = true;
    c.region_keys = 8;   // several small regions, including single-key ones

    Index idx(c);
    idx.bulk_load(pairs);

    const Mode full{Route::Full, Prefetch::Off, true, false, OvSearch::Hybrid, 32, false};
    const Mode interval{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
    for (const auto& kv : pairs) {
        const auto a = idx.find(kv.first, full);
        const auto b = idx.find(kv.first, interval);
        require(a.found && a.value == kv.second, "domain find (full route)");
        require(b.found && b.value == kv.second, "domain find (interval route)");
    }
    for (const auto& kv : pairs) {
        const std::string absent = kv.first + "~absent";
        require(!idx.find(absent, interval).found, "domain absent probe is a miss");
    }

    std::vector<Pair> three{{"aa", 1}, {"ab", 2}, {"ac", 3}};
    {
        Config bad = c;
        bad.canonical = Route::Interval;
        bool threw = false;
        try { Index t(bad); t.bulk_load(three); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "Route::Interval is refused by the directory backend");
    }
    {
        Config bad = c;
        bad.model_kind = ModelKind::Unigram;
        bool threw = false;
        try { Index t(bad); t.bulk_load(three); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "ModelKind::Unigram is refused by the directory backend");
    }
}

// ---------------------------------------------------------------- dynamic
void check_dynamic(const std::string& kind, const std::string& trcsv) {
    std::vector<Pair> keys = make_data(kind, 32768, trcsv);
    std::sort(keys.begin(), keys.end(),
              [](const Pair& a, const Pair& b) { return a.first < b.first; });

    Config c = e25sw::baseline_config(32);
    c.model_backend = ModelBackend::EqualDepthDir;
    Index idx(c);
    idx.bulk_load(keys);
    const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

    std::vector<std::string> added;
    std::uint64_t bad = 0;
    for (std::size_t i = 0; i < 1500; ++i) {
        const std::string k = "insert-" + std::to_string(i) + "-" + keys[i % keys.size()].first;
        added.push_back(k);
        if (!idx.insert(k, 900000 + i)) ++bad;
    }
    for (std::size_t i = 0; i < 700; ++i)
        if (!idx.erase(added[i])) ++bad;
    for (std::size_t i = 700; i < added.size(); ++i) {
        const auto r = idx.find(added[i], qmode);
        if (!r.found || r.value != 900000 + i) ++bad;
    }
    std::uint64_t survivors = 0;
    for (std::size_t i = 0; i < 700; ++i)
        if (idx.find(added[i], qmode).found) ++survivors;
    for (const auto& kv : keys)
        if (!idx.find(kv.first, qmode).found) ++bad;

    require(bad == 0, "dynamic insert/erase/re-find answers are exact");
    require(survivors == 0, "erased keys are gone");
}


// ---------------------------------------------------------------- byte order (audit control)
//
// AUDIT_DEPTH_DIR_ORDER_20261011.md: the search used to compare `sep[i] < q[i]` on plain char,
// which is signed on x86, so every byte >= 0x80 sorted below every byte < 0x80. Placement and
// lookup shared that map, so a find-only check could not see it. This is the audit's four-key
// fixture: ranks 1,2,3 of 0x01 / 0x7f / 0x80 / 0xff are the one-byte thresholds 0x7f / 0x80 /
// 0xff, and the expected group of a query byte is the independent INTEGER count of those
// thresholds below it.
void check_high_byte_order() {
    std::vector<std::string> owned;
    for (unsigned b : {1u, 127u, 128u, 255u}) owned.emplace_back(1, static_cast<char>(b));
    std::vector<std::string_view> keys(owned.begin(), owned.end());
    const auto model = equivdir::RegionModel::fit(keys.data(), keys.size(), 0, 4, false);

    std::uint64_t mismatches = 0, inversions = 0;
    std::uint32_t previous = 0;
    for (unsigned b = 0; b < 256; ++b) {
        const std::string q(1, static_cast<char>(b));
        const std::uint32_t expected =
            static_cast<std::uint32_t>((127u <= b) + (128u <= b) + (255u <= b));
        const std::uint32_t actual = model.predict_group(q);
        if (actual != expected) ++mismatches;
        if (b && actual < previous) ++inversions;
        previous = actual;
    }
    require(mismatches == 0,
            "byte order: every single-byte query is placed by the unsigned count (" +
                std::to_string(mismatches) + " mismatches)");
    require(inversions == 0, "byte order: no group inversion over 0x00..0xff");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_depth_dir KIND N [TRANCOCSV]");
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

        check_high_byte_order();
        check_duplicate_keys();
        check_domain_and_refusals();
        check_dynamic(kind, trcsv);
        check_corpus(kind, trcsv, thr, std::stoull(argv[2]));

        std::cout << "checks=" << checks << " failures=" << failures << "\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << "\n";
        return 2;
    }
}

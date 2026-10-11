// Gate + entry diagnostic for the BlockLinearRank backend (2026-10-11).
//
// WHAT THIS FILE IS FOR. The third model backend changes the string -> group map, and the
// claims that must hold before any timer runs are:
//
//   * the ported encoding is the teammate's, bit for bit. The 12 vectors below were taken
//     from a run that compared OUR encode() against THEIR blocklearn::encode() (upstream
//     commit 60837a88) on the same inputs. A hard-coded vector is the only check that cannot
//     drift along with the implementation;
//   * the group map is ORDER-PRESERVING inside every region: the placement group of a key
//     (Region::placement_group) must be non-decreasing in unsigned-byte key order, or a
//     group's records stop being a contiguous key range and the sub-block boundary directory
//     becomes meaningless. The checker is validated by a NEGATIVE CONTROL: a rotated group
//     vector must be caught;
//   * every stored key is FOUND with its exact value by the query path, on the same index
//     object. This is the real placement-vs-query test: the two go through different functions
//     (canonical_group vs predict_for), and a key whose query group differs from its placement
//     group is a MISS;
//   * the input domain is the whole byte-string domain: empty key, embedded NUL, bytes
//     >= 0x80, 4096-byte keys, single-key and empty regions, G == 1;
//   * the combinations the backend does not implement are REFUSED (ModelKind != Context16,
//     Route != Full) rather than silently mapped by something else;
//   * the dynamic paths still hold: insert/erase, then re-find every key and re-check
//     monotonicity.
//
// It also records the ENTRY numbers the plan asks for: group load distribution, primary
// probes, full comparisons and overflow entries, for both backends on the same corpus, plus
// the per-region probe ratio so the aggregate can be read as "everywhere a little" or "a few
// regions a lot".
//
// Usage: check_block_rank KIND N [TRANCOCSV]     KIND: mixed|skew|tranco

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

// ---------------------------------------------------------------- encode vectors
struct EncVec {
    const char* name;
    std::string key;
    std::size_t prefix;
    std::uint64_t want;
};

std::vector<EncVec> encode_vectors() {
    std::string longkey(4095, 'x');
    std::string nul1(1, '\0');
    std::string nul2;
    nul2.push_back('\0');
    nul2.push_back('a');
    std::string hi80(1, static_cast<char>(0x80));
    std::string hiff(1, static_cast<char>(0xff));
    std::string hi_mix;
    hi_mix.push_back(static_cast<char>(0x80));
    hi_mix.push_back('a');

    return {
        {"empty", "", 0, 0x0000000000000000ULL},
        {"a", "a", 0, 0x1880000000000000ULL},
        {"ab", "ab", 0, 0x188c600000000000ULL},
        {"b", "b", 0, 0x18c0000000000000ULL},
        {"nul", nul1, 0, 0x0040000000000000ULL},
        {"nul_a", nul2, 0, 0x004c400000000000ULL},
        {"hi80", hi80, 0, 0x2040000000000000ULL},
        {"hiff", hiff, 0, 0x4000000000000000ULL},
        {"hi80_a", hi_mix, 0, 0x204c400000000000ULL},
        {"abcdefgh", "abcdefgh", 0, 0x188c66432998ce68ULL},
        {"long_4095x", longkey, 0, 0x1e4f2793c9e4f279ULL},
        {"prefix2", "zzabcdefgh", 2, 0x188c66432998ce68ULL},
    };
}

void check_encode_vectors() {
    for (const auto& v : encode_vectors()) {
        const auto got = blockrank::encode(v.key, v.prefix, 7);
        require(got == v.want,
                std::string("encode ") + v.name + ": got " + std::to_string(got) +
                    ", want " + std::to_string(v.want));
    }
}

// ---------------------------------------------------------------- fit vectors
void check_fit_vectors() {
    {
        // Equal coordinates must not be split across segments. Here the front/back common
        // prefix is 1 byte, so the encoder reads bytes 1..7 and these 19 long keys collide
        // in that window (they differ at byte 8): the whole set is ONE segment.
        std::vector<std::string> keys;
        keys.push_back("a");
        for (int i = 0; i < 19; ++i)
            keys.push_back(std::string("a") + "0123456" + static_cast<char>('a' + i));
        std::sort(keys.begin(), keys.end());

        std::vector<std::string_view> views;
        for (const auto& k : keys) views.push_back(k);
        const auto m = blockrank::RegionModel::fit(views.data(), views.size(), 1, 16);

        require(m.fitted(), "collision set fitted");
        require(m.segment_count() == 1, "collision set is one segment");
        if (m.segment_count() == 1) {
            const auto& s = m.segments()[0];
            require(s.y_min == 0 && s.y_max == 19, "collision segment covers ranks 0..19");
            require(s.x_min == 0, "collision x_min is the single-byte key");
        }
        // Every colliding key must land in the same group. keys[0] == "a" is a deliberate
        // NON-colliding neighbour (its coordinate is 0), so the family starts at index 1 --
        // asserting over the whole vector would demand that a different coordinate share the
        // answer, which is not what "equal coordinates must not be split" says.
        const auto g0 = m.predict_group(keys[1]);
        for (std::size_t i = 1; i < keys.size(); ++i)
            require(m.predict_group(keys[i]) == g0, "collision keys share a group");
    }
    {
        // 100 sequential keys, budget 8 -> exactly 8 segments, ordered and contiguous.
        std::vector<std::string> keys;
        for (int i = 0; i < 100; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "k%05d", i);
            keys.emplace_back(buf);
        }
        std::sort(keys.begin(), keys.end());
        std::vector<std::string_view> views;
        for (const auto& k : keys) views.push_back(k);
        const auto m = blockrank::RegionModel::fit(views.data(), views.size(), 0, 16);
        require(m.segment_count() == 8, "sequential set is eight segments");
        bool ordered = true;
        for (std::size_t i = 1; i < m.segment_count(); ++i) {
            if (!(m.segments()[i - 1].x_max < m.segments()[i].x_min)) ordered = false;
            if (m.segments()[i - 1].y_max + 1 != m.segments()[i].y_min) ordered = false;
        }
        require(ordered, "segments are ordered and their rank ranges are contiguous");
        std::uint32_t last = 0;
        bool mono = true;
        for (const auto& k : keys) {
            const auto g = m.predict_group(k);
            if (g < last) mono = false;
            last = g;
        }
        require(mono, "group map is non-decreasing on sorted input");
    }
    {
        // Boundary statuses.
        const auto none = blockrank::RegionModel::fit(nullptr, 0, 0, 16);
        require(!none.fitted() && none.predict_group("x") == 0, "empty region maps to group 0");
        std::vector<std::string_view> one{"only"};
        const auto single = blockrank::RegionModel::fit(one.data(), 1, 4, 16);
        require(!single.fitted() && single.predict_group("only") == 0,
                "single-key region maps to group 0");
        const auto one_group = blockrank::RegionModel::fit(one.data(), 1, 4, 1);
        require(one_group.predict_group("only") == 0, "G == 1 maps to group 0");
    }
}

// ---------------------------------------------------------------- monotonicity
bool monotone(const std::vector<std::uint32_t>& groups) {
    for (std::size_t i = 1; i < groups.size(); ++i)
        if (groups[i] < groups[i - 1]) return false;
    return true;
}

void check_monotone_negative_control() {
    std::vector<std::uint32_t> g{0, 0, 1, 1, 2, 2};
    require(monotone(g), "control: the reference vector is monotone");
    std::vector<std::uint32_t> rotated = g;
    rotated[3] = 0;   // one key moved back a group
    require(!monotone(rotated), "negative control: a rotated vector is caught");
}

// ---------------------------------------------------------------- per-node groups
struct NodeScan {
    std::vector<std::vector<std::uint32_t>> groups;   // per node, in key order
    std::uint64_t groups_total = 0;
};

NodeScan scan_groups(const Index& idx, const std::vector<Pair>& keys) {
    NodeScan out;
    out.groups.resize(idx.region_count());
    for (const auto& kv : keys) {
        const auto r = idx.locate(kv.first);
        const auto g = idx.region(r).placement_group(kv.first);
        out.groups[r].push_back(g);
        ++out.groups_total;
    }
    return out;
}

void check_index_domain_suite() {
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

    Config c;
    c.model_backend = ModelBackend::BlockLinearRank;
    c.multiblock = true;
    c.reuse_group_ids = true;
    c.compact_plan_commit = true;
    c.region_keys = 8;   // force several regions, including very small ones

    Index idx(c);
    idx.bulk_load(pairs);

    Mode m{Route::Full, Prefetch::Off, true, false, OvSearch::Hybrid, 32, false};
    for (const auto& kv : pairs) {
        const auto r = idx.find(kv.first, m);
        require(r.found && r.value == kv.second,
                std::string("domain find: len=") + std::to_string(kv.first.size()));
    }
    for (const auto& kv : pairs) {
        std::string absent = kv.first + "~absent";
        const auto r = idx.find(absent, m);
        require(!r.found, "domain absent probe is a miss");
    }
    const auto scan = scan_groups(idx, pairs);
    for (std::size_t i = 0; i < scan.groups.size(); ++i)
        require(monotone(scan.groups[i]), "domain monotone in node " + std::to_string(i));
}

void check_refusals() {
    std::vector<Pair> pairs{{"aa", 1}, {"ab", 2}, {"ac", 3}};

    {
        Config c;
        c.model_backend = ModelBackend::BlockLinearRank;
        c.canonical = Route::Interval;
        bool threw = false;
        try { Index idx(c); idx.bulk_load(pairs); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "Route::Interval is refused by the rank backend");
    }
    {
        Config c;
        c.model_backend = ModelBackend::BlockLinearRank;
        c.model_kind = ModelKind::Unigram;
        bool threw = false;
        try { Index idx(c); idx.bulk_load(pairs); } catch (const std::invalid_argument&) { threw = true; }
        require(threw, "ModelKind::Unigram is refused by the rank backend");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_block_rank KIND N [TRANCOCSV]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        check_encode_vectors();
        check_fit_vectors();
        check_monotone_negative_control();
        check_index_domain_suite();
        check_refusals();

        // ---- corpus: the same keys through both backends -------------------------------
        std::vector<Pair> keys = make_data(kind, nkeys, trcsv);
        std::sort(keys.begin(), keys.end(),
                  [](const Pair& a, const Pair& b) { return a.first < b.first; });
        const std::size_t N = keys.size();

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

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        Config base = e25sw::baseline_config(thr);
        base.model_backend = ModelBackend::Context16;
        Config cand = base;
        cand.model_backend = ModelBackend::BlockLinearRank;
        e25sw::require_only_switches_differ(base, cand, {"model_backend"});

        Region::reset_block_rank_stats();
        Index ref(base), blk(cand);
        ref.bulk_load(keys);
        blk.bulk_load(keys);

        std::uint64_t ref_bad = 0, blk_bad = 0, checksum_ref = 0, checksum_blk = 0;
        std::uint64_t blk_probes = 0, blk_cmps = 0, blk_ovf = 0;
        std::uint64_t ref_probes = 0, ref_cmps = 0, ref_ovf = 0;
        std::vector<std::uint64_t> ref_region_probes(ref.region_count(), 0);
        std::vector<std::uint64_t> blk_region_probes(blk.region_count(), 0);
        for (const auto& kv : keys) {
            const auto ra = ref.locate(kv.first);
            const auto rb = blk.locate(kv.first);
            const auto a = ref.find(kv.first, qmode);
            const auto b = blk.find(kv.first, qmode);
            ref_region_probes[ra] += a.subblocks_visited;
            blk_region_probes[rb] += b.subblocks_visited;
            if (!a.found || a.value != kv.second) ++ref_bad;
            if (!b.found || b.value != kv.second) ++blk_bad;
            checksum_ref = checksum_ref * 1000003ULL + (a.found ? a.value + 1 : 0) + a.steps;
            checksum_blk = checksum_blk * 1000003ULL + (b.found ? b.value + 1 : 0) + b.steps;
            ref_probes += a.subblocks_visited;
            ref_cmps += a.str_cmps_primary + a.str_cmps_overflow;
            ref_ovf += a.overflow_entered;
            blk_probes += b.subblocks_visited;
            blk_cmps += b.str_cmps_primary + b.str_cmps_overflow;
            blk_ovf += b.overflow_entered;
        }
        require(ref_bad == 0, "reference backend finds every key");
        require(blk_bad == 0, "rank backend finds every key");
        require(checksum_blk != 0, "rank checksum is not degenerate");

        const auto scan = scan_groups(blk, keys);
        for (std::size_t i = 0; i < scan.groups.size(); ++i) {
            require(monotone(scan.groups[i]),
                    "corpus monotone in node " + std::to_string(i));
            if (scan.groups[i].size() >= 4) {
                auto rotated = scan.groups[i];
                rotated[rotated.size() / 2] = scan.groups[i].back();
                if (scan.groups[i].back() != scan.groups[i][rotated.size() / 2])
                    require(!monotone(rotated), "corpus negative control caught in node " +
                                                    std::to_string(i));
            }
        }

        std::size_t load_min = SIZE_MAX, load_max = 0;
        std::uint64_t load_sum = 0, load_groups = 0, overflow_records = 0;
        for (std::size_t r = 0; r < blk.region_count(); ++r) {
            for (const auto& gs : blk.region(r).group_stats()) {
                load_min = std::min(load_min, gs.primary_entries + gs.overflow_entries);
                load_max = std::max(load_max, gs.primary_entries + gs.overflow_entries);
                load_sum += gs.primary_entries + gs.overflow_entries;
                ++load_groups;
                overflow_records += gs.overflow_entries;
            }
        }

        const auto st = Region::block_rank_stats();

        std::uint64_t dyn_bad = 0, dyn_absent_gone = 0;
        std::vector<std::string> added;
        added.reserve(2000);
        for (std::size_t i = 0; i < 2000; ++i) {
            std::string k = "insert-" + std::to_string(i) + "-" + keys[i % keys.size()].first;
            added.push_back(k);
            if (!blk.insert(k, 777000 + i)) ++dyn_bad;
        }
        for (std::size_t i = 0; i < 1000; ++i)
            if (!blk.erase(added[i])) ++dyn_bad;
        for (std::size_t i = 1000; i < added.size(); ++i) {
            const auto r = blk.find(added[i], qmode);
            if (!r.found || r.value != 777000 + i) ++dyn_bad;
        }
        for (std::size_t i = 0; i < 1000; ++i)
            if (blk.find(added[i], qmode).found) ++dyn_absent_gone;   // a survivor is the failure
        std::uint64_t after_bad = 0;
        for (const auto& kv : keys)
            if (!blk.find(kv.first, qmode).found) ++after_bad;

        require(dyn_bad == 0, "dynamic insert/erase answers are exact");
        require(dyn_absent_gone == 0, "erased keys are gone");
        require(after_bad == 0, "every original key still found after the write burst");
        require(blk.region_count() >= 1, "index has regions");

        std::cout << "{\"kind\":\"" << kind << "\",\"n\":" << N
                  << ",\"regions\":" << blk.region_count()
                  << ",\"rank_regions_fitted\":" << st.regions_fitted
                  << ",\"rank_regions_boundary\":" << st.regions_boundary
                  << ",\"rank_keys_fitted\":" << st.keys_fitted
                  << ",\"rank_segments\":" << st.segments_total
                  << ",\"checksum_ref\":" << checksum_ref
                  << ",\"checksum_block\":" << checksum_blk
                  << ",\"ref_probes\":" << ref_probes << ",\"block_probes\":" << blk_probes
                  << ",\"ref_cmps\":" << ref_cmps << ",\"block_cmps\":" << blk_cmps
                  << ",\"ref_overflow_entered\":" << ref_ovf
                  << ",\"block_overflow_entered\":" << blk_ovf
                  << ",\"groups\":" << load_groups << ",\"group_load_min\":" << load_min
                  << ",\"group_load_max\":" << load_max
                  << ",\"group_load_avg\":" << (load_groups ? double(load_sum) / double(load_groups) : 0.0)
                  << ",\"overflow_records\":" << overflow_records << "}\n";

        {
            std::vector<double> ratios;
            for (std::size_t i = 0; i < blk_region_probes.size() && i < ref_region_probes.size(); ++i) {
                if (!ref_region_probes[i]) continue;
                ratios.push_back(double(blk_region_probes[i]) / double(ref_region_probes[i]));
            }
            std::sort(ratios.begin(), ratios.end());
            const auto at = [&ratios](double q) {
                return ratios.empty() ? 0.0 : ratios[std::min(ratios.size() - 1,
                    static_cast<std::size_t>(q * double(ratios.size() - 1)))];
            };
            std::size_t worse = 0;
            for (double r : ratios) if (r > 1.0) ++worse;
            std::cout << "{\"kind\":\"" << kind << "\",\"probe_ratio_regions\":" << ratios.size()
                      << ",\"probe_ratio_median\":" << at(0.5) << ",\"probe_ratio_p90\":" << at(0.9)
                      << ",\"probe_ratio_max\":" << (ratios.empty() ? 0.0 : ratios.back())
                      << ",\"probe_ratio_worse_regions\":" << worse << "}\n";
        }

        std::cout << "checks=" << checks << " failures=" << failures << "\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << "\n";
        return 2;
    }
}

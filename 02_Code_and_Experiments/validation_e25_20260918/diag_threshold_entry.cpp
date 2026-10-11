// Entry diagnosis for the equal-depth threshold directory (2026-10-11).
//
// WHAT THIS IS FOR. The BlockLinearRank round ended with a measured entry regression: its
// mapping concentrates keys (median per-region primary probes 2.0-3.8x, 62/64..64/64 regions
// worse). This file asks whether a directory of EQUAL-DEPTH THRESHOLDS avoids that, and what
// it costs -- WITHOUT touching index.hpp. Nothing here is integrated; no timing is taken.
//
// THE CANDIDATE. Inside one region, sort the keys and place G-1 thresholds at equal RANKS
// (threshold j at rank ceil(j*n/G)). A key's group is the number of thresholds it compares
// >= against. Two encodings of the same idea are measured, because they differ in exactly the
// property under test:
//
//   FIXED  thresholds are 63-bit coordinates (the same 9-bit/7-byte coordinate the rank model
//          uses). 8 bytes per threshold, one integer compare per step -- but two keys that
//          share those 7 suffix bytes share a coordinate, so they cannot be separated.
//   BYTES  thresholds are the boundary keys' SUFFIXES, compared with the index's own unsigned
//          byte order. No truncation, so the depth is exact; the price is the separator bytes
//          and a length-aware compare.
//
// WHAT IS MEASURED, per region and summed:
//   * the group LOAD distribution for four maps: the reference Context16 model, the
//     BlockLinearRank model, and the two threshold encodings -- all on the SAME regions, so
//     the comparison is like for like;
//   * the candidate's own LOOKUP cost: binary-search steps and compared bytes per query, over
//     the region's own keys;
//   * the candidate's resident bytes per region (thresholds only), against the reference
//     model's 16 x 258 x 4 B table.
//
// NOT measured here, and not claimed: end-to-end time, latency, throughput, or the effect on
// sub-block geometry. Those need an integrated candidate and an A/B round.
//
// Usage: diag_threshold_entry KIND N [TRANCOCSV]

#include "data.hpp"
#include "index.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;

namespace {

std::size_t common_prefix_len(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

// The region each key belongs to, computed ONCE: the per-region loop below would otherwise
// pay one routing search per region per key.
std::vector<std::size_t> bucket_of(Index& idx, const std::vector<Pair>& keys) {
    std::vector<std::size_t> out(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) out[i] = idx.locate(keys[i].first);
    return out;
}

// Length-aware unsigned byte comparison, i.e. the order the index itself uses.
bool byte_less(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    const int c = n ? std::memcmp(a.data(), b.data(), n) : 0;
    if (c) return c < 0;
    return a.size() < b.size();
}

// Bytes a memcmp-style comparison actually walks: everything up to and including the first
// differing byte, or the shorter length plus the length decision when one is a prefix of the
// other. This is the honest charge for a separator search -- min(len)+1 is a worst case that a
// search over DISTINCT separators almost never pays.
std::size_t compare_cost(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i + 1;
}

std::size_t lcp(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

struct LoadStats {
    std::uint64_t groups = 0, empty = 0, total = 0;
    std::size_t max = 0;
    double mean() const { return groups ? double(total) / double(groups) : 0.0; }
    double max_ratio() const { return mean() > 0 ? double(max) / mean() : 0.0; }
};

void add(LoadStats& s, std::size_t load) {
    ++s.groups;
    s.total += load;
    if (!load) ++s.empty;
    s.max = std::max(s.max, load);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_threshold_entry KIND N [TRANCOCSV]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        std::vector<Pair> keys = make_data(kind, nkeys, trcsv);
        std::sort(keys.begin(), keys.end(),
                  [](const Pair& a, const Pair& b) { return a.first < b.first; });

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

        Config base = e25sw::baseline_config(thr);
        base.model_backend = ModelBackend::Context16;
        Config cand = base;
        cand.model_backend = ModelBackend::BlockLinearRank;

        Index ref(base), blk(cand);
        ref.bulk_load(keys);
        blk.bulk_load(keys);
        if (ref.region_count() != blk.region_count())
            throw std::runtime_error("the two backends disagree on the region count");

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        LoadStats ctx_loads, rank_loads, fixed_loads, bytes_loads;
        std::uint64_t fixed_threshold_bytes = 0, bytes_threshold_bytes = 0;
        std::uint64_t fixed_steps = 0, fixed_cmp_bytes = 0;
        std::uint64_t bytes_steps = 0, bytes_cmp_bytes = 0;
        std::uint64_t truncated_threshold_bytes = 0;   // separators cut at lcp(prev,next)+1
        std::uint64_t ctx_subblocks = 0, bytes_subblocks = 0;   // the layout consequence
        std::uint64_t queries = 0;
        std::uint64_t distinct_fixed = 0;      // thresholds that are not repeats
        std::uint64_t truncated_regions = 0;   // regions where the fixed encoding loses depth

        const std::vector<std::size_t> bucket = bucket_of(ref, keys);

        for (std::size_t r = 0; r < ref.region_count(); ++r) {
            const Region& rr = ref.region(r);
            const std::uint32_t G = rr.groups();
            if (G == 0) continue;

            // The region's own key range, in the order the index routed it, so the partition
            // under test is the partition the index builds.
            std::vector<std::string_view> mine;
            for (std::size_t i = 0; i < keys.size(); ++i)
                if (bucket[i] == r) mine.push_back(keys[i].first);
            const std::size_t n = mine.size();
            if (!n) continue;

            for (const auto& gs : rr.group_stats())
                add(ctx_loads, gs.primary_entries + gs.overflow_entries);
            for (const auto& gs : blk.region(r).group_stats())
                add(rank_loads, gs.primary_entries + gs.overflow_entries);

            const std::size_t plen = common_prefix_len(mine.front(), mine.back());

            // ---- thresholds at equal ranks, both encodings ----
            std::vector<std::uint64_t> xf(n);
            for (std::size_t i = 0; i < n; ++i) xf[i] = blockrank::encode(mine[i], plen, 7);

            std::vector<std::uint64_t> tf;      // FIXED thresholds
            std::vector<std::string_view> tb;   // BYTES thresholds (suffix views)
            for (std::uint32_t j = 1; j < G; ++j) {
                const std::size_t rank = std::min(
                    n - 1, (static_cast<std::size_t>(j) * n) / G);
                tf.push_back(xf[rank]);
                tb.push_back(mine[rank].substr(plen));
            }
            std::sort(tf.begin(), tf.end());
            std::sort(tb.begin(), tb.end(), byte_less);
            fixed_threshold_bytes += tf.size() * sizeof(std::uint64_t);
            // The classic separator compression: a boundary only has to be stored up to one byte
            // past what it shares with its SORTED neighbours. This is a memory account, not a
            // lookup model -- the integrated round would measure the search itself.
            for (std::size_t j = 0; j < tb.size(); ++j) {
                const std::size_t prev = j ? lcp(tb[j - 1], tb[j]) : 0;
                const std::size_t next = (j + 1 < tb.size()) ? lcp(tb[j], tb[j + 1]) : 0;
                const std::size_t keep = std::min(tb[j].size(), std::max(prev, next) + 1);
                truncated_threshold_bytes += keep;
            }
            for (const auto& s : tb) bytes_threshold_bytes += s.size();
            const std::size_t distinct =
                static_cast<std::size_t>(std::unique(tf.begin(), tf.end()) - tf.begin());
            distinct_fixed += distinct;
            // The fixed encoding cannot separate the target depths when two thresholds share a
            // coordinate: those groups are unreachable, and the region's depth is capped.
            if (distinct < tf.size()) ++truncated_regions;

            // ---- the loads the two encodings would produce ----
            std::vector<std::size_t> lf(G, 0), lb(G, 0);
            std::vector<std::size_t> gb_of(n, 0);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t gf = static_cast<std::size_t>(
                    std::upper_bound(tf.begin(), tf.end(), xf[i]) - tf.begin());
                const std::size_t gb = static_cast<std::size_t>(
                    std::upper_bound(tb.begin(), tb.end(), mine[i].substr(plen), byte_less) -
                    tb.begin());
                gb_of[i] = std::min(gb, std::size_t(G - 1));
                ++lf[std::min(gf, std::size_t(G - 1))];
                ++lb[gb_of[i]];
            }

            // The layout consequence: how many sub-blocks each map needs, with the index's own
            // rule (max of the entry and byte constraints). Context16's numbers are MEASURED,
            // from its group_stats; the balanced map's are computed from its loads and the real
            // suffix bytes. Directory size -- and therefore locate cost -- follows from this.
            {
                const std::size_t slots = base.slots;
                const std::size_t per_block =
                    std::size_t(base.block_bytes) - sizeof(Header) - slots * sizeof(Entry);
                const auto blocks = [&](std::size_t entries, std::size_t bytes) {
                    if (!entries) return std::size_t(0);
                    const std::size_t by_entries = (entries + slots - 1) / slots;
                    const std::size_t by_bytes = (bytes + per_block - 1) / per_block;
                    return std::max<std::size_t>(std::max(by_entries, by_bytes), 1);
                };
                for (const auto& gs : rr.group_stats())
                    ctx_subblocks += blocks(gs.primary_entries, gs.primary_bytes);
                std::vector<std::size_t> lb_bytes(G, 0);
                for (std::size_t i = 0; i < n; ++i) lb_bytes[std::min(gb_of[i], std::size_t(G - 1))] +=
                    (mine[i].size() - plen);
                for (std::uint32_t g = 0; g < G; ++g)
                    bytes_subblocks += blocks(lb[g], lb_bytes[g]);
            }
            for (std::uint32_t g = 0; g < G; ++g) {
                add(fixed_loads, lf[g]);
                add(bytes_loads, lb[g]);
            }

            // ---- the candidate's own lookup cost, over the region's keys ----
            for (std::size_t i = 0; i < n; ++i) {
                ++queries;
                // FIXED: binary search on 63-bit coordinates, one 8-byte compare per step.
                {
                    std::size_t lo = 0, hi = tf.size();
                    while (lo < hi) {
                        const std::size_t mid = (lo + hi) / 2;
                        ++fixed_steps;
                        fixed_cmp_bytes += sizeof(std::uint64_t);
                        if (tf[mid] <= xf[i]) lo = mid + 1;
                        else hi = mid;
                    }
                }
                // BYTES: the same search with the index's own length-aware comparison. The
                // compared cost is charged as min(len) + 1, i.e. the bytes a memcmp walks plus
                // the length decision.
                {
                    const std::string_view key_suffix = mine[i].substr(plen);
                    std::size_t lo = 0, hi = tb.size();
                    while (lo < hi) {
                        const std::size_t mid = (lo + hi) / 2;
                        ++bytes_steps;
                        bytes_cmp_bytes += compare_cost(key_suffix, tb[mid]);
                        if (!byte_less(key_suffix, tb[mid])) lo = mid + 1;
                        else hi = mid;
                    }
                }
            }
        }

        std::cout << "{\"kind\":\"" << kind << "\",\"n\":" << keys.size()
                  << ",\"regions\":" << ref.region_count() << ",\"queries\":" << queries
                  << ",\"ctx_max\":" << ctx_loads.max << ",\"ctx_mean\":" << ctx_loads.mean()
                  << ",\"ctx_empty\":" << ctx_loads.empty
                  << ",\"rank_max\":" << rank_loads.max << ",\"rank_mean\":" << rank_loads.mean()
                  << ",\"rank_empty\":" << rank_loads.empty
                  << ",\"fixed_max\":" << fixed_loads.max << ",\"fixed_mean\":" << fixed_loads.mean()
                  << ",\"fixed_empty\":" << fixed_loads.empty
                  << ",\"fixed_threshold_bytes\":" << fixed_threshold_bytes
                  << ",\"fixed_distinct_thresholds\":" << distinct_fixed
                  << ",\"fixed_truncated_regions\":" << truncated_regions
                  << ",\"fixed_steps_per_query\":"
                  << (queries ? double(fixed_steps) / double(queries) : 0.0)
                  << ",\"fixed_cmp_bytes_per_query\":"
                  << (queries ? double(fixed_cmp_bytes) / double(queries) : 0.0)
                  << ",\"bytes_max\":" << bytes_loads.max << ",\"bytes_mean\":" << bytes_loads.mean()
                  << ",\"bytes_empty\":" << bytes_loads.empty
                  << ",\"bytes_threshold_bytes\":" << bytes_threshold_bytes
                  << ",\"bytes_truncated_threshold_bytes\":" << truncated_threshold_bytes
                  << ",\"ctx_subblocks\":" << ctx_subblocks
                  << ",\"bytes_subblocks\":" << bytes_subblocks
                  << ",\"bytes_steps_per_query\":"
                  << (queries ? double(bytes_steps) / double(queries) : 0.0)
                  << ",\"bytes_cmp_bytes_per_query\":"
                  << (queries ? double(bytes_cmp_bytes) / double(queries) : 0.0)
                  << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << "\n";
        return 2;
    }
}

// Scratch smoke: does the BlockLinearRank backend build, load, and answer?
#include "index.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace validation;

int main() {
    Config c;
    c.model_backend = ModelBackend::BlockLinearRank;
    c.multiblock = true;
    c.reuse_group_ids = true;
    c.compact_plan_commit = true;

    std::vector<Pair> pairs;
    for (int i = 0; i < 5000; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "key%06d", i);
        pairs.emplace_back(buf, static_cast<std::uint64_t>(i));
    }
    std::sort(pairs.begin(), pairs.end(),
              [](const Pair& a, const Pair& b) { return a.first < b.first; });

    Index idx(c);
    idx.bulk_load(pairs);

    Mode m{Route::Full, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
    std::size_t hits = 0, bad = 0;
    for (const auto& kv : pairs) {
        const auto r = idx.find(kv.first, m);
        if (r.found && r.value == kv.second) ++hits;
        else ++bad;
    }

    const auto st = Region::block_rank_stats();
    std::printf("hits=%zu bad=%zu regions_fitted=%llu keys_fitted=%llu boundary=%llu segments=%llu fits=%llu\n",
                hits, bad,
                static_cast<unsigned long long>(st.regions_fitted),
                static_cast<unsigned long long>(st.keys_fitted),
                static_cast<unsigned long long>(st.regions_boundary),
                static_cast<unsigned long long>(st.segments_total),
                static_cast<unsigned long long>(st.fits));
    return bad ? 1 : 0;
}

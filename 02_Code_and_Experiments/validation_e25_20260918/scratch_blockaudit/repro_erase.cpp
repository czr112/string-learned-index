// Focused repro for the dynamic cell of check_block_rank: insert -> find -> erase -> find.
#include "data.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace validation;

int main(int argc, char** argv) {
    const std::string kind = argc > 1 ? argv[1] : "synthetic";
    const std::size_t nkeys = argc > 2 ? std::stoull(argv[2]) : 20000;
    const std::string trcsv = argc > 3 ? argv[3] : "";

    std::vector<Pair> keys;
    if (kind == "synthetic") {
        for (std::size_t i = 0; i < nkeys; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "k%06zu", i);
            keys.emplace_back(buf, i);
        }
    } else {
        keys = make_data(kind, nkeys, trcsv);
    }
    std::sort(keys.begin(), keys.end(),
              [](const Pair& a, const Pair& b) { return a.first < b.first; });

    Config c = e25sw::baseline_config(32);
    c.model_backend = ModelBackend::BlockLinearRank;

    Index blk(c);
    blk.bulk_load(keys);
    const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

    std::size_t survivors = 0, first_bad = SIZE_MAX;
    std::string bad_key;
    for (std::size_t i = 0; i < 2000; ++i) {
        const std::string k = "insert-" + std::to_string(i) + "-" + keys[i % keys.size()].first;
        const bool ins = blk.insert(k, 777000 + i);
        if (!ins) { std::printf("insert %zu returned false\n", i); }
    }
    for (std::size_t i = 0; i < 1000; ++i) {
        const std::string k = "insert-" + std::to_string(i) + "-" + keys[i % keys.size()].first;
        const bool era = blk.erase(k);
        if (!era) std::printf("erase %zu returned false\n", i);
    }
    for (std::size_t i = 0; i < 1000; ++i) {
        const std::string k = "insert-" + std::to_string(i) + "-" + keys[i % keys.size()].first;
        const auto r = blk.find(k, qmode);
        if (r.found) {
            ++survivors;
            if (first_bad == SIZE_MAX) { first_bad = i; bad_key = k; }
        }
    }
    std::printf("survivors=%zu first_bad=%zu regions=%zu size=%zu\n",
                survivors, first_bad, blk.region_count(), blk.size());
    if (first_bad != SIZE_MAX) {
        const auto ri = blk.locate(bad_key);
        const auto g = blk.region(ri).placement_group(bad_key);
        const auto r = blk.find(bad_key, qmode);
        std::printf("  key=%s region=%zu placement_group=%u found=%d value=%llu source=%u\n",
                    bad_key.c_str(), ri, g, int(r.found),
                    (unsigned long long)r.value, unsigned(r.source));
        // A second erase: if THIS one succeeds, the first erase removed a DIFFERENT copy.
        const bool era2 = blk.erase(bad_key);
        const auto r2 = blk.find(bad_key, qmode);
        std::printf("  second erase=%d, still found=%d\n", int(era2), int(r2.found));
    }
    return survivors ? 1 : 0;
}

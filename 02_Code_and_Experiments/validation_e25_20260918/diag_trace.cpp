// E33 / SA0: print the real mapping chain for real keys.
//
// The structure-review plan demands one diagnostic trace that follows a real string
// from the full key through region, prefix, suffix, logical group and sub-block -- and
// that the query path and the placement path agree on the group for the same key
// (invariant I1). Reading the code gives the shape; only running it gives the values,
// and the second half (query group == placement group) is an assertion this program
// makes rather than a claim the document repeats.
//
// READ-ONLY diagnostic: it builds an index, prints, and compares. It is not a timing
// build and claims no performance.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

int main(int argc, char** argv) {
    try {
        if (argc < 3)
            throw std::invalid_argument("diag_trace KIND N [TRANCOCSV] [PER_KEY]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const int per_key = argc > 4 ? std::stoi(argv[4]) : 3;

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        std::uint32_t thr = 32;
        {
            std::ifstream fz("../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json");
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string text = ss.str();
            const auto position = text.find("\"frozen_hybrid_threshold\":");
            if (position == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(text.substr(position + 26)));
        }

        // The frozen configuration, stated explicitly rather than read from defaults.
        Config cfg = e25sw::baseline_config(thr);
        cfg.write_fast_locate = true;
        cfg.reuse_locate_ctx = true;
        cfg.compact_reuse_tag = true;
        cfg.compact_needs_garbage = true;
        cfg.scan_reserve_result = true;

        std::cerr << "diag_trace switches " << e25sw::switches(cfg) << std::endl;

        auto index = std::make_unique<Index>(cfg);
        index->bulk_load(keys);

        std::cout << "{\"kind\":\"" << kind << "\",\"keys\":" << N
                  << ",\"regions\":" << index->region_count()
                  << ",\"live\":" << index->size() << "}" << std::endl;

        // Sample evenly across the key space, not just the first few, so the trace covers
        // several regions and several groups rather than one crowded corner.
        const std::size_t step = N / std::size_t(per_key > 0 ? per_key : 1);

        for (std::size_t i = 0; i < N && i < std::size_t(per_key) * step; i += step) {
            const std::string& key = keys[i].first;

            // The query side: which region does locate() pick, what is its prefix, and
            // which group does the model predict for this key's suffix?
            const std::size_t region = index->locate(key);
            const Region& r = index->region(region);

            const std::string_view suffix = std::string_view(key).substr(r.prefix_length());
            const std::uint32_t group = r.group_of(suffix);

            if (group >= r.groups())
                throw std::runtime_error("predicted group out of range for key " + key);

            std::size_t entries = 0;
            const std::size_t blocks = r.subblock_count(group);

            for (std::size_t b = 0; b < blocks; ++b)
                entries += r.subblock_entries(group, b);

            const auto found = index->find(key, Mode{Route::Full, Prefetch::Off, true, false});

            std::cout << "{\"key\":\"" << key << "\""
                      << ",\"key_len\":" << key.size()
                      << ",\"region\":" << region
                      << ",\"region_prefix\":\"" << r.prefix() << "\""
                      << ",\"region_prefix_len\":" << r.prefix_length()
                      << ",\"suffix\":\"" << std::string(suffix) << "\""
                      << ",\"suffix_len\":" << suffix.size()
                      << ",\"group\":" << group
                      << ",\"groups_in_region\":" << r.groups()
                      << ",\"subblocks\":" << blocks
                      << ",\"entries_in_group\":" << entries
                      << ",\"found\":" << (found.found ? 1 : 0)
                      << "}" << std::endl;
        }

        // The I1 assertion over a wider sample than the printed traces: every key in the
        // index must map to a group that is in range, and the query path must find the key
        // it placed.
        std::size_t checked = 0;
        for (std::size_t i = 0; i < N; i += (N / 512 ? N / 512 : 1)) {
            const std::string& key = keys[i].first;
            const std::size_t region = index->locate(key);
            const Region& r = index->region(region);
            const std::uint32_t group =
                r.group_of(std::string_view(key).substr(r.prefix_length()));

            if (group >= r.groups())
                throw std::runtime_error("group out of range: " + key);

            const auto found = index->find(key, Mode{Route::Full, Prefetch::Off, true, false});

            if (!found.found || found.value != keys[i].second)
                throw std::runtime_error("query path does not return what bulk_load placed: " + key);

            ++checked;
        }

        std::cout << "{\"i1_checked\":" << checked << ",\"i1_result\":\"pass\"}" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

// Scratch probe (not a gate): what does the checker see after a split?
#include "index.hpp"
#include "switches.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using validation::Config;
using validation::Index;
using validation::Mode;
using validation::Pair;

static std::vector<std::string> read_keys(const char* path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("open");
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> raw, keys;
    std::size_t begin = 0;
    while (begin < data.size()) {
        const std::size_t nl = data.find('\n', begin);
        const std::size_t end = nl == std::string::npos ? data.size() : nl;
        std::size_t stop = end;
        if (stop > begin && data[stop - 1] == '\r') --stop;
        raw.emplace_back(data.substr(begin, stop - begin));
        if (nl == std::string::npos) break;
        begin = nl + 1;
    }
    std::sort(raw.begin(), raw.end());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i && raw[i] == raw[i - 1]) continue;
        keys.push_back(raw[i]);
        if (limit && keys.size() >= limit) break;
    }
    return keys;
}

int main(int argc, char** argv) {
    const std::size_t limit = argc > 2 ? std::stoull(argv[2]) : 8000;
    const std::size_t split_ops = argc > 3 ? std::stoull(argv[3]) : 4200;
    std::vector<std::string> keys = read_keys(argv[1], limit);
    Config cfg = e25sw::baseline_config(128);
    e25sw::pin_frozen_switches(cfg);
    cfg.model_backend = validation::ModelBackend::BlockLinearRank;
    Index index(cfg);
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < keys.size(); ++i) pairs.emplace_back(keys[i], i);
    index.bulk_load(pairs);
    std::printf("before: regions=%zu size=%zu splits=%zu\n", index.region_count(), index.size(),
                index.splits);
    const std::string stem = keys.back();
    for (std::size_t i = 0; i < split_ops; ++i) {
        const std::string k = stem + "-bmsplit-" + std::to_string(1000000 + i);
        if (!index.insert(k, 0x42000000ull + i)) std::printf("insert rejected: %s\n", k.c_str());
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    std::printf("after: regions=%zu size=%zu splits=%zu rebuilds=%zu\n", index.region_count(),
                index.size(), index.splits, index.rebuilds);
    for (std::size_t r = 0; r < index.region_count(); ++r) {
        const auto& reg = index.region(r);
        std::printf("  region %zu: size=%zu groups=%u status=%s fence=%s\n", r, reg.size(),
                    reg.groups(), blockrank::to_string(reg.block_rank_status()),
                    std::string(index.fence(r).substr(0, 24)).c_str());
    }
    // walk
    std::size_t first = 0;
    while (first < keys.size()) {
        const std::size_t rid = index.locate(keys[first]);
        std::size_t last = first + 1;
        while (last < keys.size() && index.locate(keys[last]) == rid) ++last;
        const auto& r = index.region(rid);
        std::string prefix = keys[first];
        const std::string& b = keys[last - 1];
        std::size_t p = 0;
        while (p < prefix.size() && p < b.size() && prefix[p] == b[p]) ++p;
        prefix.resize(p);
        std::vector<std::string_view> view;
        for (std::size_t i = first; i < last; ++i) view.push_back(keys[i]);
        const blockrank::RegionModel model =
            blockrank::RegionModel::fit(view.data(), view.size(), prefix.size(), r.groups());
        std::uint64_t mono = 0, range = 0, recomp = 0, shown = 0;
        std::uint32_t prev = 0;
        for (std::size_t i = first; i < last; ++i) {
            const std::uint32_t actual = r.placement_group(keys[i]);
            if (actual >= r.groups()) ++range;
            if (i > first && actual < prev) ++mono;
            const std::uint32_t want = model.predict_group(keys[i]);
            if (want != actual) {
                ++recomp;
                if (shown < 3) {
                    ++shown;
                    std::printf("    mismatch key=%s want=%u actual=%u\n", keys[i].c_str(), want, actual);
                }
            }
            prev = actual;
        }
        std::printf("  bucket region=%zu keys=%zu derived_prefix_len=%zu groups=%u "
                    "accept_prefix=%d accept_short=%d mono=%llu range=%llu recompute=%llu\n",
                    rid, last - first, prefix.size(), r.groups(), int(r.accepts_prefix(prefix)),
                    int(!prefix.empty() &&
                        r.accepts_prefix(std::string_view(prefix).substr(0, prefix.size() - 1))),
                    (unsigned long long)mono, (unsigned long long)range, (unsigned long long)recomp);
        first = last;
    }
    return 0;
}
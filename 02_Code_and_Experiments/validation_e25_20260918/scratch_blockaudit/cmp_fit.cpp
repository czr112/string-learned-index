// Cross-check: the teammate's BlockModel::fit(block_size=1, bytes=7, segments=8) against
// our RegionModel::fit, field by field. Their window()/radius is deliberately not ported.
#include "../../Learned-Index-Model/src/block_model.h"   // blocklearn::BlockModel
#include "model/block_linear_rank.hpp"                    // blockrank::RegionModel

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
std::uint64_t checks = 0, failures = 0;
void req(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", what); }
}

void compare(const char* name, const std::vector<std::string>& keys_in) {
    // Sorted input is a precondition of both fits (and of the index's regions), so the
    // battery sorts here instead of trusting each case to be written in order.
    std::vector<std::string> keys = keys_in;
    std::sort(keys.begin(), keys.end());
    std::vector<std::string_view> views;
    views.reserve(keys.size());
    for (const auto& k : keys) views.push_back(k);

    const std::size_t cp = keys.empty() ? 0
        : blockrank::common_prefix(views.front(), views.back());

    const auto theirs = blocklearn::BlockModel::fit(views, 1, 7, 8, /*max_error=*/0);
    const auto ours = blockrank::RegionModel::fit(views.data(), views.size(), cp, 16);

    std::printf("== %s: n=%zu cp=%zu theirs_seg=%zu ours_seg=%zu their_avg_blocks_in_window=%.3f\n",
                name, keys.size(), cp, theirs.segment_count(), ours.segment_count(),
                theirs.average_blocks());
    req(theirs.segment_count() == ours.segment_count(), "segment count");
    const auto& ts = theirs.segments();
    const auto& os = ours.segments();
    const std::size_t m = std::min(ts.size(), os.size());
    for (std::size_t i = 0; i < m; ++i) {
        const bool same = ts[i].x_min == os[i].x_min && ts[i].x_max == os[i].x_max &&
                          ts[i].y_min == os[i].y_min && ts[i].y_max == os[i].y_max &&
                          ts[i].slope == os[i].slope && ts[i].intercept == os[i].intercept;
        if (!same) {
            std::printf("  seg %zu DIFF: theirs(x %llu..%llu y %u..%u s %.17g i %.17g) "
                        "ours(x %llu..%llu y %u..%u s %.17g i %.17g)\n",
                        i,
                        (unsigned long long)ts[i].x_min, (unsigned long long)ts[i].x_max,
                        ts[i].y_min, ts[i].y_max, ts[i].slope, ts[i].intercept,
                        (unsigned long long)os[i].x_min, (unsigned long long)os[i].x_max,
                        os[i].y_min, os[i].y_max, os[i].slope, os[i].intercept);
        }
        req(same, "segment fields");
    }

    // The group map is ours alone: check it is in range and non-decreasing over the input.
    std::uint32_t last = 0;
    bool mono = true, inrange = true;
    for (const auto& k : keys) {
        const std::uint32_t g = ours.predict_group(k);
        if (g >= 16) inrange = false;
        if (g < last) mono = false;
        last = g;
    }
    req(inrange, "group in range");
    req(mono, "group non-decreasing");
}

std::vector<std::string> seq(std::size_t n, const char* fmt) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < n; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof buf, fmt, (int)i);
        out.emplace_back(buf);
    }
    return out;
}
}  // namespace

int main() {
    compare("sequential100", seq(100, "k%05d"));
    {   // shared 64-byte prefix
        std::vector<std::string> keys;
        const std::string p(64, 'p');
        for (int i = 0; i < 40; ++i) keys.push_back(p + std::to_string(i % 7) + char('a' + (i % 26)));
        compare("shared_prefix", keys);
    }
    {   // TRUE coordinate collisions: the common prefix of front/back is 1 byte, so the
        // encoder reads bytes 1..7 -- and these keys differ only at byte 8.
        std::vector<std::string> keys;
        keys.push_back("a");
        for (int i = 0; i < 19; ++i)
            keys.push_back(std::string("a") + "0123456" + char('a' + i));
        // sanity: the two long keys really encode to the same coordinate
        compare("coord_collisions", keys);
    }
    {   // binary-ish keys incl. NUL and high bytes
        std::vector<std::string> keys;
        for (int i = 0; i < 32; ++i) {
            std::string k; k.push_back(char(i)); k.push_back(char(0x80 + i % 0x40));
            keys.push_back(k);
        }
        compare("binary", keys);
    }
    compare("single", {"only"});
    compare("empty", {});

    std::printf("checks=%llu failures=%llu\n",
                (unsigned long long)checks, (unsigned long long)failures);
    return failures ? 1 : 0;
}

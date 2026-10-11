// Scratch probe: what do the live regions and the depth-dir stats actually report?
#include "data.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"
#include <cstdio>
#include <string>
#include <vector>
using namespace validation;

int main() {
    std::vector<Pair> keys;
    for (int i = 0; i < 100; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "dup%d", i % 5);
        keys.emplace_back(buf, i + 1);
    }
    std::sort(keys.begin(), keys.end());

    Config tc = e25sw::baseline_config(32);
    tc.model_backend = ModelBackend::EqualDepthDirTrunc;
    tc.fixed_groups = 8;
    Config fc = tc;
    fc.model_backend = ModelBackend::EqualDepthDir;

    Region::reset_depth_dir_stats();
    Region rt(keys, tc);
    Region ru(keys, fc);
    const auto st = Region::depth_dir_stats();
    std::printf("dup stats: thresholds=%llu bytes=%llu truncated=%llu fitted=%llu dupthr=%llu boundary=%llu fits=%llu\n",
                (unsigned long long)st.thresholds_total, (unsigned long long)st.threshold_bytes_total,
                (unsigned long long)st.regions_truncated, (unsigned long long)st.regions_fitted,
                (unsigned long long)st.regions_with_duplicate_thresholds,
                (unsigned long long)st.regions_boundary, (unsigned long long)st.fits);
    std::printf("rt: groups=%u prefix=%zu count=%zu bytes=%zu truncated=%d\n", rt.groups(),
                rt.prefix_length(), rt.depth_dir_thresholds(), rt.depth_dir_separator_bytes(),
                (int)rt.depth_dir_truncated());
    std::printf("ru: groups=%u prefix=%zu count=%zu bytes=%zu truncated=%d\n", ru.groups(),
                ru.prefix_length(), ru.depth_dir_thresholds(), ru.depth_dir_separator_bytes(),
                (int)ru.depth_dir_truncated());

    std::vector<std::string_view> views;
    for (const auto& kv : keys) views.push_back(kv.first);
    const auto mt = equivdir::RegionModel::fit(views.data(), views.size(), rt.prefix_length(),
                                               rt.groups(), true);
    std::printf("model: count=%zu bytes=%zu truncated=%d prefix=%zu\n", mt.threshold_count(),
                mt.separator_bytes(), (int)mt.truncated(), mt.prefix_len());
    for (std::size_t i = 0; i < mt.threshold_count(); ++i) {
        const auto s = mt.separator(i);
        std::printf("  t[%zu] = \"", i);
        for (char c : s) std::printf("%c", c);
        std::printf("\" (%zu)\n", s.size());
    }

    // synthetic, like the gate's first case
    std::vector<Pair> sk;
    for (int i = 0; i < 100; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "synthetic%04d", i);
        sk.emplace_back(buf, i + 1);
    }
    std::sort(sk.begin(), sk.end());
    Region::reset_depth_dir_stats();
    Region st_r(sk, tc);
    Region su_r(sk, fc);
    const auto st2 = Region::depth_dir_stats();
    std::printf("synthetic stats: thresholds=%llu bytes=%llu truncated=%llu fitted=%llu\n",
                (unsigned long long)st2.thresholds_total, (unsigned long long)st2.threshold_bytes_total,
                (unsigned long long)st2.regions_truncated, (unsigned long long)st2.regions_fitted);
    std::printf("st_r: groups=%u prefix=%zu count=%zu bytes=%zu; su_r bytes=%zu\n", st_r.groups(),
                st_r.prefix_length(), st_r.depth_dir_thresholds(), st_r.depth_dir_separator_bytes(),
                su_r.depth_dir_separator_bytes());
    return 0;
}

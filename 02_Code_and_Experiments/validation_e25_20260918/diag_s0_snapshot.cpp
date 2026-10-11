// S0 snapshot: the CURRENT structure, written down explicitly.
//
// Task book section 4.1: "先建立当前结构 S0 的显式配置快照。记录主块预算、区域划分、逻辑组数、
// 已启用维护策略、溢出策略和所有实验开关；尤其核清 E31 局部整理与扫描定位缓存当前是否真的启用。"
//
// Two reasons this is a PROGRAM and not a paragraph:
//   * a paragraph about a Config drifts the moment a field is added, and this tree has added
//     nine of them since the last such paragraph;
//   * the structural half (regions, groups, sub-blocks, per-group spread) can only come from a
//     real index, and it is what every later S0/S1 comparison has to be equal to.
//
// It prints one JSON object per corpus and prints the configuration ONCE, before them, so a
// record's configuration can be read without the reader reconstructing it from the source.
//
// Diagnostic only. It reads accessors and never mutates, so it can be run against the shipped
// build without a separate configuration.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;

static void dump_config(const Config& c) {
    std::cout << "{\"section\":\"config\""
              << ",\"switch_string\":\"" << e25sw::switches(c) << "\""
              << ",\"block_bytes\":" << c.block_bytes
              << ",\"slots\":" << c.slots
              << ",\"load\":" << c.load
              << ",\"region_keys\":" << c.region_keys
              << ",\"fixed_groups\":" << c.fixed_groups
              << ",\"ov_threshold\":" << c.ov_threshold
              << ",\"offset_directory\":" << (c.offset_directory ? "true" : "false")
              << ",\"multiblock\":" << (c.multiblock ? "true" : "false")
              << ",\"reuse_group_ids\":" << (c.reuse_group_ids ? "true" : "false")
              << ",\"write_simd\":" << (c.write_simd ? "true" : "false")
              << ",\"write_locate\":" << (c.write_locate ? "true" : "false")
              << ",\"canonical\":\"" << (c.canonical == Route::Full ? "Full" : "not-Full") << "\""
              << ",\"model_kind\":\"Context16/Position16/Unigram\""
              << ",\"model_backend\":\""
              << (c.model_backend == ModelBackend::Context16 ? "Context16" : "LitsHpt") << "\""
              // The query mode is NOT a Config field: it is a global the benchmarks pass to
              // find(). Its route differs from the placement route, which is exactly the
              // asymmetry check_g1_contract's part C measures.
              << ",\"query_mode_route\":\""
              << (query_mode.route == Route::Full ? "Full" : "Interval") << "\""
              << ",\"canonical_route\":\""
              << (c.canonical == Route::Full ? "Full" : "Interval") << "\""
              << "}" << std::endl;
}

int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::invalid_argument(
                "diag_s0_snapshot KIND[:N],... [TRANCOCSV]  (e.g. mixed:131072,skew:131072,tranco:262144)");

        const std::vector<std::pair<std::string, std::size_t>> wanted = [] (const std::string& s) {
            std::vector<std::pair<std::string, std::size_t>> out;
            std::stringstream ss(s);
            std::string item;
            while (std::getline(ss, item, ',')) {
                const auto c = item.find(':');
                if (c == std::string::npos) throw std::invalid_argument("expected KIND:N");
                out.emplace_back(item.substr(0, c), std::stoull(item.substr(c + 1)));
            }
            return out;
        }(argv[1]);

        const std::string trcsv = argc > 2 ? argv[2] : "";

        // The shipped configuration, from the pinning helper -- never from the raw defaults,
        // because Config's own defaults are the pre-E9 single-block path.
        Config cfg = e25sw::baseline_config(32);
        e25sw::pin_frozen_switches(cfg);

        dump_config(cfg);

        for (const auto& kv : wanted) {
            const std::string& kind = kv.first;
            const std::size_t n = kv.second;

            const std::string frozen_rel =
                "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
            std::uint32_t thr = 32;
            {
                std::ifstream fz(frozen_rel);
                if (fz) {
                    std::stringstream ss; ss << fz.rdbuf();
                    const std::string t = ss.str();
                    const auto p = t.find("\"frozen_hybrid_threshold\":");
                    if (p != std::string::npos)
                        thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
                }
            }

            Config c = e25sw::baseline_config(thr);
            e25sw::pin_frozen_switches(c);

            const auto keys = make_data(kind, n, trcsv);

            Index idx(c);
            idx.bulk_load(keys);

            // ---- the structure, as it actually came out ----------------------------
            std::vector<std::size_t> per_region_groups;
            std::vector<std::size_t> per_region_subblocks;
            std::size_t groups_total = 0, subblocks_total = 0;
            std::size_t primary_entries = 0, overflow_entries = 0;
            std::size_t garbage_total = 0, free_total = 0;
            std::size_t max_group_entries = 0;

            for (std::size_t r = 0; r < idx.region_count(); ++r) {
                const Region& reg = idx.region(r);
                const std::size_t g = reg.groups();
                per_region_groups.push_back(g);
                groups_total += g;

                std::size_t sb = 0;
                for (std::size_t i = 0; i < g; ++i) sb += reg.subblock_count(static_cast<std::uint32_t>(i));
                per_region_subblocks.push_back(sb);
                subblocks_total += sb;

                for (const auto& x : reg.group_stats()) {
                    primary_entries += x.primary_entries;
                    overflow_entries += x.overflow_entries;
                    garbage_total += x.garbage_bytes;
                    free_total += x.free_bytes;
                    max_group_entries = std::max(max_group_entries, x.primary_entries + x.overflow_entries);
                }
            }

            const auto mb = idx.memory_breakdown();
            const std::size_t live = idx.size();

            std::cout << "{\"section\":\"structure\""
                      << ",\"kind\":\"" << kind << "\""
                      << ",\"keys\":" << n
                      << ",\"live\":" << live
                      << ",\"regions\":" << idx.region_count()
                      << ",\"groups_total\":" << groups_total
                      << ",\"subblocks_total\":" << subblocks_total
                      << ",\"groups_per_region_min\":"
                      << *std::min_element(per_region_groups.begin(), per_region_groups.end())
                      << ",\"groups_per_region_max\":"
                      << *std::max_element(per_region_groups.begin(), per_region_groups.end())
                      << ",\"primary_entries\":" << primary_entries
                      << ",\"overflow_entries\":" << overflow_entries
                      << ",\"max_group_entries\":" << max_group_entries
                      << ",\"garbage_bytes\":" << garbage_total
                      << ",\"free_bytes\":" << free_total
                      << ",\"memory\":" << idx.memory()
                      << ",\"mb_blocks\":" << mb.blocks
                      << ",\"mb_model\":" << mb.model
                      << ",\"mb_prefix\":" << mb.prefix
                      << ",\"mb_overflow\":" << mb.overflow
                      << ",\"mb_tables\":" << mb.tables
                      << ",\"mb_directory\":" << mb.directory
                      << ",\"mb_self\":" << mb.self
                      << "}" << std::endl;
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

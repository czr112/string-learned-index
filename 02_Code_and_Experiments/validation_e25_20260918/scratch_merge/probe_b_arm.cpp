// Scratch probe: reproduce the 2026-10-10 scan-entry device's cross-side compare sums for the
// B (pinned baseline), W (baseline + subblock_reclaim + reclaim_widest) and CWS configs on
// grow/tranco/262144, fracs 1.0 and 2.0, limit 256, 17 starts -- to locate why the M4 entry
// device's C_old differs from the recorded combo_se file.
#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;
using Reference = std::map<std::string, std::uint64_t, Less>;

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::invalid_argument("probe_b_arm KIND N [TRANCOCSV]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        std::uint32_t thr = 32;
        {
            const std::string frozen_rel =
                "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        for (int variant = 0; variant < 3; ++variant) {
            Config cfg = e25sw::baseline_config(thr);
            e25sw::pin_frozen_switches(cfg);
            if (variant == 1) { cfg.subblock_reclaim = true; cfg.reclaim_widest = true; }
            if (variant == 2) { cfg.subblock_reclaim = true; cfg.reclaim_widest = true;
                                cfg.compact_region_routing = true; cfg.scan_ordered_start = true; }

            std::vector<std::vector<std::uint16_t>> sg, caps;
            std::vector<std::vector<std::uint32_t>> bb;
            {
                Index probe(cfg);
                probe.bulk_load(keys);
                for (std::size_t i = 0; i < probe.region_count(); ++i) {
                    const auto& gs = probe.region(i).group_stats();
                    const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
                    const Alloc d = demand_single(gs, budget);
                    StrictLog log;
                    std::vector<std::uint16_t> c;
                    const Alloc strict = demand_strict(gs, d, log, c);
                    sg.push_back(strict.sg);
                    bb.push_back(strict.bb);
                    caps.push_back(std::move(c));
                }
            }

            const double FRACS[5] = {0.00, 0.25, 0.50, 1.00, 2.00};
            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;
            Reference ref;
            for (const auto& kv : keys) {
                live.push_back(kv.first);
                live_set.insert(kv.first);
                ref[kv.first] = kv.second;
            }

            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg, bb, caps);

            std::uint64_t serial = 0;
            std::size_t done = 0;
            ::validation::e25::counts().reset();
            for (double frac : FRACS) {
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                std::vector<std::pair<std::string, std::string>> moves;
                for (std::size_t i = done; i < target; ++i) {
                    const std::string victim = live.front();
                    live.pop_front();
                    live_set.erase(victim);
                    std::string base = victim;
                    const auto pos = base.find("~r");
                    if (pos != std::string::npos) base.resize(pos);
                    std::string fresh = base + "~r" + std::to_string(serial);
                    const std::uint64_t val = 0x7000ULL + serial;
                    ++serial;
                    moves.emplace_back(victim, fresh);
                    ref.erase(victim);
                    ref[fresh] = val;
                    live.push_back(fresh);
                    live_set.insert(fresh);
                }
                // Interleaved exactly like bench_combo's grow arm (its documented shape).
                for (const auto& m : moves) {
                    s->erase(m.first);
                    s->insert(m.second, ref.at(m.second));
                }
                done = target;
                if (frac != 1.0 && frac != 2.0) continue;

                std::vector<Pair> live_pairs;
                live_pairs.reserve(live.size());
                for (const auto& k : live) live_pairs.emplace_back(k, ref.at(k));
                std::sort(live_pairs.begin(), live_pairs.end(),
                          [](const Pair& x, const Pair& y) { return compare(x.first, y.first) < 0; });
                std::vector<std::string> starts;
                starts.push_back(std::string());
                for (int i = 0; i < 16 && !live_pairs.empty(); ++i)
                    starts.push_back(live_pairs[(std::size_t(i) * live_pairs.size()) / 16].first);

                std::uint64_t merge = 0, rows = 0, outp = 0, outo = 0;
                MergeShadowCounts tot;
                std::uint64_t sh_mismatch = 0;
                for (const auto& st : starts) {
                    ::validation::e25::counts().reset();
                    const auto out = s->scan(st, 256);
                    merge += ::validation::e25::counts().scan_merge_cmps;
                    outp += ::validation::e25::counts().scan_out_primary;
                    outo += ::validation::e25::counts().scan_out_overflow;
                    rows += out.size();
                    std::vector<Pair> sh;
                    const MergeShadowCounts sc = s->diag_scan_merge_shadow(st, 256, sh);
                    tot.add(sc);
                    if (sh != out) ++sh_mismatch;
                }
                std::cout << "variant=" << variant << " frac=" << frac << " merge=" << merge
                          << " rows=" << rows << " outp=" << outp << " outo=" << outo
                          << " recl=" << ::validation::e25::counts().subblock_reclaims
                          << " wide=" << ::validation::e25::counts().reclaim_widened
                          << " shadow_cmps=" << tot.merge_cmps
                          << " probes=" << tot.probes << " ok=" << tot.probe_ok
                          << " fail=" << tot.probe_fail << " eq=" << tot.probe_fail_equal
                          << " seg_rows=" << tot.seg_rows << " sh_mismatch=" << sh_mismatch
                          << std::endl;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
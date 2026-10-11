// P1 read-path entry diagnosis (2026-10-11): where does a point query's work go, now that the
// equal-depth directory has improved the group distribution?
//
// NOT a timing device: counts only, on the bench_depth_dir workload (same generator, region
// rules, capacity rules, frozen threshold, qmode and probe seeds). See
// docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md section 4 for the frozen apparatus.
//
// Per query, on the SAME probe list for both arms (Context16, EqualDepthDirTrunc):
//   outer region navigation   e25::locate_cmp_calls / locate_cmp_bytes (Index::locate probes)
//   model prediction          result.steps; for DDirT also the traced directory shadow
//                             (probes, compared bytes, termination reasons)
//   fingerprint               e25::fp_hash_bytes total, fp_fused_bytes model-side; the tail the
//                             caller still hashes is the difference
//   main block                subblocks_visited, SIMD batches, tag candidates, candidate
//                             blocks; exact compares split primary/overflow by site
//   overflow                  containers entered, record probes, exact compares
//
// Assertions (any failure exits non-zero): the device really runs mode.locate == false
// (locate_probes == 0 on every query -- sequential label probe, not a sub-block directory
// search); cmp totals equal primary + overflow; every find answer matches the independent
// oracle (hits value-checked, misses verified absent); for DDirT every probe whose prediction
// ran reproduces result.steps and placement_group() through the traced shadow.
//
// Usage: diag_depth_dir_read_path KIND N [TRANCOCSV]

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
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace validation;

namespace {

bool ubyte_less(std::string_view a, std::string_view b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) {
                                            return static_cast<unsigned char>(x) <
                                                   static_cast<unsigned char>(y);
                                        });
}

struct Probe {
    std::string key;
    bool expect_hit = false;
    std::uint64_t expect_value = 0;
};

std::vector<Probe> make_probes(const std::vector<std::string>& live, std::size_t count,
                               std::uint64_t seed, int miss_percent,
                               const std::unordered_map<std::string, std::uint64_t>& values) {
    std::vector<Probe> out;
    if (live.empty()) return out;
    std::mt19937_64 rng(seed);
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (static_cast<int>(rng() % 100) < miss_percent) {
            std::string k = live[rng() % live.size()];
            k.push_back('~');
            k.push_back(static_cast<char>('a' + (rng() % 26)));
            out.push_back(Probe{k, false, 0});
        } else {
            const std::string& k = live[rng() % live.size()];
            const auto it = values.find(k);
            out.push_back(Probe{k, true, it == values.end() ? 0 : it->second});
        }
    }
    return out;
}

std::uint64_t hash_probes(const std::vector<Probe>& probes) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto& p : probes) {
        for (unsigned char ch : p.key) h = (h ^ ch) * 1099511628211ULL;
        h = (h ^ (p.expect_hit ? 1u : 0u)) * 1099511628211ULL;
    }
    return h;
}

struct Acc {
    std::uint64_t queries = 0;
    std::uint64_t key_bytes = 0;
    // outer region navigation
    std::uint64_t region_probes = 0, region_bytes = 0;
    // model prediction
    std::uint64_t model_bytes = 0, model_bytes_max = 0;
    // fingerprint
    std::uint64_t fp_total = 0, fp_fused = 0;
    // directory shadow (DDirT arm only)
    std::uint64_t dir_runs = 0, dir_not_fitted = 0;
    std::uint64_t dir_probes = 0, dir_compared = 0;
    std::uint64_t dir_end_first_diff = 0, dir_end_sep_le_q = 0, dir_end_sep_gt_q = 0,
                  dir_sep_prefix_of_q = 0;
    // main block
    std::uint64_t subblocks = 0, simd_batches = 0, tag_candidates = 0, candidate_blocks = 0;
    std::uint64_t boundary_probes = 0, boundary_bytes = 0;
    std::uint64_t exact_calls = 0, exact_bytes = 0, ov_calls = 0, ov_bytes = 0;
    std::uint64_t cmp_calls_total = 0, cmp_bytes_total = 0;
    std::uint64_t str_primary = 0, str_overflow = 0;
    // overflow
    std::uint64_t ov_entered = 0, ov_checks = 0;
    // denominators / verification
    std::uint64_t steps_zero = 0, mismatches = 0, oracle_violations = 0;
    std::uint64_t mode_violations = 0, total_violations = 0, trace_violations = 0,
                  group_violations = 0;
    std::uint64_t fused_eq_steps = 0, fused_ne_steps = 0;
    std::vector<std::uint64_t> steps_dist, subblocks_dist, dir_probes_dist;
    std::vector<std::uint64_t> region_bytes_dist;   // per-region model bytes, sized by caller
};

struct Pcts {
    std::uint64_t p50 = 0, p90 = 0, p99 = 0, max = 0;
};

Pcts pcts(std::vector<std::uint64_t> v) {
    Pcts r;
    if (v.empty()) return r;
    std::sort(v.begin(), v.end());
    r.p50 = v[static_cast<std::size_t>(0.50 * double(v.size() - 1))];
    r.p90 = v[static_cast<std::size_t>(0.90 * double(v.size() - 1))];
    r.p99 = v[static_cast<std::size_t>(0.99 * double(v.size() - 1))];
    r.max = v.back();
    return r;
}

std::ofstream g_out;   // results/main/depth_read_path_<kind>.jsonl, append

void emit(const std::string& kind, std::size_t n, std::uint32_t thr, const char* arm,
          const char* backend, const char* phase, const char* stratum, std::size_t region_count,
          std::uint64_t probe_hash, Acc& a) {
    const Pcts sp = pcts(a.steps_dist);
    const Pcts sb = pcts(a.subblocks_dist);
    const Pcts dp = pcts(a.dir_probes_dist);
    const Pcts rb = pcts(a.region_bytes_dist);
    const std::uint64_t fp_tail = a.fp_total - a.fp_fused;
    std::ostringstream o;
    o << "{\"device\":\"diag_depth_dir_read_path\",\"kind\":\"" << kind << "\",\"n\":" << n
      << ",\"thr\":" << thr << ",\"regions\":" << region_count << ",\"arm\":\"" << arm
      << "\",\"backend\":\"" << backend << "\",\"phase\":\"" << phase << "\",\"stratum\":\""
      << stratum << "\",\"queries\":" << a.queries << ",\"probe_hash\":" << probe_hash
      << ",\"key_bytes_avg\":" << (a.queries ? double(a.key_bytes) / double(a.queries) : 0.0)
      << ",\"region_probes\":" << a.region_probes << ",\"region_bytes\":" << a.region_bytes
      << ",\"model_bytes\":" << a.model_bytes << ",\"model_bytes_max\":" << a.model_bytes_max
      << ",\"steps_p50\":" << sp.p50 << ",\"steps_p90\":" << sp.p90 << ",\"steps_p99\":" << sp.p99
      << ",\"steps_max\":" << sp.max
      << ",\"fp_total\":" << a.fp_total << ",\"fp_fused\":" << a.fp_fused
      << ",\"fp_tail\":" << fp_tail
      << ",\"dir_runs\":" << a.dir_runs << ",\"dir_not_fitted\":" << a.dir_not_fitted
      << ",\"dir_probes\":" << a.dir_probes << ",\"dir_compared\":" << a.dir_compared
      << ",\"dir_probes_p50\":" << dp.p50 << ",\"dir_probes_p99\":" << dp.p99
      << ",\"dir_end_first_diff\":" << a.dir_end_first_diff
      << ",\"dir_end_sep_le_q\":" << a.dir_end_sep_le_q
      << ",\"dir_end_sep_gt_q\":" << a.dir_end_sep_gt_q
      << ",\"dir_sep_prefix_of_q\":" << a.dir_sep_prefix_of_q
      << ",\"subblocks\":" << a.subblocks << ",\"subblocks_p50\":" << sb.p50
      << ",\"subblocks_p99\":" << sb.p99 << ",\"simd_batches\":" << a.simd_batches
      << ",\"tag_candidates\":" << a.tag_candidates << ",\"candidate_blocks\":" << a.candidate_blocks
      << ",\"boundary_probes\":" << a.boundary_probes << ",\"boundary_bytes\":" << a.boundary_bytes
      << ",\"exact_calls\":" << a.exact_calls << ",\"exact_bytes\":" << a.exact_bytes
      << ",\"ov_calls\":" << a.ov_calls << ",\"ov_bytes\":" << a.ov_bytes
      << ",\"cmp_calls_total\":" << a.cmp_calls_total << ",\"cmp_bytes_total\":" << a.cmp_bytes_total
      << ",\"str_primary\":" << a.str_primary << ",\"str_overflow\":" << a.str_overflow
      << ",\"ov_entered\":" << a.ov_entered << ",\"ov_checks\":" << a.ov_checks
      << ",\"region_model_bytes_p50\":" << rb.p50 << ",\"region_model_bytes_p99\":" << rb.p99
      << ",\"region_model_bytes_max\":" << rb.max
      << ",\"steps_zero\":" << a.steps_zero << ",\"mismatches\":" << a.mismatches
      << ",\"oracle_violations\":" << a.oracle_violations
      << ",\"mode_violations\":" << a.mode_violations << ",\"total_violations\":"
      << a.total_violations << ",\"trace_violations\":" << a.trace_violations
      << ",\"group_violations\":" << a.group_violations
      << ",\"fused_eq_steps\":" << a.fused_eq_steps << ",\"fused_ne_steps\":" << a.fused_ne_steps
      << "}" << std::endl;
    std::cout << o.str();
    g_out << o.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_depth_dir_read_path KIND N [TRANCOCSV]");
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
            if (!fz) throw std::runtime_error("frozen file missing: " + frozen_rel);
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        const char* ARM_NAME[2] = {"C16", "DDirT"};
        const ModelBackend ARM_BACKEND[2] = {ModelBackend::Context16,
                                             ModelBackend::EqualDepthDirTrunc};
        Config cfg[2];
        for (int a = 0; a < 2; ++a) {
            cfg[a] = e25sw::baseline_config(thr);
            e25sw::pin_frozen_switches(cfg[a]);
            cfg[a].model_backend = ARM_BACKEND[a];
            if (cfg[a].compact_region_routing)
                throw std::runtime_error("this diagnosis instruments the scanned routing only");
        }
        {
            Config x = cfg[0], y = cfg[1];
            x.model_backend = y.model_backend = ModelBackend::Context16;
            e25sw::require_only_switches_differ(x, y, {});
        }

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};
        if (qmode.locate) throw std::runtime_error("device expects mode.locate == false");

        std::vector<std::string> initial;
        std::unordered_map<std::string, std::uint64_t> initial_values;
        initial.reserve(N);
        initial_values.reserve(N);
        for (const auto& kv : keys) {
            initial.push_back(kv.first);
            initial_values.emplace(kv.first, kv.second);
        }
        const std::unordered_set<std::string> initial_set(initial.begin(), initial.end());

        const auto static_probes = make_probes(initial, 20000, 20261011, 10, initial_values);
        const std::uint64_t static_hash = hash_probes(static_probes);

        g_out.open(("results/main/depth_read_path_" + kind + ".jsonl").c_str(), std::ios::app);
        g_out.exceptions(std::ios::badbit | std::ios::failbit);

        for (int arm = 0; arm < 2; ++arm) {
            Region::reset_depth_dir_stats();

            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;
            std::unordered_map<std::string, std::uint64_t> live_values;
            live_values.reserve(N);
            for (const auto& kv : keys) {
                live.push_back(kv.first);
                live_set.insert(kv.first);
                live_values.emplace(kv.first, kv.second);
            }

            auto s = std::make_unique<Index>(cfg[arm]);
            s->bulk_load(keys);

            if (arm == 1) {
                for (std::size_t i = 0; i < s->region_count(); ++i) {
                    const Region& rg = s->region(i);
                    if (!rg.uses_depth_dir())
                        throw std::runtime_error("DDirT arm: a region is not on the directory backend");
                    if (!rg.depth_dir_truncated())
                        throw std::runtime_error("DDirT arm: a region is not the truncated directory");
                }
            }

            bool failed = false;
            const std::size_t rcount = s->region_count();

            auto run_accum = [&](const std::vector<Probe>& probes,
                                 const std::unordered_set<std::string>& oracle, bool dir_arm,
                                 std::uint64_t probe_hash, Acc& hit, Acc& miss) {
                hit.region_bytes_dist.assign(rcount, 0);
                miss.region_bytes_dist.assign(rcount, 0);
                for (const auto& p : probes) {
                    const bool in_oracle = oracle.count(p.key) != 0;
                    Acc& A = p.expect_hit ? hit : miss;
                    if (p.expect_hit != in_oracle) {
                        ++A.oracle_violations;
                        failed = true;
                    }
                    ::validation::e25::counts() = ::validation::e25::Counts{};
                    const Result r = s->find(p.key, qmode);
                    // Value copy, not a reference: the shadow locate below also ticks the
                    // counters, and the query's own navigation is what must be reported.
                    const ::validation::e25::Counts c = ::validation::e25::counts();
                    const std::size_t reg = s->locate(p.key);

                    ++A.queries;
                    A.key_bytes += p.key.size();
                    A.region_probes += c.locate_cmp_calls;
                    A.region_bytes += c.locate_cmp_bytes;
                    A.model_bytes += r.steps;
                    A.model_bytes_max = std::max<std::uint64_t>(A.model_bytes_max, r.steps);
                    A.fp_total += c.fp_hash_bytes;
                    A.fp_fused += c.fp_fused_bytes;
                    A.subblocks += r.subblocks_visited;
                    A.simd_batches += r.simd_batches;
                    A.tag_candidates += r.tag_candidates;
                    A.candidate_blocks += r.candidate_blocks;
                    A.boundary_probes += r.locate_probes;
                    A.boundary_bytes += r.locate_cmp_bytes;
                    A.exact_calls += c.cmp_calls_primary;
                    A.exact_bytes += c.cmp_bytes_primary;
                    A.ov_calls += c.cmp_calls_overflow;
                    A.ov_bytes += c.cmp_bytes_overflow;
                    A.cmp_calls_total += c.cmp_calls;
                    A.cmp_bytes_total += c.cmp_bytes;
                    A.str_primary += r.str_cmps_primary;
                    A.str_overflow += r.str_cmps_overflow;
                    A.ov_entered += r.overflow_entered;
                    A.ov_checks += r.overflow_checks;
                    if (r.steps == 0) ++A.steps_zero;
                    A.steps_dist.push_back(r.steps);
                    A.subblocks_dist.push_back(r.subblocks_visited);
                    if (reg < A.region_bytes_dist.size())
                        A.region_bytes_dist[reg] += r.steps;

                    if (r.locate_probes != 0) { ++A.mode_violations; failed = true; }
                    if (c.cmp_calls != c.cmp_calls_primary + c.cmp_calls_overflow) {
                        ++A.total_violations;
                        failed = true;
                    }
                    if (r.found != p.expect_hit ||
                        (p.expect_hit && r.value != p.expect_value)) {
                        ++A.mismatches;
                        failed = true;
                    }

                    if (!dir_arm) {
                        if (c.fp_fused_bytes == r.steps) ++A.fused_eq_steps;
                        else ++A.fused_ne_steps;
                    } else if (r.steps > 0) {
                        const Region& rg = s->region(reg);
                        equivdir::SearchTrace tr;
                        std::uint32_t g = 0, cons = 0;
                        if (!rg.diag_depth_dir_trace(p.key, tr, g, cons)) {
                            ++A.dir_not_fitted;
                        } else {
                            ++A.dir_runs;
                            A.dir_probes += tr.probes;
                            A.dir_compared += tr.compared;
                            A.dir_end_first_diff += tr.end_first_diff;
                            A.dir_end_sep_le_q += tr.end_sep_le_q;
                            A.dir_end_sep_gt_q += tr.end_sep_gt_q;
                            A.dir_sep_prefix_of_q += tr.sep_prefix_of_q;
                            A.dir_probes_dist.push_back(tr.probes);
                            if (cons != r.steps) { ++A.trace_violations; failed = true; }
                            if (g != rg.placement_group(p.key)) {
                                ++A.group_violations;
                                failed = true;
                            }
                        }
                    }
                }
            };

            Acc st_hit, st_miss;
            run_accum(static_probes, initial_set, arm == 1, static_hash, st_hit, st_miss);

            // The bench's replacement schedule to 200%: batch erase from the front of the
            // insertion order, then insert the fresh key; the checkpoint sizes only build the
            // state, the counted probes are the final (200%) ones with the bench's seed.
            constexpr double CPS[4] = {0.25, 0.50, 1.00, 2.00};
            std::size_t done = 0;
            std::uint64_t serial = 0;
            for (double frac : CPS) {
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                if (target <= done) continue;
                struct Move {
                    std::string victim, fresh;
                    std::uint64_t value;
                };
                std::vector<Move> seg;
                seg.reserve(target - done);
                for (std::size_t i = done; i < target; ++i) {
                    const std::string victim = live.front();
                    live.pop_front();
                    live_set.erase(victim);
                    live_values.erase(victim);

                    std::string base = victim;
                    const auto pos = base.find("~r");
                    if (pos != std::string::npos) base.resize(pos);
                    std::string fresh = base + "~r" + std::to_string(serial++);
                    while (!live_set.insert(fresh).second) fresh += "x";

                    const std::uint64_t value = 0x3000ULL + serial;
                    live.push_back(fresh);
                    live_values.emplace(fresh, value);
                    seg.push_back({victim, std::move(fresh), value});
                }
                for (const auto& m : seg) s->erase(m.victim);
                for (const auto& m : seg) s->insert(m.fresh, m.value);
                done = target;
            }

            std::vector<std::string> live_vec(live.begin(), live.end());
            std::sort(live_vec.begin(), live_vec.end(), ubyte_less);
            const auto post_probes = make_probes(live_vec, 20000, 20261012, 10, live_values);
            const std::uint64_t post_hash = hash_probes(post_probes);
            const std::unordered_set<std::string> post_set(live_set.begin(), live_set.end());

            Acc po_hit, po_miss;
            run_accum(post_probes, post_set, arm == 1, post_hash, po_hit, po_miss);

            const char* backend_name = e25sw::backend_name(ARM_BACKEND[arm]);
            emit(kind, N, thr, ARM_NAME[arm], backend_name, "static", "hit", rcount, static_hash,
                 st_hit);
            emit(kind, N, thr, ARM_NAME[arm], backend_name, "static", "miss", rcount, static_hash,
                 st_miss);
            emit(kind, N, thr, ARM_NAME[arm], backend_name, "post", "hit", rcount, post_hash,
                 po_hit);
            emit(kind, N, thr, ARM_NAME[arm], backend_name, "post", "miss", rcount, post_hash,
                 po_miss);

            if (failed) {
                std::cerr << "FAILED: assertions for arm " << ARM_NAME[arm] << std::endl;
                return 1;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
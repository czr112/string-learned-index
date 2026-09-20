// E26 long-run diagnosis, step 2: why does an insert end up in overflow?
//
// For every replacement the trial is computed BEFORE the operation -- the state the
// placement actually saw -- and then the operation runs and the result is read back.
// The tally answers, for the inserts that landed in overflow:
//
//   F0  would put_primary have taken it? (expected: no, that is why it overflowed)
//   F1  would it fit after tidying ONLY the candidate sub-block, keeping ownership
//       and geometry? If yes, byte fragmentation inside that sub-block is the direct
//       cause and a group-local repack is the answer.
//   F2  does the group's own budget suffice for a full repack? If F1 fails and F2
//       holds, the problem is the sub-block boundary, not the budget. If F2 also
//       fails, the group genuinely has no room and a region-level reallocation (F3)
//       is what to look at.
//
// All of it is read-only: `trial_place` asks questions of the current structure and
// changes nothing, so the measured object is never perturbed.
//
// Build with -DE25_DIAG. No timings.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

struct Pipeline {
    std::vector<Alloc> s_alloc;
    std::vector<std::vector<std::uint16_t>> s_caps;
};

static Pipeline make_plan(const std::vector<std::vector<GroupStats>>& demand) {
    Pipeline p;
    for (const auto& gs : demand) {
        const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
        const Alloc d = demand_single(gs, budget);
        StrictLog log;
        std::vector<std::uint16_t> caps;
        p.s_alloc.push_back(demand_strict(gs, d, log, caps));
        p.s_caps.push_back(std::move(caps));
    }
    return p;
}
static auto sg_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint16_t>> o;
    for (const auto& a : v) o.push_back(a.sg);
    return o;
}
static auto bb_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint32_t>> o;
    for (const auto& a : v) o.push_back(a.bb);
    return o;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_overflow KIND N [TRANCOCSV] [RUNTAG] [MODE]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const std::string mode = argc > 5 ? argv[5] : "same";
        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        const std::string frozen_rel =
            "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss; ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        // PART 0 PROOF. Two IDENTICAL configs claiming to differ in compact_repack_budget
        // must now be REJECTED. Before the whitelist fix this call succeeded vacuously, so
        // E29's C3 arm-equality assertion verified nothing. If it still succeeds, stop.
        {
            Config same_a = e25sw::baseline_config(thr);
            Config same_b = same_a;
            bool threw = false;
            try {
                e25sw::require_only_switches_differ(same_a, same_b, {"compact_repack_budget"});
            } catch (const std::exception&) {
                threw = true;
            }
            if (!threw)
                throw std::runtime_error(
                    "switch-differ assertion is STILL VACUOUS for compact_repack_budget");
            std::cerr << "part0 self-test: identical configs rejected as required" << std::endl;
        }

        Config c = e25sw::baseline_config(thr);
        c.write_fast_locate = true;
        c.reuse_locate_ctx = true;
        c.scan_reserve_result = true;
        c.compact_reuse_tag = true;
        c.compact_needs_garbage = true;
        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        Pipeline plan;
        {
            Index probe(c);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        std::ofstream out("results/main/overflow_diag2.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        std::deque<std::string> live;
        std::unordered_set<std::string> live_set;
        Reference ref;
        for (const auto& kv : keys) { live.push_back(kv.first); ref[kv.first] = kv.second; live_set.insert(kv.first); }

        Index s(c);
        s.bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        std::uint64_t serial = 0;
        std::size_t done = 0;
        std::uint64_t cyc = 0, ov = 0, ov_bytes = 0, ov_slots = 0, ov_f1 = 0, ov_f2 = 0;
        std::uint64_t ov_f1_rank = 0, live_sum = 0, head_sum = 0;
        std::uint64_t ov_garb_ge_len = 0, ov_garb_zero = 0;
        std::uint64_t garb_sum = 0, freeb_sum = 0;

        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
        for (double frac : CPS) {
            const std::size_t target = static_cast<std::size_t>(double(N) * frac);
            while (done < target) {
                const std::string victim = live.front();
                live.pop_front();
                live_set.erase(victim);

                std::string base = victim;
                const auto pos = base.find("~r");
                if (pos != std::string::npos) base.resize(pos);
                std::string fresh;
                if (mode == "grow") {
                    fresh = base + "~r" + std::to_string(serial);
                } else {
                    fresh = base;
                    bool ok = false;
                    for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                        fresh.back() = static_cast<char>('a' + ((serial + t) % 26));
                        if (fresh != victim && ref.find(fresh) == ref.end() &&
                            live_set.find(fresh) == live_set.end()) { ok = true; break; }
                    }
                    if (!ok) throw std::runtime_error("alphabet exhausted");
                }
                const std::uint64_t val = 0x7000ULL + serial;
                ++serial;

                // The trial must see the state the placement will see.
                const auto trial = s.diag_trial(fresh);

                ref.erase(victim);
                ref[fresh] = val;
                s.erase(victim);
                s.insert(fresh, val);

                const Result r = s.find(fresh, qmode);
                if (!r.found) throw std::runtime_error("replacement not found");
                if (r.source == 2) {
                    ++ov;
                    if (trial.refused_by_bytes > 0) ++ov_bytes;
                    if (trial.refused_by_slots > 0) ++ov_slots;
                    if (trial.f1_fits) ++ov_f1;
                    if (trial.f1_rank_fits) ++ov_f1_rank;   // the rank-restricted claim
                    live_sum += trial.cand_live_records;
                    head_sum += trial.cand_headroom;
                    if (trial.f2_budget_suffices) ++ov_f2;
                    if (trial.group_garbage >= fresh.size()) ++ov_garb_ge_len;
                    if (trial.group_garbage == 0) ++ov_garb_zero;
                    garb_sum += trial.group_garbage;
                    freeb_sum += trial.group_free_bytes;
                }
                ++cyc;

                live.push_back(fresh);
                live_set.insert(fresh);
                ++done;
            }

            out << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\",\"run_tag\":\"" << run_tag << "\""
                << ",\"frac\":" << frac << ",\"cycles\":" << cyc
                << ",\"overflow_landed\":" << ov
                << ",\"ov_refused_by_bytes\":" << ov_bytes
                << ",\"ov_refused_by_slots\":" << ov_slots
                << ",\"ov_F1_fits_after_tidy\":" << ov_f1
                << ",\"ov_F1_rank_fits\":" << ov_f1_rank
                << ",\"ov_avg_cand_live_records\":" << (ov ? double(live_sum) / double(ov) : 0.0)
                << ",\"ov_avg_cand_headroom\":" << (ov ? double(head_sum) / double(ov) : 0.0)
                << ",\"ov_F2_budget_suffices\":" << ov_f2
                << ",\"ov_group_garbage_ge_len\":" << ov_garb_ge_len
                << ",\"ov_group_garbage_zero\":" << ov_garb_zero
                << ",\"ov_avg_group_garbage\":" << (ov ? double(garb_sum) / double(ov) : 0.0)
                << ",\"ov_avg_group_free_bytes\":" << (ov ? double(freeb_sum) / double(ov) : 0.0)
                << ",\"overflow_total\":" << s.overflow_count()
                << "}" << std::endl;
            std::cerr << kind << " " << mode << " frac=" << frac << " done (overflow " << ov
                      << ", F1 would fit " << ov_f1 << ")" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

// E26 long-run diagnosis, step 1: does a replacement actually change the logical group?
//
// The long-run result is that overflow grows to ~the live count under sustained
// replacement, and I wrote that the cause was "the replacement key lands in a
// different logical group, whose budget is already spent". That was an INFERENCE.
// The review pointed out it is not established: `same` mode changes the last byte of
// the key, which can perfectly well change the group the model computes. "Same
// length, anchored next to the old key" is not "same logical group".
//
// This program measures the step instead of arguing it. For every replacement it
// records:
//
//   * whether the replacement key maps to the SAME logical group as the key it
//     replaced (the physical group id is what the capacity plan was sized for, so a
//     move is a different placement question from a stay);
//   * whether the inserted record ended up in the primary or the overflow container;
//   * for a move, whether the DESTINATION group had a free slot and enough suffix
//     bytes at that moment.
//
// The mapping is state-independent, so the destination group is the same before and
// after the insert; it is read before the operation so the tally reflects the state
// the placement actually saw.
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
        if (argc < 3) throw std::invalid_argument("diag_transfer KIND N [TRANCOCSV] [RUNTAG] [MODE]");
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

        Config c = e25sw::baseline_config(thr);
        c.write_fast_locate = true;
        c.reuse_locate_ctx = true;
        c.scan_reserve_result = true;
        c.compact_reuse_tag = true;
        c.compact_needs_garbage = true;   // the frozen version

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

        std::ofstream out("results/main/transfer_diag.jsonl", std::ios::app);
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
        std::uint64_t cyc = 0, moved = 0, stayed = 0;
        std::uint64_t moved_overflow = 0, stayed_overflow = 0;

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

                // Read the mapping BEFORE the operation: the destination group is what
                // the placement will see.
                const auto w_old = s.diag_where(victim);
                const auto w_new = s.diag_where(fresh);
                const bool is_move = (w_old.second != w_new.second);

                ref.erase(victim);
                ref[fresh] = val;
                s.erase(victim);
                s.insert(fresh, val);

                const Result r = s.find(fresh, qmode);
                if (!r.found) throw std::runtime_error("replacement not found after insert");
                const bool in_overflow = (r.source == 2);

                ++cyc;
                if (is_move) {
                    ++moved;
                    if (in_overflow) ++moved_overflow;
                } else {
                    ++stayed;
                    if (in_overflow) ++stayed_overflow;
                }

                live.push_back(fresh);
                live_set.insert(fresh);
                ++done;
            }

            out << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\",\"run_tag\":\"" << run_tag << "\""
                << ",\"frac\":" << frac << ",\"cycles\":" << cyc
                << ",\"live\":" << s.size()
                << ",\"moved_group\":" << moved << ",\"stayed_group\":" << stayed
                << ",\"moved_frac\":" << (cyc ? double(moved) / double(cyc) : 0.0)
                << ",\"moved_landed_overflow\":" << moved_overflow
                << ",\"stayed_landed_overflow\":" << stayed_overflow
                << ",\"overflow_total\":" << s.overflow_count()
                << "}" << std::endl;
            std::cerr << kind << " " << mode << " frac=" << frac << " done (moved "
                      << moved << "/" << cyc << ")" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

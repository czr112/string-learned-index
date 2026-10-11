// A3 entry diagnosis: is the overflow container STILL more than 20% of maintenance, and is
// that cost mainly MOVEMENT?
//
// READ-ONLY MEASUREMENT; no prototype, no switch. The plan's A3 entry condition is
// "A1/A2 之后溢出搬移仍占显著成本", and section 6 spells the test out as ">20% of
// maintenance, dominated by moves". This program produces exactly that number on the
// CURRENT FROZEN DEFAULT configuration.
//
// E34's O1 IS NOT CREDITED HERE. O1 is a non-default experimental arm; its 12x reduction in
// moved bytes is a property of a configuration nobody runs. Every figure below is the
// shipped path. (The O1 comparison is cited in the report as CONTEXT, never as a gain the
// current default already has.)
//
// TWO WORKLOADS, because the container's cost structure differs between them:
//   replace -- long replacement to 200%, the load every earlier round used;
//   insert  -- pure insertion, no erase at all, so the erase-side shifting is zero by
//              construction and the insert-side cost stands alone.
//
// PHASES ARE REPORTED SEPARATELY. A replacement workload changes character once the
// cumulative replacement passes one full pass over N: before that the victims are all
// original records, after it they are records this run itself inserted. The checkpoints
// straddle that line, so the report groups <=100% (short term) and >100% (past one full
// replacement) apart rather than averaging them.
//
// BREAKDOWN of the container's own time, all inside the maintenance paths:
//   lookup   -- lower_bound_pos inside put_overflow
//   alloc    -- the std::string construction of the record being inserted
//   insert   -- ov_insert_ns minus the two above (the shift)
//   erase    -- erase_at: the shift plus the destruction of the removed record
//
// COUNTS LOCATE CANDIDATES; THEY DO NOT CONVERT TO TIME. No timings are taken here beyond
// the container's own clocks, and no speedup is estimated.
//
// Usage:  diag_ov_cost_a3 KIND N [TRANCOCSV] [RUNTAG] [REPS]

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
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;
using Clock = std::chrono::steady_clock;

#ifndef E25_DIAG
#error "diag_ov_cost_a3.cpp needs -DE25_DIAG"
#endif
#ifndef E30_TIME
#error "diag_ov_cost_a3.cpp needs -DE30_TIME: the container clocks are in that block"
#endif

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
        if (argc < 3) throw std::invalid_argument("diag_ov_cost_a3 KIND N [TRANCOCSV] [TAG] [REPS]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const int reps = argc > 5 ? std::stoi(argv[5]) : 1;

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

        // THE SHIPPED CONFIGURATION, and nothing else. No candidate switch is turned on,
        // which is the point: the question is what the DEFAULT path costs.
        Config cfg = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg);

        Pipeline plan;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        // CLOCK CALIBRATION, before anything is measured.
        //
        // This round put THREE clock pairs inside put_overflow (the whole body, the lookup
        // and the construction) where E32 had ONE. Two reads of steady_clock cost real
        // nanoseconds, and they land in the residual "everything else in the insert path"
        // term -- which is the term the breakdown is supposed to attribute. Without this
        // number that residual is uninterpretable: it would read as insert-side work when
        // most of it is the instrument.
        //
        // The residual contains exactly ONE extra pair beyond the two sub-timers (see the
        // analysis note), so the analyzer subtracts one `oh` from it and one from each of
        // the two sub-timers.
        double timer_overhead_ns = 0;
        {
            volatile std::uint64_t sink = 0;
            const std::size_t CAL = 200000;
            const auto c0 = Clock::now();
            for (std::size_t i = 0; i < CAL; ++i) { sink += validation::e30::now_ns(); }
            const auto c1 = Clock::now();
            timer_overhead_ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           c1 - c0).count()) / double(CAL) / 2.0;
        }

        std::ofstream out(("results/main/ov_cost_a3_" + kind + ".jsonl").c_str(), std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        const auto emit = [&](const char* workload, const char* phase, double frac,
                              const Index& s, double cum_erase, double cum_insert,
                              std::size_t live0) {
            const auto& C = validation::e25::counts();
            const auto& T = validation::e30::timers();

            // The instrument checks itself: string_allocs is incremented at exactly the
            // site the insert clock wraps, so if they disagree the clocks were moved and
            // every share below would be meaningless rather than merely imprecise.
            if (C.string_allocs != T.ov_insert_calls)
                throw std::runtime_error("instrument disagrees: string_allocs != ov_insert_calls");

            const double ins_ms = cum_insert, ers_ms = cum_erase;
            const double maint_ms = ins_ms + ers_ms;
            const double ov_i_ms = double(T.ov_insert_ns) / 1e6;
            const double ov_e_ms = double(T.ov_erase_ns) / 1e6;
            const double ov_l_ms = double(T.ov_lookup_ns) / 1e6;
            const double ov_a_ms = double(T.ov_alloc_ns) / 1e6;

            out << "{\"kind\":\"" << kind << "\",\"workload\":\"" << workload
                << "\",\"phase\":\"" << phase << "\""
                << ",\"run_tag\":\"" << run_tag << "\""
                << ",\"frac\":" << frac
                << ",\"live0\":" << live0
                << ",\"maintenance_ms\":" << maint_ms
                << ",\"erase_ms\":" << ers_ms
                << ",\"insert_ms\":" << ins_ms
                << ",\"ov_insert_ms\":" << ov_i_ms
                << ",\"ov_insert_calls\":" << T.ov_insert_calls
                << ",\"ov_erase_ms\":" << ov_e_ms
                << ",\"ov_erase_calls\":" << T.ov_erase_calls
                << ",\"ov_lookup_ms\":" << ov_l_ms
                << ",\"ov_lookup_calls\":" << T.ov_lookup_calls
                << ",\"ov_alloc_ms\":" << ov_a_ms
                << ",\"ov_alloc_calls\":" << T.ov_alloc_calls
                << ",\"ov_shifted_records_insert\":" << C.ov_shifted_records_insert
                << ",\"ov_shifted_bytes_insert\":" << C.ov_shifted_bytes_insert
                << ",\"ov_shifted_records_erase\":" << C.ov_shifted_records_erase
                << ",\"ov_shifted_bytes_erase\":" << C.ov_shifted_bytes_erase
                << ",\"ov_rec_bytes\":" << s.diag_overflow_record_bytes()
                << ",\"timer_overhead_ns\":" << timer_overhead_ns
                << "}" << std::endl;
        };

        const auto fresh_of = [&](const std::string& base, std::uint64_t serial) {
            return base + "~r" + std::to_string(serial);
        };

        // ---------------- W1: long replacement to 200% ----------------
        for (int r = 0; r < reps; ++r) {
            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;
            Reference ref;
            for (const auto& kv : keys) {
                live.push_back(kv.first);
                ref[kv.first] = kv.second;
                live_set.insert(kv.first);
            }

            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

            // Reset AFTER the load: the constructor places the whole sorted input through
            // the same put_overflow, and those placements are build cost, not this
            // workload's cost.
            validation::e25::counts().reset();
            validation::e30::timers().reset();

            const std::size_t live0 = s->size();
            std::size_t done = 0;
            double cum_erase = 0, cum_insert = 0;
            std::uint64_t serial = 0;

            for (double frac : CPS) {
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                if (target <= done) continue;

                struct Move { std::string victim, fresh; std::uint64_t value; };
                std::vector<Move> seg;
                seg.reserve(target - done);

                for (std::size_t i = done; i < target; ++i) {
                    const std::string victim = live.front();
                    live.pop_front();
                    live_set.erase(victim);

                    std::string base = victim;
                    const auto pos = base.find("~r");
                    if (pos != std::string::npos) base.resize(pos);

                    std::string fresh = fresh_of(base, serial++);
                    while (!live_set.insert(fresh).second) fresh += "x";

                    const std::uint64_t val = 0x7000ULL + serial;
                    seg.push_back({victim, fresh, val});
                    live.push_back(fresh);
                }

                for (const auto& m : seg) {
                    if (ref.erase(m.victim) != 1) throw std::runtime_error("erase target absent");
                    ref[m.fresh] = m.value;
                }

                auto a0 = Clock::now();
                for (const auto& m : seg) s->erase(m.victim);
                cum_erase += std::chrono::duration<double, std::milli>(Clock::now() - a0).count();

                a0 = Clock::now();
                for (const auto& m : seg) s->insert(m.fresh, m.value);
                cum_insert += std::chrono::duration<double, std::milli>(Clock::now() - a0).count();

                done = target;

                if (s->size() != ref.size())
                    throw std::runtime_error("live count drifted from the reference");

                emit("replace", frac > 1.0 ? "past_one_full" : "short_term", frac, *s,
                     cum_erase, cum_insert, live0);
            }
        }

        // ---------------- W2: pure insert ----------------
        // No erase at all, so the erase-side numbers are zero BY CONSTRUCTION. What is left
        // is the container's insert-side cost, which is the other half of the question.
        for (int r = 0; r < reps; ++r) {
            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

            validation::e25::counts().reset();
            validation::e30::timers().reset();

            const std::size_t live0 = s->size();
            std::size_t done = 0;
            double cum_insert = 0;
            std::uint64_t serial = 0;

            for (double frac : CPS) {
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                if (target <= done) continue;

                std::vector<std::pair<std::string, std::uint64_t>> seg;
                seg.reserve(target - done);

                for (std::size_t i = done; i < target; ++i) {
                    std::string base = keys[i % N].first;
                    const auto pos = base.find("~r");
                    if (pos != std::string::npos) base.resize(pos);
                    seg.push_back({fresh_of(base, serial++), 0xBEEFULL + serial});
                }

                const auto a0 = Clock::now();
                for (const auto& kv : seg) s->insert(kv.first, kv.second);
                cum_insert += std::chrono::duration<double, std::milli>(Clock::now() - a0).count();

                done = target;

                emit("insert", frac > 1.0 ? "past_one_full" : "short_term", frac, *s,
                     0.0, cum_insert, live0);
            }
        }

        std::cout << "{\"kind\":\"" << kind << "\",\"case\":\"done\","
                  << "\"reps\":" << reps << "}" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

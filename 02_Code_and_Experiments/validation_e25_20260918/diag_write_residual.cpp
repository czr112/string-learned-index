// Stage D entry diagnosis (2026-10-10): under W (subblock_reclaim + reclaim_widest), the
// replacement workload's maintenance time is roughly a third of the shipped B arm -- but
// it is not zero, and the round's question is WHERE the remaining time goes and whether
// there is still REPEATED or INVALID work that one bounded structural change could remove.
//
// This program does NOT compare arms and does NOT price a fix. It decomposes the write
// path of ONE fixed configuration (frozen set + W) on the combo round's own replacement
// schedule, with two boundaries the previous write-path entry (diag_write_cost) did not
// have:
//
//   1. PER-CYCLE SIDE ATTRIBUTION. The combo schedule interleaves erase(victim) with
//      insert(fresh) -- batching the erases first is what once made the widening branch
//      unreachable, so the shape is fixed. Every counter and every E30 clock is therefore
//      sampled around each individual call, and added to the ERASE side or the INSERT
//      side of the current interval. No counter is left as "write path, both".
//   2. INTERVAL INCREMENT + LOCAL WINDOW. Each checkpoint reports two denominators: the
//      whole interval since the previous checkpoint, and the last 8192 replacement cycles
//      before the checkpoint (the "local window"). The window is what separates the early
//      conversion cost from the residual cost near the checkpoint; the two must never be
//      pooled (their cycle counts differ, and so do their states).
//
// TWO BUILDS, ONE SOURCE, and they are deliberately exclusive:
//   * counting:  -DE25_DIAG -DE18_COUNT  (no E30 clocks at all)
//   * timing:    -DE30_TIME -DE30_WRITE  (no E25_DIAG: its byte-failure bookkeeping --
//                 diag_note_byte_failure -- runs INSIDE insert_into and would be charged
//                 to the clocked spans; a mixed build would price the instrument.)
// A build with both or neither refuses to compile.
//
// WHAT THIS IS NOT. No arm is compared; no speedup is claimed; the driver walls are
// printed but they include the snapshot copies and are labelled as instrumented. The
// timing build reports RAW phase values beside the empty-timer calibration, and the
// analyzer corrects conservatively (calls * calib) -- a phase near the floor is "not
// resolvable", not "free".
//
// WORKLOAD (frozen): same@131072/mixed and grow@262144/tranco, both backends, checkpoints
// 25% and 200% of N replacement cycles, schedule generated exactly as bench_combo does
// (live-key ring, serial-numbered fresh keys, per-segment reference updates). The plan
// sets 1 process x 4 rebuild replays per cell; the four replays check the COUNTS and the
// TIME ORDER OF MAGNITUDE, they are not an independent-process A/B.
//
// Usage:
//   diag_write_residual KIND N REPS TRANCOCSV RUNTAG MODE BACKEND OUTPATH [FRACS]
//     MODE    = same | grow          BACKEND = ctx16 | lits
//     OUTPATH = results/<round>/<name>.jsonl; must NOT exist; its parent must exist
//     FRACS   = comma list, default "0.25,2.00"
//
// Rows: one header row (t=hdr) then one cell row (t=cell) per (rep, checkpoint).

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef __linux__
#include <sched.h>
#endif

// ---------------------------------------------------------------- mode selection
#if defined(E25_DIAG) && defined(E18_COUNT)
#define DR_COUNT_BUILD 1
#endif
#if defined(E30_TIME) && defined(E30_WRITE)
#define DR_TIME_BUILD 1
#endif

#if defined(DR_COUNT_BUILD) && defined(DR_TIME_BUILD)
#error "diag_write_residual: BOTH diagnostic modes are on. The counting build must not carry the E30 clocks, and the timing build must not carry E25_DIAG's in-path bookkeeping (it runs inside insert_into). Build the two targets separately."
#endif
#if defined(E25_DIAG) && !defined(E18_COUNT)
#error "diag_write_residual counting build needs BOTH -DE25_DIAG and -DE18_COUNT: subblock probes, insert attempts/ok, entry shifts and suffix bytes live in the E18 block, and with it off those columns would read as zeros."
#endif
#if !defined(DR_COUNT_BUILD) && !defined(DR_TIME_BUILD)
#error "diag_write_residual needs either (-DE25_DIAG -DE18_COUNT) [counting] or (-DE30_TIME -DE30_WRITE) [timing]."
#endif

#ifdef DR_COUNT_BUILD
static const char* kBuildTag = "count";
#else
static const char* kBuildTag = "time";
#endif

using namespace validation;
using namespace validation::e9alloc;

using Clock = std::chrono::steady_clock;
using Reference = std::map<std::string, std::uint64_t, Less>;

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ---------------------------------------------------------------- build plan (same as
// bench_combo / diag_write_cost: the capacity table is an INPUT, made once with the B
// configuration and reused for every run -- the schedule and the layout of the capacity
// table must not move between this round and the combo round it diagnoses).
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

// ---------------------------------------------------------------- the field tables
// Every name here must exist in the struct it names; the compiler is the check.
#define DR_E25_FIELDS(F) \
    F(locate_calls) F(predict_calls) F(fp_of_calls) F(fp_hash_bytes) \
    F(existence_searches) F(cmp_calls) F(cmp_bytes) \
    F(place_attempts) F(place_fail_slots) F(place_fail_bytes) \
    F(compactions) F(region_rebuilds) \
    F(erase_shifts) F(erase_released_primary) F(erase_released_overflow) \
    F(string_allocs) F(ov_suffix_bytes_written) F(string_frees) \
    F(compact_entered) F(compact_helped) F(compact_no_help) \
    F(compact_records_replayed) F(compact_bytes_copied) \
    F(compact_garbage_before) F(compact_tag_mismatch) F(compact_plan_violation) \
    F(compact_plan_rejected) F(notlocated_fallback) \
    F(subblock_reclaims) F(subblock_reclaim_helped) \
    F(reclaim_widened) F(reclaim_widened_helped) F(reclaim_widened_blocks_examined) \
    F(migrate_calls) F(migrated_records) F(migrated_bytes) F(migrate_helped) \
    F(migrate_rejected) F(reclaim_skipped_unchanged) F(reclaim_skipped_gap) \
    F(fail_bytes_hole_fits) F(fail_bytes_hole_absent) F(fail_bytes_repack_fits) \
    F(fail_bytes_len_sum) F(fail_bytes_best_hole_sum) F(fail_bytes_garbage_sum) \
    F(fail_bytes_live_sum) F(hole_sweep_checks) F(hole_sweep_mismatch) \
    F(hole_events_recorded) F(hole_events_dropped) \
    F(low_activity_reclaimed) F(low_activity_n0) F(low_activity_n1) \
    F(low_activity_bytes) F(low_activity_helped) \
    F(hole_nholes0) F(hole_nholes1) F(hole_nholes2plus) F(hole_best_eq_garbage) \
    F(hole_count_lt2) F(hole_count_eq0) F(hole_count_eq1) F(hole_slots_not_free) \
    F(fail_bytes_repack_guarded) \
    F(rp_calls) F(rp_records) F(rp_bytes) F(rp_moved_records) F(rp_moved_bytes) \
    F(rp_selfcopy_records) F(rp_selfcopy_bytes) \
    F(prefix_rebuilds) F(threshold_splits) \
    F(scan_regions) F(scan_groups) F(scan_groups_empty) F(scan_blocks_skipped) \
    F(scan_merge_cmps) F(scan_result_allocs) \
    F(ov_shifted_records_insert) F(ov_shifted_bytes_insert) \
    F(ov_shifted_records_erase) F(ov_shifted_bytes_erase) \
    F(overflow_record_moves_stable) F(overflow_record_move_bytes_stable) \
    F(overflow_handle_moves) F(overflow_handle_move_bytes) \
    F(ov_store_high_water) F(ov_store_live_water) \
    F(ov_new_peak_events) F(ov_new_peak_overshoot) F(ov_new_peak_copy_bytes) \
    F(ov_cap_change_events) F(ov_cap_change_copy_bytes) \
    F(ov_queries_bin) F(ov_queries_linear) F(ov_bin_cmps) F(ov_bin_decided4) \
    F(ov_bin_decided8) F(ov_bin_short4) F(ov_bin_lcp_bytes) F(ov_linear_recs) \
    F(ov_linear_tag_reject) F(ov_linear_tag_pass) F(ov_linear_reject8) \
    F(ov_linear_lcp_bytes) F(ov_tag_entries) F(ov_tag_reject_pre) \
    F(scan_start_locates) F(scan_canonical_group) F(scan_cursor_starts) \
    F(scan_ov_cursor_starts) F(scan_adv_p_cmps) F(scan_adv_p_skipped) \
    F(scan_adv_p_subblocks) F(scan_adv_ov_cmps) F(scan_adv_ov_skipped) \
    F(scan_out_primary) F(scan_out_overflow) F(scan_out_bytes) \
    F(put_primary_calls) F(fc_calls) F(fc_probes) F(fc_gap) \
    F(hint_eq) F(hint_ne) F(hint_nodir) F(hint_below)

#define DR_E18_FIELDS(F) \
    F(group_calls) F(fingerprint_calls) F(region_ctors) F(subblock_probes) \
    F(insert_attempts) F(insert_ok) F(entry_shifts) F(suffix_bytes_copied) \
    F(overflow_inserts) F(overflow_containers)

#define DR_T_FIELDS(F) \
    F(compact_ns) F(compact_calls) F(collect_ns) F(replay_ns) \
    F(erase_ns) F(erase_calls) F(insert_ns) F(insert_calls) \
    F(ov_insert_ns) F(ov_insert_calls) F(ov_erase_ns) F(ov_erase_calls) \
    F(ov_lookup_ns) F(ov_lookup_calls) F(ov_alloc_ns) F(ov_alloc_calls) \
    F(w_insert_ns) F(w_insert_calls) F(w_erase_ns) F(w_erase_calls) \
    F(w_locate_ns) F(w_locate_calls) F(w_locate_ins_ns) F(w_locate_ins_calls) \
    F(w_locate_era_ns) F(w_locate_era_calls) F(w_place_ns) F(w_place_calls) \
    F(w_rebuild_ns) F(w_rebuild_calls) F(w_rebuild_all_ns) F(w_rebuild_all_calls) \
    F(w_rebuild_all_records) F(w_reclaim_ns) F(w_reclaim_calls) \
    F(w_rp_sort_ns) F(w_rp_sort_calls) F(w_rp_move_ns) F(w_rp_move_calls) \
    F(w_findloc_ns) F(w_findloc_calls) \
    F(w_walk_ns) F(w_walk_calls) F(w_fc_ns) F(w_fc_calls)

// ---------------------------------------------------------------- snapshots
#ifdef DR_COUNT_BUILD
struct Snap {
    e25::Counts c{};
    e18::Counts e{};
};
static Snap take() {
    Snap s;
    s.c = e25::counts();
    s.e = e18::counts();
    return s;
}
static void add_delta(Snap& acc, const Snap& hi, const Snap& lo) {
#define DR_F(n) acc.c.n += hi.c.n - lo.c.n;
    DR_E25_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) acc.e.n += hi.e.n - lo.e.n;
    DR_E18_FIELDS(DR_F)
#undef DR_F
}
// The high side is the LIVE counter state, so the post-call delta does not pay a third
// copy per cycle.
static void add_delta_live(Snap& acc, const Snap& lo) {
#define DR_F(n) acc.c.n += e25::counts().n - lo.c.n;
    DR_E25_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) acc.e.n += e18::counts().n - lo.e.n;
    DR_E18_FIELDS(DR_F)
#undef DR_F
}
static void reset_counters() {
    e25::counts().reset();
    e18::counts().reset();
}
#else  // DR_TIME_BUILD
struct Snap {
    e30::Timers t{};
};
static Snap take() {
    Snap s;
    s.t = e30::timers();
    return s;
}
static void add_delta(Snap& acc, const Snap& hi, const Snap& lo) {
#define DR_F(n) acc.t.n += hi.t.n - lo.t.n;
    DR_T_FIELDS(DR_F)
#undef DR_F
}
static void add_delta_live(Snap& acc, const Snap& lo) {
#define DR_F(n) acc.t.n += e30::timers().n - lo.t.n;
    DR_T_FIELDS(DR_F)
#undef DR_F
}
static void reset_counters() {
    e30::timers().reset();
}

// One empty timer pair, timed from a different clock. Printed and subtracted by the
// analyzer; the correction is CONSERVATIVE because a tight loop is the cheapest possible
// place to read the clock, so a real call site pays no less.
static double calib_ns() {
    const int K = 200000;
    std::uint64_t acc = 0;
    const auto a = Clock::now();
    for (int i = 0; i < K; ++i) {
        const std::uint64_t t0 = E30_T0();
        acc += E30_T0() - t0;
    }
    const double ms = ms_between(a, Clock::now());
    if (acc == 0) return 0.0;   // clock compiled out; the guard keeps the loop honest
    return ms * 1e6 / double(K);
}
#endif

// ---------------------------------------------------------------- checkpoint state
struct State {
    std::uint64_t live = 0, regions = 0, overflow = 0, compactions = 0;
    std::uint64_t blocks = 0, model = 0, prefix = 0, overflow_bytes = 0, tables = 0;
    std::uint64_t directory = 0, regions_self = 0, fences = 0, self = 0, index_bytes = 0;
};
static State state_of(const Index& s) {
    State st;
    st.live = s.size();
    st.regions = s.region_count();
    st.overflow = s.overflow_count();
    st.compactions = s.compaction_count();
    const auto mb = s.memory_breakdown();
    st.blocks = mb.blocks;
    st.model = mb.model;
    st.prefix = mb.prefix;
    st.overflow_bytes = mb.overflow;
    st.tables = mb.tables;
    st.directory = mb.directory;
    st.regions_self = mb.regions_self;
    st.fences = mb.fences;
    st.self = mb.self;
    st.index_bytes = mb.blocks + mb.model + mb.prefix + mb.overflow + mb.tables +
                     mb.directory + mb.regions_self + mb.fences + mb.self;
    return st;
}

// ---------------------------------------------------------------- hashing (identity of
// the inputs and the schedule; both fold the VALUE too, so a value drift cannot pass)
static std::uint64_t fnv_bytes(std::uint64_t h, const void* p, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}
static std::uint64_t fnv_u64(std::uint64_t h, std::uint64_t v) {
    return fnv_bytes(h, &v, sizeof(v));
}
static std::uint64_t fnv_str(std::uint64_t h, const std::string& s) {
    const std::uint64_t n = s.size();
    h = fnv_u64(h, n);
    return fnv_bytes(h, s.data(), s.size());
}

// ---------------------------------------------------------------- row emission
static void emit_header(std::ostream& out, const std::string& kind, const std::string& mode,
                        const std::string& backend, std::size_t n, std::uint32_t thr,
                        int reps, const std::string& run_tag, const std::string& fracs,
                        std::size_t window, const std::string& sw, const char* clock_mode,
                        double calib, std::uint64_t key_hash, std::uint64_t sched_hash,
                        int cpu) {
    out << "{\"t\":\"hdr\",\"kind\":\"" << kind << "\",\"mode\":\"" << mode
        << "\",\"backend\":\"" << backend << "\",\"n\":" << n << ",\"thr\":" << thr
        << ",\"reps\":" << reps << ",\"run_tag\":\"" << run_tag
        << "\",\"workload\":\"replace\",\"fracs\":\"" << fracs
        << "\",\"window_cycles\":" << window
        << ",\"clock\":\"" << clock_mode << "\",\"calib_ns\":" << calib
        << ",\"src\":\"diag_write_residual\",\"rp_split\":1,\"key_hash\":" << key_hash
        << ",\"sched_hash\":" << sched_hash << ",\"build\":\"" << kBuildTag << "\",\"cpu\":" << cpu
        << ",\"switches\":\"" << sw << "\"}" << std::endl;
}

static void emit_state(std::ostream& out, const State& st) {
    out << ",\"st_live\":" << st.live << ",\"st_regions\":" << st.regions
        << ",\"st_overflow\":" << st.overflow << ",\"st_compactions\":" << st.compactions
        << ",\"st_blocks\":" << st.blocks << ",\"st_model\":" << st.model
        << ",\"st_prefix\":" << st.prefix << ",\"st_overflow_bytes\":" << st.overflow_bytes
        << ",\"st_tables\":" << st.tables << ",\"st_directory\":" << st.directory
        << ",\"st_regions_self\":" << st.regions_self << ",\"st_fences\":" << st.fences
        << ",\"st_self\":" << st.self << ",\"st_index_bytes\":" << st.index_bytes;
}

static void emit_cell(std::ostream& out, const std::string& kind, const std::string& mode,
                      const std::string& backend, int rep, double frac,
                      std::size_t from_cycle, std::size_t to_cycle,
                      std::size_t win_from, const State& st,
                      const Snap& int_e, const Snap& int_i,
                      const Snap& win_e, const Snap& win_i,
                      std::uint64_t drv_seg_ns, std::uint64_t drv_win_ns) {
    out << "{\"t\":\"cell\",\"kind\":\"" << kind << "\",\"mode\":\"" << mode
        << "\",\"backend\":\"" << backend << "\",\"rep\":" << rep << ",\"build\":\"" << kBuildTag << "\""
        << ",\"frac\":" << frac
        << ",\"from_cycle\":" << from_cycle << ",\"to_cycle\":" << to_cycle
        << ",\"cycles\":" << (to_cycle - from_cycle)
        << ",\"win_from_cycle\":" << win_from
        << ",\"win_cycles\":" << (to_cycle - win_from)
        << ",\"drv_seg_ns\":" << drv_seg_ns << ",\"drv_win_ns\":" << drv_win_ns
        << ",\"drv_wall_instrumented\":true";
    emit_state(out, st);
#ifdef DR_COUNT_BUILD
#define DR_F(n) out << ",\"int_e_" #n "\":" << int_e.c.n;
    DR_E25_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"int_i_" #n "\":" << int_i.c.n;
    DR_E25_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"win_e_" #n "\":" << win_e.c.n;
    DR_E25_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"win_i_" #n "\":" << win_i.c.n;
    DR_E25_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"int_e_" #n "\":" << int_e.e.n;
    DR_E18_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"int_i_" #n "\":" << int_i.e.n;
    DR_E18_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"win_e_" #n "\":" << win_e.e.n;
    DR_E18_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"win_i_" #n "\":" << win_i.e.n;
    DR_E18_FIELDS(DR_F)
#undef DR_F
#else
#define DR_F(n) out << ",\"int_e_" #n "\":" << int_e.t.n;
    DR_T_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"int_i_" #n "\":" << int_i.t.n;
    DR_T_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"win_e_" #n "\":" << win_e.t.n;
    DR_T_FIELDS(DR_F)
#undef DR_F
#define DR_F(n) out << ",\"win_i_" #n "\":" << win_i.t.n;
    DR_T_FIELDS(DR_F)
#undef DR_F
#endif
    out << "}" << std::endl;
}

static std::uint64_t dr_now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
            .count());
}

// ---------------------------------------------------------------- main
int main(int argc, char** argv) {
    try {
        if (argc < 9)
            throw std::invalid_argument(
                "diag_write_residual KIND N REPS TRANCOCSV RUNTAG MODE BACKEND OUTPATH [FRACS]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argv[4];
        const std::string run_tag = argv[5];
        const std::string mode = argv[6];
        const std::string backend_arg = argv[7];
        const std::string outpath = argv[8];
        const std::string fracs_arg = argc > 9 ? argv[9] : "0.25,2.00";

        if (mode != "same" && mode != "grow")
            throw std::invalid_argument("MODE must be same or grow");
        if (backend_arg != "ctx16" && backend_arg != "lits")
            throw std::invalid_argument("BACKEND must be ctx16 or lits");
        if (reps <= 0) throw std::invalid_argument("REPS must be positive");

        std::vector<double> fracs;
        {
            std::stringstream ss(fracs_arg);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (tok.empty()) continue;
                const double f = std::stod(tok);
                if (!(f > 0.0) || f > 10.0)
                    throw std::invalid_argument("FRACS entries must be in (0, 10]");
                fracs.push_back(f);
            }
            if (fracs.empty()) throw std::invalid_argument("FRACS is empty");
            for (std::size_t i = 1; i < fracs.size(); ++i)
                if (!(fracs[i] > fracs[i - 1]))
                    throw std::invalid_argument("FRACS must be strictly increasing");
        }

        // The output must not exist. This is the program's own half of the round's
        // "refuse to overwrite" rule; the driver refuses too, deliberately twice.
        {
            std::ifstream probe(outpath);
            if (probe.good()) throw std::runtime_error("output already exists: " + outpath);
        }

        const bool grow = (mode == "grow");
        const ModelBackend backend =
            backend_arg == "lits" ? ModelBackend::LitsHpt : ModelBackend::Context16;
        const char* backend_name = backend == ModelBackend::LitsHpt ? "LitsHpt" : "Context16";

        // Affinity: the TIMING build's comparable numbers need the pin, so a failed pin
        // there is a hard error. The counting build measures counts, which do not depend
        // on where the process ran, so it proceeds unpinned and records the CPU it saw.
        int cpu = -1;
#ifdef __linux__
        {
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(0, &set);
            const int rc = sched_setaffinity(0, sizeof(set), &set);
            cpu = sched_getcpu();
#ifdef DR_TIME_BUILD
            if (rc != 0 || cpu != 0)
                throw std::runtime_error(
                    "affinity pin to CPU 0 failed; refusing to emit comparable timings");
#else
            if (rc != 0)
                std::cerr << "affinity pin failed; continuing (counting build: counts do not "
                             "depend on it)\n";
#endif
        }
#endif

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();
        if (N == 0) throw std::runtime_error("empty key set");

        const std::string frozen_rel =
            "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        // B is the reference the plan's grid names; W is B + the two WIDE switches, and
        // the difference is asserted rather than assumed.
        Config cfgB = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfgB);
        Config cfgW = cfgB;
        cfgW.subblock_reclaim = true;
        cfgW.reclaim_widest = true;
        cfgB.model_backend = backend;
        cfgW.model_backend = backend;
        e25sw::require_only_switches_differ(cfgB, cfgW, {"subblock_reclaim", "reclaim_widest"});
#ifdef DR_WRITE_LOCATE
        // Stage D probe (not a candidate): the write path's SEARCH with the boundary
        // directory on. The count build cannot see the search's per-block loop, so the
        // only way to price it before defining a candidate is to time it.
        cfgW.write_locate = true;
        std::cerr << "PROBE DR_WRITE_LOCATE: cfgW.write_locate = true\n";
#endif
        std::cerr << "diag_write_residual switches " << e25sw::switches(cfgW) << std::endl;

        // One plan, made with B, reused -- exactly as the combo round did.
        Pipeline plan;
        {
            Index probe(cfgB);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }
        const auto sg = sg_of(plan.s_alloc);
        const auto bb = bb_of(plan.s_alloc);

        // ---------------------------------------------------------- the schedule
        struct Move {
            std::string victim, fresh;
            std::uint64_t value;
        };
        struct Cell {
            double frac;
            std::size_t from, to, win_from;
        };

        std::vector<Move> moves;
        moves.reserve(std::size_t(double(N) * fracs.back()) + 1);
        std::vector<Cell> cells;
        std::uint64_t sched_hash = 1469598103934665603ULL;

        {
            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;
            Reference ref;
            for (const auto& kv : keys) {
                live.push_back(kv.first);
                live_set.insert(kv.first);
                ref[kv.first] = kv.second;
            }

            std::uint64_t serial = 0;
            std::size_t done = 0;
            for (double frac : fracs) {
                Cell cell;
                cell.frac = frac;
                cell.from = done;
                const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                if (target <= done)
                    throw std::invalid_argument("FRACS out of order or zero-length cell");
                cell.to = target;

                for (std::size_t i = done; i < target; ++i) {
                    const std::string victim = live.front();
                    live.pop_front();
                    live_set.erase(victim);

                    std::string base = victim;
                    const auto pos = base.find("~r");
                    if (pos != std::string::npos) base.resize(pos);

                    std::string fresh;
                    if (grow) {
                        fresh = base + "~r" + std::to_string(serial);
                    } else {
                        fresh = base;
                        bool ok = false;
                        for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                            fresh.back() = static_cast<char>('a' + ((serial + t) % 26));
                            if (fresh != victim && ref.find(fresh) == ref.end() &&
                                live_set.find(fresh) == live_set.end()) {
                                ok = true;
                                break;
                            }
                        }
                        if (!ok)
                            throw std::runtime_error(
                                "same-length replacement exhausted its alphabet");
                    }

                    const std::uint64_t val = 0x7000ULL + serial;
                    ++serial;
                    moves.push_back({victim, fresh, val});
                    sched_hash = fnv_str(sched_hash, victim);
                    sched_hash = fnv_str(sched_hash, fresh);
                    sched_hash = fnv_u64(sched_hash, val);
                    live.push_back(fresh);
                    live_set.insert(fresh);
                }

                for (std::size_t i = cell.from; i < cell.to; ++i) {
                    const Move& m = moves[i];
                    if (ref.erase(m.victim) != 1)
                        throw std::runtime_error("erase target absent");
                    if (ref.find(m.fresh) != ref.end())
                        throw std::runtime_error("insert target present");
                    ref[m.fresh] = m.value;
                }

                const std::size_t window = 8192;
                cell.win_from = (cell.to - cell.from > window) ? cell.to - window : cell.from;
                cells.push_back(cell);
                done = target;
            }
        }

        std::uint64_t key_hash = 1469598103934665603ULL;
        for (const auto& kv : keys) {
            key_hash = fnv_str(key_hash, kv.first);
            key_hash = fnv_u64(key_hash, kv.second);
        }

        double calib = 0.0;
#ifdef DR_TIME_BUILD
        calib = calib_ns();
        std::cerr << "clock calibration: " << calib << " ns per timer pair\n";
#endif

        std::ofstream out(outpath.c_str());
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);
#ifdef DR_COUNT_BUILD
        emit_header(out, kind, mode, backend_name, N, thr, reps, run_tag, fracs_arg, 8192,
                    e25sw::switches(cfgW), "off", 0.0, key_hash, sched_hash, cpu);
#else
        emit_header(out, kind, mode, backend_name, N, thr, reps, run_tag, fracs_arg, 8192,
                    e25sw::switches(cfgW), "on", calib, key_hash, sched_hash, cpu);
#endif

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr,
                         false};

        for (int r = 0; r < reps; ++r) {
            // The reference is rebuilt per replay: the checks at a checkpoint must be
            // against the state AT THAT CHECKPOINT, not the schedule's end state.
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;

            auto s = std::make_unique<Index>(cfgW);
            reset_counters();
            s->bulk_load_with_plan(keys, sg, bb, plan.s_caps);
            reset_counters();   // the load's own work is not this workload's

            for (const Cell& cell : cells) {
                Snap int_e{}, int_i{}, win_e{}, win_i{};

                const std::uint64_t t_seg0 = dr_now_ns();
                std::uint64_t t_win0 = 0;
                bool in_window = false;

                for (std::size_t i = cell.from; i < cell.to; ++i) {
                    if (!in_window && i == cell.win_from) {
                        in_window = true;
                        t_win0 = dr_now_ns();
                    }
                    const Move& m = moves[i];

                    const Snap a = take();
                    const bool ok_e = s->erase(m.victim);
                    const Snap b = take();
                    if (!ok_e) throw std::runtime_error("index refused an existing key");
                    const bool ok_i = s->insert(m.fresh, m.value);
                    add_delta_live(int_i, b);
                    if (!ok_i) throw std::runtime_error("index refused a fresh key");
                    add_delta(int_e, b, a);
                    if (in_window) {
                        add_delta(win_e, b, a);
                        add_delta_live(win_i, b);
                    }
                }
                const std::uint64_t t_end = dr_now_ns();
                const std::uint64_t drv_seg_ns = t_end - t_seg0;
                const std::uint64_t drv_win_ns = in_window ? t_end - t_win0 : 0;

                for (std::size_t i = cell.from; i < cell.to; ++i) {
                    const Move& m = moves[i];
                    if (ref.erase(m.victim) != 1)
                        throw std::runtime_error("reference drifted: victim absent");
                    ref[m.fresh] = m.value;
                }

                if (s->size() != ref.size())
                    throw std::runtime_error("live count drifted from the reference");
                {
                    const std::size_t span = cell.to - cell.from;
                    for (int k = 0; k < 64; ++k) {
                        const std::size_t i = cell.from + (std::size_t(k) * span) / 64;
                        const Move& m = moves[i];
                        std::string_view both_keys[2] = {m.victim, m.fresh};
                        for (const std::string_view key : both_keys) {
                            const Result res = s->find(key, qmode);
                            const auto it = ref.find(std::string(key));
                            const bool want = it != ref.end();
                            if (res.found != want)
                                throw std::runtime_error("spot check: membership mismatch");
                            if (res.found && res.value != it->second)
                                throw std::runtime_error("spot check: value mismatch");
                        }
                    }
                }

                const State st = state_of(*s);
                emit_cell(out, kind, mode, backend_name, r, cell.frac, cell.from, cell.to,
                          cell.win_from, st, int_e, int_i, win_e, win_i, drv_seg_ns,
                          drv_win_ns);
            }

            // End-of-replay content check: one ordered scan against the reference map.
            {
                const auto all = s->scan(std::string_view{}, s->size());
                if (all.size() != ref.size())
                    throw std::runtime_error("end scan: size mismatch");
                std::size_t j = 0;
                for (const auto& kv : ref) {
                    if (all[j].first != kv.first || all[j].second != kv.second)
                        throw std::runtime_error("end scan: content mismatch");
                    ++j;
                }
            }

            std::cerr << kind << " " << backend_name << " " << mode << " rep" << r
                      << " done" << std::endl;
        }

        std::cout << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\",\"backend\":\""
                  << backend_name << "\",\"case\":\"done\"}" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

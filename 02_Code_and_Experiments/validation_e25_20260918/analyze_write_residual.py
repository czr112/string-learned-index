#!/usr/bin/env python3
"""Stage D analyzer: diag_write_residual rows -> the round's worksheet.

Reads one or more JSONL files (or a directory of them), groups cells by
(kind, backend, mode, frac) and prints, per checkpoint:

  * COUNT build: per-CYCLE work on the erase side and the insert side apart, for
    the LOCAL WINDOW (last 8192 replacement cycles) and the whole INTERVAL;
  * TIME build: per-OPERATION phase time, raw and corrected (calls * calib_ns),
    with the nesting respected -- components are SHARES of their parent, they are
    not added together across nesting levels;
  * the checkpoint state (overflow stock, compactions, index bytes).

It refuses to summarize a build whose required fields are missing: a column of
zeros that means "the instrument did not run" must not read as "the work does
not happen". The required lists mirror the source's DR_E25_FIELDS /
DR_E18_FIELDS / DR_T_FIELDS exactly.

Usage: analyze_write_residual.py PATH [PATH ...]
"""
import json
import os
import sys
from statistics import median

E25_FIELDS = """locate_calls predict_calls fp_of_calls fp_hash_bytes existence_searches
cmp_calls cmp_bytes place_attempts place_fail_slots place_fail_bytes compactions
region_rebuilds erase_shifts erase_released_primary erase_released_overflow string_allocs
ov_suffix_bytes_written string_frees compact_entered compact_helped compact_no_help
compact_records_replayed compact_bytes_copied compact_garbage_before compact_tag_mismatch
compact_plan_violation compact_plan_rejected notlocated_fallback subblock_reclaims
subblock_reclaim_helped reclaim_widened reclaim_widened_helped
reclaim_widened_blocks_examined migrate_calls migrated_records migrated_bytes migrate_helped
migrate_rejected reclaim_skipped_unchanged reclaim_skipped_gap fail_bytes_hole_fits
fail_bytes_hole_absent fail_bytes_repack_fits fail_bytes_len_sum fail_bytes_best_hole_sum
fail_bytes_garbage_sum fail_bytes_live_sum hole_sweep_checks hole_sweep_mismatch
hole_events_recorded hole_events_dropped low_activity_reclaimed low_activity_n0
low_activity_n1 low_activity_bytes low_activity_helped hole_nholes0 hole_nholes1
hole_nholes2plus hole_best_eq_garbage hole_count_lt2 hole_count_eq0 hole_count_eq1
hole_slots_not_free fail_bytes_repack_guarded prefix_rebuilds threshold_splits scan_regions
scan_groups scan_groups_empty scan_blocks_skipped scan_merge_cmps scan_result_allocs
ov_shifted_records_insert ov_shifted_bytes_insert ov_shifted_records_erase
ov_shifted_bytes_erase overflow_record_moves_stable overflow_record_move_bytes_stable
overflow_handle_moves overflow_handle_move_bytes ov_store_high_water ov_store_live_water
ov_new_peak_events ov_new_peak_overshoot ov_new_peak_copy_bytes ov_cap_change_events
ov_cap_change_copy_bytes ov_queries_bin ov_queries_linear ov_bin_cmps ov_bin_decided4
ov_bin_decided8 ov_bin_short4 ov_bin_lcp_bytes ov_linear_recs ov_linear_tag_reject
ov_linear_tag_pass ov_linear_reject8 ov_linear_lcp_bytes ov_tag_entries ov_tag_reject_pre
scan_start_locates scan_canonical_group scan_cursor_starts scan_ov_cursor_starts
scan_adv_p_cmps scan_adv_p_skipped scan_adv_p_subblocks scan_adv_ov_cmps scan_adv_ov_skipped
scan_out_primary scan_out_overflow scan_out_bytes put_primary_calls fc_calls fc_probes fc_gap hint_eq hint_ne hint_nodir hint_below""".split()

E18_FIELDS = """group_calls fingerprint_calls region_ctors subblock_probes insert_attempts
insert_ok entry_shifts suffix_bytes_copied overflow_inserts overflow_containers""".split()

T_FIELDS = """compact_ns compact_calls collect_ns replay_ns erase_ns erase_calls insert_ns
insert_calls ov_insert_ns ov_insert_calls ov_erase_ns ov_erase_calls ov_lookup_ns
ov_lookup_calls ov_alloc_ns ov_alloc_calls w_insert_ns w_insert_calls w_erase_ns
w_erase_calls w_locate_ns w_locate_calls w_locate_ins_ns w_locate_ins_calls
w_locate_era_ns w_locate_era_calls w_place_ns w_place_calls w_rebuild_ns w_rebuild_calls
w_rebuild_all_ns w_rebuild_all_calls w_rebuild_all_records w_reclaim_ns w_reclaim_calls
w_findloc_ns w_findloc_calls w_walk_ns w_walk_calls w_fc_ns w_fc_calls""".split()

# Repack split (2026-10-11). Present only in files whose header carries "rp_split": 1;
# check_file adds them to the required set for exactly those files, so the stage-D files
# (no marker) still analyze with their own field set. The count side and the time side
# are separate lists because the two builds are mutually exclusive by construction.
RP_COUNT_FIELDS = """rp_calls rp_records rp_bytes rp_moved_records rp_moved_bytes
rp_selfcopy_records rp_selfcopy_bytes""".split()
RP_TIME_FIELDS = "w_rp_sort_ns w_rp_sort_calls w_rp_move_ns w_rp_move_calls".split()


STATE_FIELDS = """st_live st_regions st_overflow st_compactions st_blocks st_model st_prefix
st_overflow_bytes st_tables st_directory st_regions_self st_fences st_self
st_index_bytes""".split()

CELL_BASE = """t kind mode backend rep frac from_cycle to_cycle cycles win_from_cycle
win_cycles drv_seg_ns drv_win_ns build""".split()
HDR_BASE = """t kind mode backend n thr reps workload fracs window_cycles clock calib_ns src
build key_hash sched_hash cpu switches""".split()


def fail(msg):
    print("ANALYZER FAILED: " + msg, file=sys.stderr)
    sys.exit(2)


def load_files(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for name in sorted(os.listdir(p)):
                if name.endswith(".jsonl"):
                    files.append(os.path.join(p, name))
        else:
            files.append(p)
    if not files:
        fail("no .jsonl inputs found")
    return files


def read_rows(path):
    rows = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    if not rows:
        fail("empty file: " + path)
    return rows


def check_file(path, rows, require_split=False):
    hdr = rows[0]
    if hdr.get("t") != "hdr":
        fail(path + ": first row is not the header")
    for k in HDR_BASE:
        if k not in hdr:
            fail(path + ": header field missing: " + k)
    build = hdr["build"]
    rp_split = hdr.get("rp_split") == 1
    if require_split and not rp_split:
        fail(path + ": --require-rp-split was given but the header carries no rp_split marker")
    expected = (E25_FIELDS + E18_FIELDS) if build == "count" else T_FIELDS
    if rp_split:
        expected = expected + (RP_COUNT_FIELDS if build == "count" else RP_TIME_FIELDS)
    for row in rows[1:]:
        if row.get("t") != "cell":
            fail(path + ": unexpected row type " + str(row.get("t")))
        for k in CELL_BASE + STATE_FIELDS:
            if k not in row:
                fail(path + ": cell field missing: " + k)
        for side in ("int_e_", "int_i_", "win_e_", "win_i_"):
            for name in expected:
                if side + name not in row:
                    fail(path + ": field missing: " + side + name)
            if not rp_split:
                continue
            # Arithmetic the split cannot satisfy by accident: every record the packing
            # loop handled is either moved or a self-copy; the two segments never exceed
            # the parent clock they are carved out of, and sort/move see the same passes.
            if build == "count":
                if (row[side + "rp_moved_records"] + row[side + "rp_selfcopy_records"]
                        != row[side + "rp_records"]):
                    fail(path + ": rp records do not add up at " + side)
                if (row[side + "rp_moved_bytes"] + row[side + "rp_selfcopy_bytes"]
                        != row[side + "rp_bytes"]):
                    fail(path + ": rp bytes do not add up at " + side)
            else:
                if row[side + "w_rp_sort_calls"] != row[side + "w_rp_move_calls"]:
                    fail(path + ": rp sort/move call counts differ at " + side)
                if row[side + "w_rp_sort_calls"] > row[side + "w_reclaim_calls"]:
                    fail(path + ": rp full passes exceed reclaim calls at " + side)
                if (row[side + "w_reclaim_ns"]
                        < row[side + "w_rp_sort_ns"] + row[side + "w_rp_move_ns"]):
                    fail(path + ": rp segments exceed the parent clock at " + side)
    return build


def med(xs):
    return median(xs) if xs else 0.0


COUNT_INSERT_ROWS = [
    ("inserts", "w_insert_calls", "t"),
    ("locate (outer)", "locate_calls", "e25"),
    ("predict", "predict_calls", "e25"),
    ("fingerprint", "fp_of_calls", "e25"),
    ("exists search", "existence_searches", "e25"),
    ("full cmp", "cmp_calls", "e25"),
    ("cmp bytes", "cmp_bytes", "e25"),
    ("place attempts", "place_attempts", "e25"),
    ("fail: slots", "place_fail_slots", "e25"),
    ("fail: bytes", "place_fail_bytes", "e25"),
    ("subblk probes", "subblock_probes", "e18"),
    ("ins attempts", "insert_attempts", "e18"),
    ("ins ok", "insert_ok", "e18"),
    ("entry shifts", "entry_shifts", "e18"),
    ("suffix bytes", "suffix_bytes_copied", "e18"),
    ("reclaims", "subblock_reclaims", "e25"),
    ("reclaim helped", "subblock_reclaim_helped", "e25"),
    ("widen passes", "reclaim_widened", "e25"),
    ("widen blocks", "reclaim_widened_blocks_examined", "e25"),
    ("compact entered", "compact_entered", "e25"),
    ("compact replay recs", "compact_records_replayed", "e25"),
    ("compact bytes", "compact_bytes_copied", "e25"),
    ("overflow inserts", "overflow_inserts", "e18"),
    ("string allocs", "string_allocs", "e25"),
    ("ov suffix bytes", "ov_suffix_bytes_written", "e25"),
    ("region rebuilds", "region_rebuilds", "e25"),
    ("threshold splits", "threshold_splits", "e25"),
]

COUNT_ERASE_ROWS = [
    ("erases", "w_erase_calls", "t"),
    ("locate (outer)", "locate_calls", "e25"),
    ("exists search", "existence_searches", "e25"),
    ("full cmp", "cmp_calls", "e25"),
    ("cmp bytes", "cmp_bytes", "e25"),
    ("erase shifts", "erase_shifts", "e25"),
    ("released primary", "erase_released_primary", "e25"),
    ("released overflow", "erase_released_overflow", "e25"),
    ("string frees", "string_frees", "e25"),
    ("ov shift recs", "ov_shifted_records_erase", "e25"),
    ("ov shift bytes", "ov_shifted_bytes_erase", "e25"),
]

# The *_calls fields only exist in the timing build; in the counting build the
# cycle count itself is the denominator, so those rows are printed only for time.


def get_field(rows, side, field, fieldsets):
    kind = fieldsets
    for r in rows:
        if side + field in r:
            return True
    return False


def _rp_per_call(rows, ns, calls_field, calib):
    """Per-full-pass nanoseconds: raw and corrected by ONE calibration pair. Each segment
    is delimited by a single shared boundary, so one pair is its own instrument cost.
    Returns (raw, corrected, median calls)."""
    raw, corr, calls = [], [], []
    for r in rows:
        c = r["win_i_" + calls_field]
        calls.append(c)
        if c > 0:
            raw.append(r["win_i_" + ns] / c)
            corr.append(r["win_i_" + ns] / c - calib)
    if not raw:
        return 0.0, 0.0, med(calls)
    return med(raw), med(corr), med(calls)


def rp_time_group(rows, calib):
    def m(key):
        return med([r["win_i_" + key] for r in rows])
    cycles = max(1.0, med([r["win_cycles"] for r in rows]))
    s_raw, s_corr, _ = _rp_per_call(rows, "w_rp_sort_ns", "w_rp_sort_calls", calib)
    m_raw, m_corr, _ = _rp_per_call(rows, "w_rp_move_ns", "w_rp_move_calls", calib)
    t_raw, t_corr, _ = _rp_per_call(rows, "w_reclaim_ns", "w_reclaim_calls", 2.0 * calib)
    fs = " [AT FLOOR]" if s_raw <= calib else ""
    fm = " [AT FLOOR]" if m_raw <= calib else ""
    tot = s_corr + m_corr
    share = (100.0 * m_corr / tot) if tot > 0 else 0.0
    print("    repack split (insert side, per full pass; one calib pair removed per segment,"
          " the parent by two approximate pairs):")
    print("      reclaim calls %.3f/op; full passes %.3f/op; sort %8.1f -> %8.1f%s ns; "
          "move %8.1f -> %8.1f%s ns; parent %8.1f -> %8.1f ns; move share %5.1f%%"
          % (m("w_reclaim_calls") / cycles, m("w_rp_sort_calls") / cycles,
             s_raw, s_corr, fs, m_raw, m_corr, fm, t_raw, t_corr, share))
    ceiling = (m("w_reclaim_calls") / cycles) * m_corr
    print("      H ceiling before its own find cost: %.1f ns/op = %.3f calls/op x %.1f ns"
          " move/call" % (ceiling, m("w_reclaim_calls") / cycles, m_corr))


def rp_count_group(rows):
    def m(key):
        return med([r["win_i_" + key] for r in rows])
    cycles = max(1.0, med([r["win_cycles"] for r in rows]))
    calls = m("rp_calls")
    per = (lambda x: (m(x) / calls) if calls > 0 else 0.0)
    perop = (lambda x: m(x) / cycles)
    print("    repack work (insert side): %.3f full passes/op; per pass: %6.2f records, "
          "%8.1f B memmove'd (moved %6.2f rec / %8.1f B; self-copy %6.2f rec / %8.1f B)"
          % (calls / cycles, per("rp_records"), per("rp_bytes"), per("rp_moved_records"),
             per("rp_moved_bytes"), per("rp_selfcopy_records"), per("rp_selfcopy_bytes")))
    print("      per op: %6.2f records, %8.1f B moved (offset changed)"
          % (perop("rp_records"), perop("rp_moved_bytes")))


def show_count(path, hdr, cells):
    print("=" * 110)
    print("FILE %s" % os.path.basename(path))
    print("  build=count corpus=%s n=%d reps=%d thr=%d" %
          (hdr["kind"], hdr["n"], hdr["reps"], hdr["thr"]))
    print("  switches: %s" % hdr["switches"])
    print("  key_hash=%d sched_hash=%d" % (hdr["key_hash"], hdr["sched_hash"]))
    print("=" * 110)
    groups = {}
    for r in cells:
        groups.setdefault((r["kind"], r["backend"], r["mode"], r["frac"]), []).append(r)
    for key in sorted(groups, key=str):
        kind, backend, mode, frac = key
        rows = groups[key]
        reps = sorted({r["rep"] for r in rows})
        cyc = rows[0]["cycles"]
        wcyc = rows[0]["win_cycles"]
        print()
        print("--- %s / %s / %s / frac=%.2f   (%d reps; interval %d cycles, window %d)"
              % (kind, backend, mode, frac, len(reps), cyc, wcyc))
        print("    %-20s %12s %12s" % ("insert side", "window/ins", "interval/ins"))
        for label, field, fs in COUNT_INSERT_ROWS:
            prefix_set = (E25_FIELDS if fs == "e25" else E18_FIELDS) if fs != "t" else None
            if fs == "t":
                continue  # *_calls do not exist in the count build
            if field not in prefix_set and fs != "t":
                fail("analyzer field table out of date: " + field)
            wv = med([r["win_i_" + field] / max(1, r["win_cycles"]) for r in rows])
            iv = med([r["int_i_" + field] / max(1, r["cycles"]) for r in rows])
            print("    %-20s %12.3f %12.3f" % (label, wv, iv))
        print("    %-20s %12s %12s" % ("erase side", "window/era", "interval/era"))
        for label, field, fs in COUNT_ERASE_ROWS:
            if fs == "t":
                continue
            wv = med([r["win_e_" + field] / max(1, r["win_cycles"]) for r in rows])
            iv = med([r["int_e_" + field] / max(1, r["cycles"]) for r in rows])
            print("    %-20s %12.3f %12.3f" % (label, wv, iv))
        if hdr.get("rp_split") == 1:
            rp_count_group(rows)
        st = {k: med([r[k] for r in rows]) for k in STATE_FIELDS}
        print("    state@checkpoint: live=%.0f overflow=%.0f compactions=%.0f "
              "index_bytes=%.0f regions=%.0f"
              % (st["st_live"], st["st_overflow"], st["st_compactions"],
                 st["st_index_bytes"], st["st_regions"]))
        drv = med([r["drv_seg_ns"] for r in rows]) / 1e6
        drvw = med([r["drv_win_ns"] for r in rows]) / 1e6
        print("    driver walls (instrumented, NOT timings): seg=%.1f ms window=%.1f ms"
              % (drv, drvw))


TIME_INSERT = [
    ("Index::insert (whole)", "w_insert_ns", "w_insert_calls"),
    ("  locate (outer)", "w_locate_ins_ns", "w_locate_ins_calls"),
    ("  Region::find_located", "w_findloc_ns", "w_findloc_calls"),
    ("  place_with_ctx", "w_place_ns", "w_place_calls"),
    ("    of which repack_subblock", "w_reclaim_ns", "w_reclaim_calls"),
    ("    of which compact", "compact_ns", "compact_calls"),
    ("  rebuild (all)", "w_rebuild_all_ns", "w_rebuild_all_calls"),
    ("  rebuild (region)", "w_rebuild_ns", "w_rebuild_calls"),
    ("  Region::insert_slow", "insert_ns", "insert_calls"),
    ("  ov insert (total)", "ov_insert_ns", "ov_insert_calls"),
    ("    of which ov lookup", "ov_lookup_ns", "ov_lookup_calls"),
    ("    of which ov alloc", "ov_alloc_ns", "ov_alloc_calls"),
]

TIME_ERASE = [
    ("Index::erase (whole)", "w_erase_ns", "w_erase_calls"),
    ("  locate (outer)", "w_locate_era_ns", "w_locate_era_calls"),
    ("  Region::erase", "erase_ns", "erase_calls"),
    ("    of which find_located", "w_findloc_ns", "w_findloc_calls"),
    ("    of which ov erase", "ov_erase_ns", "ov_erase_calls"),
]


def _per_op(rows, side, ns, calls, calib, win):
    dn = "win_cycles" if win else "cycles"
    raw, corr, shares = [], [], []
    for r in rows:
        d = max(1, r[dn])
        raw_v = r[side + ns] / d
        c = r[side + calls] if calls else 0
        corr_v = (r[side + ns] - c * calib) / d
        raw.append(raw_v)
        corr.append(corr_v)
    return med(raw), med(corr)


def show_time(path, hdr, cells):
    calib = hdr["calib_ns"]
    print("=" * 110)
    print("FILE %s" % os.path.basename(path))
    print("  build=time corpus=%s n=%d reps=%d thr=%d calib=%.3f ns/pair cpu=%s"
          % (hdr["kind"], hdr["n"], hdr["reps"], hdr["thr"], calib, hdr["cpu"]))
    print("  switches: %s" % hdr["switches"])
    print("  key_hash=%d sched_hash=%d" % (hdr["key_hash"], hdr["sched_hash"]))
    print("=" * 110)
    groups = {}
    for r in cells:
        groups.setdefault((r["kind"], r["backend"], r["mode"], r["frac"]), []).append(r)
    for key in sorted(groups, key=str):
        kind, backend, mode, frac = key
        rows = groups[key]
        reps = sorted({r["rep"] for r in rows})
        cyc = rows[0]["cycles"]
        wcyc = rows[0]["win_cycles"]
        print()
        print("--- %s / %s / %s / frac=%.2f   (%d reps; interval %d cycles, window %d)"
              % (kind, backend, mode, frac, len(reps), cyc, wcyc))
        # cross-check: one insert and one erase per cycle
        ins_calls = med([r["win_i_w_insert_calls"] for r in rows])
        era_calls = med([r["win_e_w_erase_calls"] for r in rows])
        flag = "OK" if (abs(ins_calls - wcyc) < 0.5 and abs(era_calls - wcyc) < 0.5) else "MISMATCH"
        print("    call cross-check: w_insert_calls=%.0f w_erase_calls=%.0f vs window %d -> %s"
              % (ins_calls, era_calls, wcyc, flag))
        for side, label, spec in (("i", "INSERT", TIME_INSERT), ("e", "ERASE", TIME_ERASE)):
            print("    %s side, window per operation (ns/op; raw -> corrected):" % label)
            whole_corr = None
            for name, ns, calls in spec:
                raw_w, corr_w = _per_op(rows, "win_" + side + "_", ns, calls, calib, True)
                raw_i, corr_i = _per_op(rows, "int_" + side + "_", ns, calls, calib, False)
                if name.startswith("Index::insert (whole)") or name.startswith("Index::erase (whole)"):
                    whole_corr = corr_w
                share = (100.0 * corr_w / whole_corr) if whole_corr else 0.0
                print("      %-28s win %10.1f -> %10.1f (%6.1f%%)   int %10.1f -> %10.1f"
                      % (name, raw_w, corr_w, share, raw_i, corr_i))
            # The residual is computed on RAW values and then has the whole span own
            # clock pair (calls * calib) removed: subtracting CORRECTED parts would
            # double-count nothing but would leave the whole own pair inside the
            # difference (the earlier version did exactly that and printed ~2x).
            if side == "i":
                whole_raw = med([r["win_i_w_insert_ns"] / max(1, r["win_cycles"]) for r in rows])
                whole_calls = med([r["win_i_w_insert_calls"] for r in rows])
                parts = ["w_locate_ins", "w_findloc", "w_place", "insert", "w_rebuild",
                         "w_rebuild_all"]
            else:
                whole_raw = med([r["win_e_w_erase_ns"] / max(1, r["win_cycles"]) for r in rows])
                whole_calls = med([r["win_e_w_erase_calls"] for r in rows])
                parts = ["w_locate_era", "erase"]
            acc = 0.0
            for p in parts:
                raw_w, corr_w = _per_op(rows, "win_" + side + "_", p + "_ns", p + "_calls",
                                        calib, True)
                acc += raw_w
            resid = whole_raw - acc - calib * whole_calls / max(1, med([r["win_cycles"] for r in rows]))
            print("      %-28s win %10.1f   (%.1f%% of the whole; the whole own clock pair"
                  " is removed, nested pairs stay inside their part)"
                  % ("residual (uninstrumented)", resid, 100.0 * resid / whole_raw))
        if hdr.get("rp_split") == 1:
            rp_time_group(rows, calib)
        st = {k: med([r[k] for r in rows]) for k in STATE_FIELDS}
        print("    state@checkpoint: live=%.0f overflow=%.0f compactions=%.0f "
              "index_bytes=%.0f" % (st["st_live"], st["st_overflow"], st["st_compactions"],
                                    st["st_index_bytes"]))
        drv = med([r["drv_seg_ns"] for r in rows]) / 1e6
        drvw = med([r["drv_win_ns"] for r in rows]) / 1e6
        print("    driver walls (instrumented, NOT timings): seg=%.1f ms window=%.1f ms"
              % (drv, drvw))


# ---------------------------------------------------------------- summary mode
def _wmed(rows, key, denom=None):
    if denom is None:
        return med([r[key] for r in rows])
    return med([r[key] / max(1, r[denom]) for r in rows])


def summary_count(path, hdr, cells):
    print("-" * 118)
    print("SUMMARY count build: %s  corpus=%s n=%d thr=%d reps=%d"
          % (os.path.basename(path), hdr["kind"], hdr["n"], hdr["thr"], hdr["reps"]))
    groups = {}
    for r in cells:
        groups.setdefault((r["backend"], r["mode"], r["frac"]), []).append(r)
    for key in sorted(groups, key=str):
        backend, mode, frac = key
        rows = groups[key]
        w = "win_cycles"
        ins = {
            "recl": _wmed(rows, "win_i_subblock_reclaims", w),
            "recl_ok": _wmed(rows, "win_i_subblock_reclaim_helped", w),
            "widen": _wmed(rows, "win_i_reclaim_widened", w),
            "comp": _wmed(rows, "win_i_compact_entered", w),
            "ov": _wmed(rows, "win_i_overflow_inserts", w),
            "probe": _wmed(rows, "win_i_subblock_probes", w),
            "att": _wmed(rows, "win_i_place_attempts", w),
            "cmp": _wmed(rows, "win_i_cmp_calls", w),
            "cmpB": _wmed(rows, "win_i_cmp_bytes", w),
            "sfxB": _wmed(rows, "win_i_suffix_bytes_copied", w),
            "esh": _wmed(rows, "win_i_entry_shifts", w),
            "fallback": _wmed(rows, "win_i_notlocated_fallback", w),
            "walk": _wmed(rows, "win_i_put_primary_calls", w),
            "fc": _wmed(rows, "win_i_fc_calls", w),
            "fcp": _wmed(rows, "win_i_fc_probes", w),
            "gap": _wmed(rows, "win_i_fc_gap", w),
            "heq": _wmed(rows, "win_i_hint_eq"),
            "hne": _wmed(rows, "win_i_hint_ne"),
            "hnd": _wmed(rows, "win_i_hint_nodir"),
            "hbl": _wmed(rows, "win_i_hint_below"),
        }
        era = {
            "cmp": _wmed(rows, "win_e_cmp_calls", w),
            "cmpB": _wmed(rows, "win_e_cmp_bytes", w),
            "sh": _wmed(rows, "win_e_erase_shifts", w),
            "relP": _wmed(rows, "win_e_erase_released_primary", w),
            "relO": _wmed(rows, "win_e_erase_released_overflow", w),
            "ovR": _wmed(rows, "win_e_ov_shifted_records_erase", w),
            "ovB": _wmed(rows, "win_e_ov_shifted_bytes_erase", w),
            "free": _wmed(rows, "win_e_string_frees", w),
        }
        st_ov = _wmed(rows, "st_overflow")
        st_mb = _wmed(rows, "st_index_bytes") / 1e6
        print("%-5s %-4s f=%.2f | ins/op recl=%.3f(ok %.3f) widen=%.3f comp=%.3f "
              "ov=%.3f probe=%.2f att=%.2f cmp=%.1f cmpB=%.0f sfxB=%.1f esh=%.1f fb=%.3f"
              % (backend, mode, frac, ins["recl"], ins["recl_ok"], ins["widen"], ins["comp"],
                 ins["ov"], ins["probe"], ins["att"], ins["cmp"], ins["cmpB"], ins["sfxB"],
                 ins["esh"], ins["fallback"]))
        print("                    | walks/op put_primary=%.2f fc=%.2f fcprobe=%.2f gap=%.3f | "
              "hint eq/ne/nodir/below=%d/%d/%d/%d"
              % (ins["walk"], ins["fc"], ins["fcp"], ins["gap"], ins["heq"], ins["hne"],
                 ins["hnd"], ins["hbl"]))
        print("                    | era/op cmp=%.2f cmpB=%.0f sh=%.1f relP=%.1f relO=%.2f "
              "ovR=%.2f ovB=%.0f free=%.2f | ov=%.0f MB=%.2f"
              % (era["cmp"], era["cmpB"], era["sh"], era["relP"], era["relO"], era["ovR"],
                 era["ovB"], era["free"], st_ov, st_mb))
        if hdr.get("rp_split") == 1:
            rpc = _wmed(rows, "win_i_rp_calls", w)
            per = (lambda x: rpc and _wmed(rows, "win_i_" + x, "win_i_rp_calls"))
            print("                    | rp work/pass: %6.2f rec, %7.1f B; moved %6.2f "
                  "rec/%7.1f B; self-copy %6.2f rec/%7.1f B; passes/op %.3f"
                  % (per("rp_records"), per("rp_bytes"), per("rp_moved_records"),
                     per("rp_moved_bytes"), per("rp_selfcopy_records"),
                     per("rp_selfcopy_bytes"), rpc))


def summary_time(path, hdr, cells):
    calib = hdr["calib_ns"]
    print("-" * 118)
    print("SUMMARY time build: %s  corpus=%s n=%d reps=%d calib=%.1f ns/pair"
          % (os.path.basename(path), hdr["kind"], hdr["n"], hdr["reps"], calib))
    groups = {}
    for r in cells:
        groups.setdefault((r["backend"], r["mode"], r["frac"]), []).append(r)
    for key in sorted(groups, key=str):
        backend, mode, frac = key
        rows = groups[key]
        wc = med([r["win_cycles"] for r in rows])

        def op(side, name):
            return med([r["win_%s_%s" % (side, name)] / max(1, r["win_cycles"]) for r in rows])

        iw = {
            "whole": med([r["win_i_w_insert_ns"] / max(1, r["win_cycles"]) for r in rows]),
            "loc": op("i", "w_locate_ins_ns"), "find": op("i", "w_findloc_ns"),
            "place": op("i", "w_place_ns"), "recl": op("i", "w_reclaim_ns"),
            "comp": op("i", "compact_ns"), "slow": op("i", "insert_ns"),
            "reb": op("i", "w_rebuild_ns") + op("i", "w_rebuild_all_ns"),
            "ov": op("i", "ov_insert_ns"), "ovl": op("i", "ov_lookup_ns"),
        }
        ew = {
            "whole": med([r["win_e_w_erase_ns"] / max(1, r["win_cycles"]) for r in rows]),
            "loc": op("e", "w_locate_era_ns"), "reg": op("e", "erase_ns"),
            "find": op("e", "w_findloc_ns"), "ov": op("e", "ov_erase_ns"),
        }
        ires = iw["whole"] - (iw["loc"] + iw["find"] + iw["place"] + iw["slow"] + iw["reb"]) - calib
        eres = ew["whole"] - (ew["loc"] + ew["reg"]) - calib
        print("%-5s %-4s f=%.2f | ins %7.1f = loc %6.1f + find %6.1f + place %6.1f "
              "(recl %5.1f) + slow %5.1f + reb %5.1f + resid %6.1f (%4.1f%%); ov %5.1f"
              % (backend, mode, frac, iw["whole"], iw["loc"], iw["find"], iw["place"],
                 iw["recl"], iw["slow"], iw["reb"], ires,
                 100.0 * ires / iw["whole"], iw["ov"]))
        print("                    | era %7.1f = loc %6.1f + region %6.1f (find %5.1f) "
              "+ resid %6.1f (%4.1f%%)"
              % (ew["whole"], ew["loc"], ew["reg"], ew["find"], eres,
                 100.0 * eres / ew["whole"]))
        print("                    | walks %5.1f (fc %5.1f) ns/op; calls %.2f + %.2f /op"
              % (op("i", "w_walk_ns"), op("i", "w_fc_ns"), op("i", "w_walk_calls"),
                 op("i", "w_fc_calls")))
        if hdr.get("rp_split") == 1:
            s_raw, s_corr, _ = _rp_per_call(rows, "w_rp_sort_ns", "w_rp_sort_calls", calib)
            m_raw, m_corr, _ = _rp_per_call(rows, "w_rp_move_ns", "w_rp_move_calls", calib)
            tot = s_corr + m_corr
            print("                    | rp split: sort %7.1f move %7.1f ns/call (corr); "
                  "move share %5.1f%%; calls %.3f/op"
                  % (s_corr, m_corr, (100.0 * m_corr / tot) if tot > 0 else 0.0,
                     op("i", "w_reclaim_calls")))


def main(argv):
    usage = ("usage: analyze_write_residual.py [--summary] [--require-rp-split] "
             "PATH [PATH ...]")
    if len(argv) < 2:
        fail(usage)
    summary = False
    require_split = False
    args = argv[1:]
    while args and args[0].startswith("--"):
        if args[0] == "--summary":
            summary = True
        elif args[0] == "--require-rp-split":
            require_split = True
        else:
            fail("unknown option: " + args[0])
        args = args[1:]
    if not args:
        fail(usage)
    pairs = {}
    for path in load_files(args):
        rows = read_rows(path)
        build = check_file(path, rows, require_split=require_split)
        hdr = rows[0]
        pairs.setdefault((hdr["backend"], hdr["kind"]), set()).add(build)
        cells = rows[1:]
        if summary:
            if build == "count":
                summary_count(path, hdr, cells)
            else:
                summary_time(path, hdr, cells)
        else:
            if build == "count":
                show_count(path, hdr, cells)
            else:
                show_time(path, hdr, cells)
    if require_split:
        for key in sorted(pairs):
            builds = pairs[key]
            if builds != {"count", "time"}:
                fail("--require-rp-split: %s/%s has files for %s, not one count+time pair"
                     % (key[0], key[1], "/".join(sorted(builds))))
    print()
    print("analyze_write_residual: done")


if __name__ == "__main__":
    main(sys.argv)

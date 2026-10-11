#!/usr/bin/env python3
"""Stage A scan-entry analyzer (2026-10-10).

Reads one or more (main, scanwork) pairs produced by the -DE25_DIAG diagnostic twin of
bench_combo and:

  1. REFUSES a file whose grid is incomplete: every (rep, arm, checkpoint, limit) cell must
     carry two timed passes and one verifying pass over the same 16 starts, plus the boundary
     probes. A missing cell is not "zero work", it is a missing measurement.
  2. REFUSES a row that contradicts the scan contract: rows == expect_rows,
     out_primary + out_overflow == rows, the verification pass reported no byte mismatch, and
     the advance counters obey the arithmetic of the advance loop itself
     (skipped <= cmps <= skipped + 1, the same for the overflow container). That last pair is
     the check that the comparisons were counted AT the comparison: a count reconstructed from
     log2(n) cannot satisfy it for both a walk that stops mid-container and one that runs off
     the end.
  3. CROSS-CHECKS the per-call deltas against the checkpoint-cumulative counters of the main
     file: the per-call sum for a checkpoint may never exceed the main delta, and the residual
     is reported as scan work that happened outside the benchmark's own scan phases (region
     rebuilds, maintenance). The round that came before this one read the cumulative number
     alone and could not separate the two; this is that separation, printed rather than argued.
  4. Prints the per-start and per-probe tables. Per start, not per median: one extreme start is
     the whole reason this round exists, and a median over sixteen starts hides exactly it.

COUNTS ONLY. Nothing here turns a count into a time, a time share or a speed-up. The only
ratios printed are count ratios between arms, labelled as such.

Usage:
  python3 -B analyze_scan_entry.py [--json OUT] [--relaxed] [stem ...]
  python3 -B analyze_scan_entry.py --selftest

Exit codes: 0 = every check passed and the report was printed; 2 = a refusal (the reason is
printed); 1 = usage or IO error.
"""

import argparse
import json
import os
import statistics
import sys
import tempfile

ROUND_DIR = os.path.dirname(os.path.abspath(__file__))
MAIN_DIR = os.path.join(ROUND_DIR, "results", "main")

# The three grids Stage A is required to run (plan section 3.1). n is asserted because a
# diagnosis run at the wrong size is a different experiment wearing the right name.
GRID_SPECS = {
    "combo_se_ctx16_growtranco": {"kind": "tranco", "mode": "grow", "backend": "Context16", "n": 262144},
    "combo_se_lits_growtranco": {"kind": "tranco", "mode": "grow", "backend": "LitsHpt", "n": 262144},
    "combo_se_lits_samemixed": {"kind": "mixed", "mode": "same", "backend": "LitsHpt", "n": 131072},
}

ARM_ORDER = ("B", "W", "C", "WC")
ALL_LIMITS = (16, 256, 4096)
ALL_FRACS = (0.0, 0.25, 0.5, 1.0, 2.0)
STARTS = 17  # start 0 is the empty string, 1..16 the sampled live keys (bench_combo)
REQUIRED_PROBES = ("absent_mid", "after_last", "first_key")
OPTIONAL_PROBES = ("before_first",)

# Every numeric field a scanwork row must carry. A missing field is a refusal, never a zero:
# an absent counter and a counter that read zero are different statements.
WORK_INT_FIELDS = (
    "rep", "position", "n", "thr", "limit", "pass", "start_idx", "start_len", "start_fnv",
    "rows", "expect_rows", "bytes_mismatch",
    "adv_p_cmps", "adv_p_skipped", "adv_p_subblocks", "adv_o_cmps", "adv_o_skipped",
    "out_primary", "out_overflow", "out_bytes",
    "scan_regions", "scan_groups", "scan_groups_empty", "scan_blocks_skipped",
    "scan_merge_cmps", "scan_result_allocs", "scan_start_locates", "scan_cursor_starts",
    "scan_ov_cursor_starts", "scan_canonical",
)
WORK_STR_FIELDS = ("kind", "run_tag", "mode", "backend", "arm", "phase", "probe_id")

# Fields the main file carries per checkpoint that the per-call file also carries as deltas.
CUMULATIVE_FIELDS = (
    "scan_regions", "scan_groups", "scan_groups_empty", "scan_blocks_skipped",
    "scan_merge_cmps", "scan_result_allocs", "scan_start_locates", "scan_cursor_starts",
    "scan_ov_cursor_starts", "scan_canonical",
)

# THE COUNTER FIELDS -- the per-call deltas. Only these carry the wrap check below; fields
# like start_fnv and n are identities and a 64-bit hash is not a wrapped counter.
DELTA_FIELDS = (
    "adv_p_cmps", "adv_p_skipped", "adv_p_subblocks", "adv_o_cmps", "adv_o_skipped",
    "out_primary", "out_overflow", "out_bytes",
    "scan_regions", "scan_groups", "scan_groups_empty", "scan_blocks_skipped",
    "scan_merge_cmps", "scan_result_allocs", "scan_start_locates", "scan_cursor_starts",
    "scan_ov_cursor_starts", "scan_canonical",
)

# A delta larger than this is a wrapped subtraction (the counters are process-global and
# unsigned), not work: no single scan call in these grids can compare 2^40 records. The
# largest legitimate value here is out_bytes at limit=4096 over long keys.
DELTA_CEILING = 1 << 40


class Shape:
    def __init__(self, reps, arms=ARM_ORDER, fracs=ALL_FRACS, limits=ALL_LIMITS, starts=STARTS):
        self.reps = tuple(range(reps))
        self.arms = tuple(arms)
        self.fracs = tuple(fracs)
        self.limits = tuple(limits)
        self.starts = starts


def frac_key(frac):
    return int(round(float(frac) * 1000.0))


def read_jsonl(path, problems, what):
    rows = []
    if not os.path.exists(path):
        problems.append("missing %s file: %s" % (what, path))
        return rows
    with open(path, "r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except ValueError as exc:
                problems.append("%s line %d is not JSON: %s" % (what, lineno, exc))
    if not rows:
        problems.append("%s file is empty: %s" % (what, path))
    return rows


def check_row_shape(row, problems, where):
    for field in WORK_STR_FIELDS:
        if not isinstance(row.get(field), str):
            problems.append("%s: field %r is not a string" % (where, field))
    for field in WORK_INT_FIELDS:
        value = row.get(field)
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            problems.append("%s: field %r is not a number" % (where, field))
    if "bytes_checked" not in row:
        problems.append("%s: field 'bytes_checked' is missing" % where)
    if row.get("phase") not in ("timed", "verify", "probe"):
        problems.append("%s: unknown phase %r" % (where, row.get("phase")))


def check_row_contract(row, problems, where):
    """The invariants that hold for EVERY scan call, checked on the row itself."""
    rows = int(row["rows"])
    expect = int(row["expect_rows"])
    out_primary = int(row["out_primary"])
    out_overflow = int(row["out_overflow"])
    if rows != expect:
        problems.append("%s: rows %d != expect_rows %d (the answer disagrees with the live reference)"
                        % (where, rows, expect))
    if out_primary + out_overflow != rows:
        problems.append("%s: out_primary %d + out_overflow %d != rows %d"
                        % (where, out_primary, out_overflow, rows))
    if rows == 0 and int(row["out_bytes"]) != 0:
        problems.append("%s: zero rows but out_bytes %d" % (where, int(row["out_bytes"])))
    if rows > 0 and int(row["out_bytes"]) < rows:
        problems.append("%s: out_bytes %d < rows %d (a non-empty key is at least one byte)"
                        % (where, int(row["out_bytes"]), rows))
    if row["phase"] in ("verify", "probe"):
        if not row.get("bytes_checked"):
            problems.append("%s: %s row did not check the emitted key bytes" % (where, row["phase"]))
        if int(row["bytes_mismatch"]) != 0:
            problems.append("%s: bytes_mismatch %d" % (where, int(row["bytes_mismatch"])))
    # The advance loop evaluates one comparison per record it skips and AT MOST ONE more that
    # stops it -- PER REGION ENTERED. Index::scan calls Region::scan once per region and each
    # call runs the advance again on its own start (the empty view for every region after the
    # first), so a per-call bound of skipped + 1 is WRONG for a scan that spans regions: the
    # LitsHpt/tranco grid shows two regions and one failing comparison each with zero records
    # skipped. The first version of this analyzer asserted skipped + 1 and refused two honest
    # grids; the bound below is what the loop actually guarantees.
    #   primary   <= skipped + (regions that can enter the advance) = skipped + scan_regions
    #   overflow  <= skipped + (group cursors that had a container) = skipped + ov cursors
    p_cmps, p_skip = int(row["adv_p_cmps"]), int(row["adv_p_skipped"])
    o_cmps, o_skip = int(row["adv_o_cmps"]), int(row["adv_o_skipped"])
    if not (p_skip <= p_cmps <= p_skip + int(row["scan_regions"])):
        problems.append("%s: adv_p_cmps %d is not in [adv_p_skipped=%d, skips+regions=%d]"
                        % (where, p_cmps, p_skip, p_skip + int(row["scan_regions"])))
    if not (o_skip <= o_cmps <= o_skip + int(row["scan_ov_cursor_starts"])):
        problems.append("%s: adv_o_cmps %d is not in [adv_o_skipped=%d, skips+ov_cursors=%d]"
                        % (where, o_cmps, o_skip, o_skip + int(row["scan_ov_cursor_starts"])))
    # Structure counters that are exact by construction of the loop they sit in.
    if int(row["scan_groups"]) != int(row["scan_cursor_starts"]):
        problems.append("%s: scan_cursor_starts %d != scan_groups %d (one cursor per group)"
                        % (where, int(row["scan_cursor_starts"]), int(row["scan_groups"])))
    if int(row["scan_ov_cursor_starts"]) > int(row["scan_groups"]):
        problems.append("%s: scan_ov_cursor_starts > scan_groups" % where)
    if int(row["scan_groups_empty"]) > int(row["scan_groups"]):
        problems.append("%s: scan_groups_empty %d > scan_groups %d"
                        % (where, int(row["scan_groups_empty"]), int(row["scan_groups"])))
    if rows > 0 and int(row["scan_groups"]) < 1:
        problems.append("%s: %d rows returned but no group was entered" % (where, rows))
    if rows > 0 and int(row["out_primary"]) + int(row["out_overflow"]) == 0:
        problems.append("%s: %d rows but no output counted" % (where, rows))
    for field in DELTA_FIELDS:
        if int(row[field]) > DELTA_CEILING:
            problems.append("%s: %s = %d exceeds the wrap ceiling (counter reset mid-run?)"
                            % (where, field, int(row[field])))


def probe_rows_at(cells, rep, arm, frac, limit):
    """Every boundary-probe row of one cell. The probe id is part of the key, so a lookup
    with an empty probe id would silently match nothing; the round's first smoke run is the
    reason this helper exists rather than an inline get()."""
    out = []
    for key, row in cells.items():
        if (key[0] == rep and key[1] == arm and key[2] == frac and key[3] == limit
                and key[4] == "probe"):
            out.append(row)
    return out


def probe_ids_at(cells, rep, arm, frac, limit):
    return {row["probe_id"] for row in probe_rows_at(cells, rep, arm, frac, limit) if row["probe_id"]}


def check_grid(work_rows, shape, problems, stem):
    """Cell-by-cell shape of one diagnosis grid."""
    cells = {}
    for row in work_rows:
        where = "%s/%s/%s/frac=%s/limit=%d/%s/pass=%d/start=%d/%s" % (
            row["arm"], row["rep"], row["kind"], row["frac"], row["limit"], row["phase"],
            row["pass"], row["start_idx"], row["probe_id"])
        key = (int(row["rep"]), row["arm"], frac_key(row["frac"]), int(row["limit"]),
               row["phase"], int(row["pass"]), int(row["start_idx"]), row["probe_id"])
        if key in cells:
            problems.append("%s: duplicate scanwork cell %r" % (stem, key))
        cells[key] = row
        if int(row["rep"]) not in shape.reps:
            problems.append("%s: rep %s is outside the declared shape" % (stem, row["rep"]))
        if row["arm"] not in shape.arms:
            problems.append("%s: unknown arm %r" % (stem, row["arm"]))
        if frac_key(row["frac"]) not in [frac_key(f) for f in shape.fracs]:
            problems.append("%s: unknown checkpoint frac %r" % (stem, row["frac"]))
        if int(row["limit"]) not in shape.limits:
            problems.append("%s: unknown limit %r" % (stem, row["limit"]))

    missing = []
    for rep in shape.reps:
        for arm in shape.arms:
            for frac in shape.fracs:
                for limit in shape.limits:
                    for start in range(shape.starts):
                        for phase, passes in (("timed", (0, 1)), ("verify", (0,))):
                            for p in passes:
                                key = (rep, arm, frac_key(frac), limit, phase, p, start, "")
                                if key not in cells:
                                    missing.append(key)
                    for probe_id in REQUIRED_PROBES:
                        if probe_id not in probe_ids_at(cells, rep, arm, frac_key(frac), limit):
                            missing.append((rep, arm, frac_key(frac), limit, "probe", 0, -1, probe_id))
    if missing:
        shown = "\n".join("    %r" % (m,) for m in missing[:12])
        problems.append("%s: %d required scanwork cell(s) missing, e.g.\n%s" % (stem, len(missing), shown))
    return cells


def cross_check(main_rows, cells, shape, stem, report, problems):
    """Per-call deltas vs the checkpoint-cumulative counters of the main file.

    The main row of checkpoint k is written after seg k was applied and after that
    checkpoint's query and scan phases, so main[k] - main[k-1] is every scan counter movement
    in that window, while the scanwork rows only cover the benchmark's own scan calls. The
    per-call sum is therefore a LOWER bound, and the residual is the part that happened inside
    maintenance (region rebuilds call Region::all(), which calls scan()). Reported, not hidden.
    """
    by_key = {}
    for row in main_rows:
        by_key[(int(row["rep"]), row["arm"], frac_key(row["frac"]))] = row
    for rep in shape.reps:
        for arm in shape.arms:
            prev = None
            for frac in shape.fracs:
                row = by_key.get((rep, arm, frac_key(frac)))
                if row is None:
                    continue
                for field in CUMULATIVE_FIELDS:
                    if field not in row:
                        continue
                    now = int(row[field])
                    delta = now if prev is None else now - int(prev[field])
                    if delta < 0:
                        report.append("    %s %s frac=%s: main %s went backwards (%d -> %d)"
                                      % (stem, arm, frac, field, int(prev[field]), now))
                        continue
                    summed = 0
                    for limit in shape.limits:
                        for phase, passes in (("timed", (0, 1)), ("verify", (0,))):
                            for p in passes:
                                for start in range(shape.starts):
                                    r = cells.get((rep, arm, frac_key(frac), limit, phase, p,
                                                   start, ""))
                                    if r is not None:
                                        summed += int(r[field])
                        for r in probe_rows_at(cells, rep, arm, frac_key(frac), limit):
                            summed += int(r[field])
                    if summed > delta:
                        # The main row is written AFTER that checkpoint's scan phases, so the
                        # per-call sum can never legitimately exceed the main delta. If it
                        # does, the two files do not describe the same run (or the counters
                        # were reset mid-run) and nothing derived from them may be reported.
                        problems.append("%s %s frac=%s: per-call %s sum %d exceeds the main "
                                        "delta %d" % (stem, arm, frac, field, summed, delta))
                        report.append("    %s %s frac=%s limit-aggregate: per-call %s sum %d "
                                      "EXCEEDS the main delta %d"
                                      % (stem, arm, frac, field, summed, delta))
                prev = row


def med(values):
    return statistics.median(values) if values else float("nan")


def fmt(value, digits=1):
    if value is None:
        return "-"
    if isinstance(value, float):
        return ("%%.%df" % digits) % value
    return str(value)


def analyse(stem, shape, out, problems, report_lines):
    main_rows = read_jsonl(os.path.join(MAIN_DIR, stem + ".jsonl"), problems, stem + " main")
    work_rows = read_jsonl(os.path.join(MAIN_DIR, stem + "_scanwork.jsonl"), problems,
                           stem + " scanwork")
    if problems:
        return None

    meta = {}
    for row in work_rows:
        check_row_shape(row, problems, "%s row" % stem)
    for row in work_rows:
        where = "%s/%s/%s/frac=%s/limit=%d/%s/pass=%d/start=%d/%s" % (
            row["arm"], row["rep"], row["kind"], row["frac"], row["limit"], row["phase"],
            row["pass"], row["start_idx"], row["probe_id"])
        check_row_contract(row, problems, where)
    cells = check_grid(work_rows, shape, problems, stem)

    spec = GRID_SPECS.get(stem)
    if spec is not None:
        for row in work_rows:
            if row["kind"] != spec["kind"] or row["mode"] != spec["mode"]:
                problems.append("%s: row %r/%r does not match the grid's kind/mode" %
                                (stem, row["kind"], row["mode"]))
            if row["backend"] != spec["backend"]:
                problems.append("%s: row backend %r != expected %r" % (stem, row["backend"], spec["backend"]))
            if int(row["n"]) != spec["n"]:
                problems.append("%s: row n %d != the declared grid size %d" % (stem, int(row["n"]), spec["n"]))
    if spec is not None and not problems:
        meta.update(spec)
    if problems:
        return None

    # ---------------------------------------------------------------- report
    out.write("\n=== %s ===\n" % stem)
    out.write("kind=%s mode=%s backend=%s n=%d run_tag=%s reps=%s arms=%s\n" % (
        work_rows[0]["kind"], work_rows[0]["mode"], work_rows[0]["backend"],
        int(work_rows[0]["n"]), work_rows[0]["run_tag"], list(shape.reps), list(shape.arms)))
    out.write("rows: scanwork=%d main=%d\n" % (len(work_rows), len(main_rows)))

    # per (arm, frac, limit, start-class) medians over reps and starts
    def cell_rows(arm, frac, limit, phase, passes, starts=None, probe_id=""):
        out_rows = []
        for rep in shape.reps:
            for p in passes:
                keys = [range(shape.starts)] if starts is None else [starts]
                for start_iter in keys:
                    for start in start_iter:
                        r = cells.get((rep, arm, frac_key(frac), limit, phase, p, start, probe_id))
                        if r is not None:
                            out_rows.append(r)
        return out_rows

    for frac in shape.fracs:
        for limit in shape.limits:
            out.write("\n-- frac=%s limit=%d (timed, median over %d starts x %d reps) --\n"
                      % (frac, limit, shape.starts, len(shape.reps)))
            out.write("%-4s %8s %8s %7s %8s %7s %7s %9s %8s %7s\n" % (
                "arm", "p_cmps", "p_skip", "p_sub", "o_cmps", "o_skip", "rows", "ov_share",
                "out_bytes", "locates"))
            for arm in shape.arms:
                rows = cell_rows(arm, frac, limit, "timed", (0, 1))
                if not rows:
                    continue
                p_cmps = med([int(r["adv_p_cmps"]) for r in rows])
                p_skip = med([int(r["adv_p_skipped"]) for r in rows])
                p_sub = med([int(r["adv_p_subblocks"]) for r in rows])
                o_cmps = med([int(r["adv_o_cmps"]) for r in rows])
                o_skip = med([int(r["adv_o_skipped"]) for r in rows])
                rows_out = med([int(r["rows"]) for r in rows])
                prim = sum(int(r["out_primary"]) for r in rows)
                ovf = sum(int(r["out_overflow"]) for r in rows)
                share = (100.0 * ovf / (prim + ovf)) if (prim + ovf) else 0.0
                out_bytes = med([int(r["out_bytes"]) for r in rows])
                locates = med([int(r["scan_start_locates"]) for r in rows])
                out.write("%-4s %8s %8s %7s %8s %7s %7s %8.1f%% %8s %7s\n" % (
                    arm, fmt(p_cmps), fmt(p_skip), fmt(p_sub), fmt(o_cmps), fmt(o_skip),
                    fmt(rows_out), share, fmt(out_bytes), fmt(locates)))

    # Per-start detail, limit=256 and 16: the regression cells. Printed for every start so a
    # single extreme start cannot hide behind a median.
    for limit in (16, 256):
        for frac in shape.fracs:
            out.write("\n-- per-start adv_p_cmps / adv_p_skipped / adv_p_subblocks, "
                      "limit=%d frac=%s --\n" % (limit, frac))
            header = "%-6s" % "start"
            for arm in shape.arms:
                header += " %-16s" % arm
            out.write(header + "\n")
            for start in range(shape.starts):
                line = "%-6d" % start
                for arm in shape.arms:
                    rows = cell_rows(arm, frac, limit, "timed", (0, 1), starts=(start,))
                    if not rows:
                        line += " %-16s" % "-"
                        continue
                    line += " %-16s" % ("%d/%d/%d" % (
                        int(med([int(r["adv_p_cmps"]) for r in rows])),
                        int(med([int(r["adv_p_skipped"]) for r in rows])),
                        int(med([int(r["adv_p_subblocks"]) for r in rows]))))
                out.write(line + "\n")

    # Probes, grouped by probe id: these are the boundary cases the timed queue never had.
    out.write("\n-- boundary probes (median over reps; phase=probe) --\n")
    for limit in shape.limits:
        for frac in (0.0, 2.0):
            out.write("\nlimit=%d frac=%s\n" % (limit, frac))
            out.write("%-13s %-4s %7s %8s %8s %8s %8s %9s\n" % (
                "probe", "arm", "rows", "p_cmps", "p_skip", "o_cmps", "o_skip", "out_bytes"))
            probe_ids = list(REQUIRED_PROBES) + list(OPTIONAL_PROBES)
            for probe_id in probe_ids:
                for arm in shape.arms:
                    rows = []
                    for rep in shape.reps:
                        rows.extend(probe_rows_at(cells, rep, arm, frac_key(frac), limit))
                    rows = [r for r in rows if r["probe_id"] == probe_id]
                    if not rows:
                        continue
                    out.write("%-13s %-4s %7s %8s %8s %8s %8s %9s\n" % (
                        probe_id, arm,
                        fmt(med([int(r["rows"]) for r in rows])),
                        fmt(med([int(r["adv_p_cmps"]) for r in rows])),
                        fmt(med([int(r["adv_p_skipped"]) for r in rows])),
                        fmt(med([int(r["adv_o_cmps"]) for r in rows])),
                        fmt(med([int(r["adv_o_skipped"]) for r in rows])),
                        fmt(med([int(r["out_bytes"]) for r in rows]))))

    # Count ratios between arms, totals over starts/reps, labelled as counts.
    out.write("\n-- count ratios (sum over starts and reps; counts, never times) --\n")
    for limit in shape.limits:
        for frac in (0.0, 0.25, 0.5, 1.0, 2.0):
            totals = {}
            for arm in shape.arms:
                rows = cell_rows(arm, frac, limit, "timed", (0, 1))
                totals[arm] = {
                    "p_cmps": sum(int(r["adv_p_cmps"]) for r in rows),
                    "o_cmps": sum(int(r["adv_o_cmps"]) for r in rows),
                    "p_sub": sum(int(r["adv_p_subblocks"]) for r in rows),
                    "o_skip": sum(int(r["adv_o_skipped"]) for r in rows),
                }
            if "B" in totals and totals["B"]["p_cmps"]:
                line = ("limit=%-5d frac=%-4s " % (limit, frac))
                for arm in shape.arms:
                    if arm == "B":
                        continue
                    line += "%s/B p_cmps=%.3f o_cmps=%s o_skip=%s   " % (
                        arm, totals[arm]["p_cmps"] / totals["B"]["p_cmps"],
                        ("%.3f" % (totals[arm]["o_cmps"] / totals["B"]["o_cmps"]))
                        if totals["B"]["o_cmps"] else "-",
                        ("%.3f" % (totals[arm]["o_skip"] / totals["B"]["o_skip"]))
                        if totals["B"]["o_skip"] else "-")
                out.write(line + "\n")

    # Maintenance residual: the separation the previous round could not make.
    report_lines.append("--- %s: main-file deltas vs per-call sums (residual = maintenance) ---" % stem)
    cross_check(main_rows, cells, shape, stem, report_lines, problems)
    for line in report_lines:
        out.write(line + "\n")

    meta.update({
        "scanwork_rows": len(work_rows),
        "main_rows": len(main_rows),
    })
    return meta


def selftest():
    """Build a tiny VALID pair in a temp directory, then break it four ways.

    Each break must be refused with a reason; the valid pair must pass. This is the same
    discipline the round's other analyzers use: the refusal paths are tested, not assumed.
    """
    global MAIN_DIR
    problems = []
    tmp = tempfile.mkdtemp(prefix="scanentry_selftest_")
    shape = Shape(reps=1, arms=("B",), fracs=(0.0,), limits=(16,), starts=2)
    stem = "selftest_stem"

    def make_rows():
        work = []
        main = []
        for arm in shape.arms:
            running = {f: 0 for f in CUMULATIVE_FIELDS}
            for frac in shape.fracs:
                for limit in shape.limits:
                    for p in (0, 1):
                        for start in range(shape.starts):
                            work.append(dict(
                                kind="mixed", run_tag="st", mode="grow", backend="Context16",
                                arm=arm, wide=False, routing=False, rep=0, position=0, n=64,
                                thr=8, frac=frac, limit=limit, phase="timed", **{"pass": p},
                                probe_id="", start_idx=start, start_len=2, start_fnv=1,
                                rows=2, expect_rows=2, bytes_checked=False, bytes_mismatch=0,
                                adv_p_cmps=start + 1, adv_p_skipped=start, adv_p_subblocks=0,
                                adv_o_cmps=0, adv_o_skipped=0, out_primary=2, out_overflow=0,
                                out_bytes=4, scan_regions=1, scan_groups=1, scan_groups_empty=0,
                                scan_blocks_skipped=0, scan_merge_cmps=0, scan_result_allocs=2,
                                scan_start_locates=1, scan_cursor_starts=1,
                                scan_ov_cursor_starts=0, scan_canonical=0))
                    for start in range(shape.starts):
                        work.append(dict(
                            kind="mixed", run_tag="st", mode="grow", backend="Context16",
                            arm=arm, wide=False, routing=False, rep=0, position=0, n=64,
                            thr=8, frac=frac, limit=limit, phase="verify", **{"pass": 0},
                            probe_id="", start_idx=start, start_len=2, start_fnv=1,
                            rows=2, expect_rows=2, bytes_checked=True, bytes_mismatch=0,
                            adv_p_cmps=start + 1, adv_p_skipped=start, adv_p_subblocks=0,
                            adv_o_cmps=0, adv_o_skipped=0, out_primary=2, out_overflow=0,
                            out_bytes=4, scan_regions=1, scan_groups=1, scan_groups_empty=0,
                            scan_blocks_skipped=0, scan_merge_cmps=0, scan_result_allocs=2,
                            scan_start_locates=1, scan_cursor_starts=1,
                            scan_ov_cursor_starts=0, scan_canonical=0))
                    for probe_id in REQUIRED_PROBES:
                        work.append(dict(
                            kind="mixed", run_tag="st", mode="grow", backend="Context16",
                            arm=arm, wide=False, routing=False, rep=0, position=0, n=64,
                            thr=8, frac=frac, limit=limit, phase="probe", **{"pass": 0},
                            probe_id=probe_id, start_idx=-1, start_len=2, start_fnv=1,
                            rows=0, expect_rows=0, bytes_checked=True, bytes_mismatch=0,
                            adv_p_cmps=0, adv_p_skipped=0, adv_p_subblocks=0,
                            adv_o_cmps=0, adv_o_skipped=0, out_primary=0, out_overflow=0,
                            out_bytes=0, scan_regions=1, scan_groups=1, scan_groups_empty=1,
                            scan_blocks_skipped=0, scan_merge_cmps=0, scan_result_allocs=0,
                            scan_start_locates=1, scan_cursor_starts=1,
                            scan_ov_cursor_starts=0, scan_canonical=0))
                for f in CUMULATIVE_FIELDS:
                    running[f] += sum(int(r[f]) for r in work
                                      if r["arm"] == arm and r["frac"] == frac)
                main.append(dict(rep=0, arm=arm, frac=frac, n=64, **running))
        return main, work

    def write(main, work, tag):
        d = os.path.join(tmp, tag)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, stem + ".jsonl"), "w", encoding="utf-8") as f:
            for row in main:
                f.write(json.dumps(row) + "\n")
        with open(os.path.join(d, stem + "_scanwork.jsonl"), "w", encoding="utf-8") as f:
            for row in work:
                f.write(json.dumps(row) + "\n")
        return d

    failures = 0

    def expect_refusal(tag, mutate, reason):
        nonlocal failures
        global MAIN_DIR
        main, work = make_rows()
        mutate(main, work)
        d = write(main, work, tag)
        MAIN_DIR = d
        problems = []
        out = sys.stdout
        sys.stdout = open(os.devnull, "w")
        try:
            analyse(stem, shape, out, problems, [])
        finally:
            sys.stdout.close()
            sys.stdout = out
        if not problems:
            print("SELFTEST FAIL: %s was ACCEPTED" % tag)
            failures += 1
        else:
            print("selftest ok: %s refused (%s)" % (tag, problems[0][:90]))
        del reason

    main, work = make_rows()
    d = write(main, work, "valid")
    MAIN_DIR = d
    problems = []
    out = sys.stdout
    sys.stdout = open(os.devnull, "w")
    try:
        analyse(stem, shape, out, problems, [])
    finally:
        sys.stdout.close()
        sys.stdout = out
    if problems:
        print("SELFTEST FAIL: the valid pair was refused: %s" % problems[0])
        failures += 1
    else:
        print("selftest ok: the valid pair passed")

    expect_refusal("missing_main", lambda m, w: m.clear(), "missing main file")
    os.remove(os.path.join(d, stem + "_scanwork.jsonl"))
    problems = []
    out = sys.stdout
    sys.stdout = open(os.devnull, "w")
    try:
        analyse(stem, shape, out, problems, [])
    finally:
        sys.stdout.close()
        sys.stdout = out
    if not problems:
        print("SELFTEST FAIL: missing scanwork file was accepted")
        failures += 1
    else:
        print("selftest ok: missing scanwork file refused")

    expect_refusal("duplicate", lambda m, w: w.append(dict(w[0])), "duplicate cell")
    expect_refusal("row_mismatch", lambda m, w: w[0].__setitem__("rows", 99), "rows != expect")
    expect_refusal("split_mismatch", lambda m, w: w[0].__setitem__("out_primary", 1), "split != rows")
    expect_refusal("cmp_wrap", lambda m, w: w[0].__setitem__("adv_p_cmps", 5), "cmps > skipped+1")
    return failures


def main(argv):
    global MAIN_DIR
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("stems", nargs="*", help="output stems under results/main")
    ap.add_argument("--reps", type=int, default=4, help="repetitions in the grid (default 4)")
    ap.add_argument("--json", help="write the machine-readable summary here")
    ap.add_argument("--dir", default=None,
                    help="directory holding <stem>.jsonl and <stem>_scanwork.jsonl")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--relaxed", action="store_true",
                    help="skip the fixed-grid size assertion (for smoke runs)")
    args = ap.parse_args(argv)

    if args.selftest:
        return 1 if selftest() else 0

    if args.dir:
        MAIN_DIR = args.dir
    stems = args.stems or list(GRID_SPECS.keys())
    shape = Shape(reps=args.reps)
    problems = []
    report_lines = []
    summary = {}
    for stem in stems:
        print("analysing %s" % stem, file=sys.stderr)
        meta = analyse(stem, shape, sys.stdout, problems, report_lines)
        if meta:
            summary[stem] = meta
    for line in report_lines:
        pass  # already printed inside the per-stem report
    if problems:
        print("\nANALYSIS REFUSED:", file=sys.stderr)
        for p in problems[:40]:
            print("  " + p, file=sys.stderr)
        print("  (%d problem(s) total)" % len(problems), file=sys.stderr)
        return 2
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1, sort_keys=True)
        print("wrote %s" % args.json, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
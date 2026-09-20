#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E34 analysis: the O1 prototype's acceptance table.

One file in: results/main/long_o1_{same,grow}.jsonl -- arms C2 / O1 / ART, 5 processes x
3 datasets x 3 repetitions x 4 checkpoints x 2 modes, 1080 records.

The reviewer's acceptance table asks five things, and this script answers them in that
order:

  full erase/replace time        did the movement reduction become an operation gain
  per-operation latency          did it remove the long stalls
  insert / point query / scan    or was the cost moved into another operation
  own space and transient peak   or was it bought with more resident space
  moved entries and bytes        did the mechanism do what it claims

Aggregation, stated once and used everywhere: a cell's per-process value is the median of
the within-repetition paired ratios; the headline is the median of those five per-process
values, and the direction count says how many of the 5 independent processes agree with it.
max/min across the five processes is the noise floor for that cell -- a headline inside its
own noise floor is reported as such, not as a result. Latency percentiles are medians over
the process-repetition cells, which is the right summary for a summary statistic.

A one-line reading:
  cum_ms       the timed span, maintenance only (erase + insert), cycles N*frac
  query_ms     point queries against the live set, OUTSIDE the timed span
  op_pXX_ns    the individual maintenance operations, timed one by one inside the span
               (so the p99 is a real per-operation stall, not a share of the total)
  memory       the index's own resident bytes; ov_bytes is its overflow share
  ov_handle_*  handles (4 B) the ON arm shifted; ov_record_moves_stable must be 0
  ov_shifted_* the LOGICAL displacement, identical in both arms by design

Nothing is written except results/main/e34_analysis.txt.
"""

import json
import os
import statistics as st

DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(DIR, "results", "main", "e34_analysis.txt")

KINDS = ("mixed", "skew", "tranco")
FRACS = (0.25, 0.50, 1.00, 2.00)
TAGS = ("P1", "P2", "P3", "P4", "P5")
ARMS = ("C2", "O1", "ART")
LAT = ("op_p50_ns", "op_p99_ns", "op_p999_ns", "op_max_ns")


def load(mode):
    path = os.path.join(DIR, "results", "main", "long_o1_%s.jsonl" % mode)

    with open(path, "r", encoding="utf-8") as f:
        rows = [json.loads(line) for line in f if line.strip()]

    return rows, sorted({r["rep"] for r in rows})


def index(rows):
    return {(r["kind"], r["mode"], r["rep"], r["arm"], r["run_tag"], r["frac"]): r for r in rows}


def ratio_table(idx, reps, num, den, field, label, out):
    """num/den for one field, paired within a repetition."""
    out.write("\n-- %s %s / %s --\n" % (label, num, den))
    out.write("  %-7s %-5s %-6s %9s %9s %7s   %s\n"
              % ("data", "mode", "frac", "procMed", "max/min", "dir", "per-process medians"))

    for kind in KINDS:
        for mode in ("same", "grow"):
            for frac in FRACS:
                per_process = []

                for tag in TAGS:
                    ratios = []

                    for rep in reps:
                        a = idx.get((kind, mode, rep, num, tag, frac))
                        b = idx.get((kind, mode, rep, den, tag, frac))

                        if a and b and b[field]:
                            ratios.append(a[field] / b[field])

                    if ratios:
                        per_process.append(st.median(ratios))

                if not per_process:
                    continue

                out.write("  %-7s %-6s %5.2f %9.3f %9.3f %5d/%d   %s\n"
                          % (kind, mode, frac, st.median(per_process),
                             max(per_process) / min(per_process),
                             sum(1 for v in per_process if v < 1.0), len(per_process),
                             " ".join("%.3f" % v for v in per_process)))


def latency_table(rows, out):
    out.write("\n-- per-operation latency, ns; median over the process-repetition cells --\n")
    out.write("  p50/p99/p99.9/max are per-OPERATION: every maintenance op is timed\n")
    out.write("  individually inside the span, so these are stalls, not shares of a total.\n")
    out.write("  %-7s %-6s %5s %-4s %9s %9s %10s %11s %9s\n"
              % ("data", "mode", "frac", "arm", "p50", "p99", "p99.9", "max", "samples"))

    for kind in KINDS:
        for mode in ("same", "grow"):
            for frac in (1.00, 2.00):
                for arm in ARMS:
                    cells = [r for r in rows
                             if r["kind"] == kind and r["mode"] == mode
                             and r["frac"] == frac and r["arm"] == arm]

                    if not cells:
                        continue

                    med = lambda k: st.median([c[k] for c in cells])

                    out.write("  %-7s %-6s %5.2f %-4s %9.0f %9.0f %10.0f %11.0f %9.0f\n"
                              % (kind, mode, frac, arm, med("op_p50_ns"), med("op_p99_ns"),
                                 med("op_p999_ns"), med("op_max_ns"), med("op_samples")))


def structural_table(rows, out):
    out.write("\n-- structure and space at the 200%% checkpoint (median over the cells) --\n")
    out.write("  memory is the index's own resident bytes; ov_bytes is its overflow share;\n")
    out.write("  hw/live are the ON arm's store high-water and live counts. 0 for ART means\n")
    out.write("  NOT APPLICABLE: libart exposes no overflow or byte accounting to this adapter.\n")
    out.write("  %-7s %-6s %-4s %12s %12s %12s %9s %9s %11s\n"
              % ("data", "mode", "arm", "memory", "ov_bytes", "overflow", "ov_hdl_M",
                 "ov_rec_M", "hdl_bytes_M"))

    for kind in KINDS:
        for mode in ("same", "grow"):
            for arm in ARMS:
                cells = [r for r in rows
                         if r["kind"] == kind and r["mode"] == mode
                         and r["frac"] == 2.0 and r["arm"] == arm]

                if not cells:
                    continue

                med = lambda k: st.median([c[k] for c in cells])

                out.write("  %-7s %-6s %-4s %12.0f %12.0f %12.0f %9.0f %9.0f %11.0f\n"
                          % (kind, mode, arm, med("memory"), med("ov_bytes"), med("overflow"),
                             med("ov_handle_moves") / 1e6, med("ov_record_moves_stable") / 1e6,
                             med("ov_handle_move_bytes") / 1e6))


def scan_section(out):
    """The scan half of the cost-transfer row, from its own paired sweep.

    Reads results/main/e34_scan_o1.jsonl -- arms S2 / S2O1 / ARR over 3 states and 3 scan
    lengths. S2O1 is S2 plus overflow_stable_store, one switch, so the ratio is the scan's
    price for the stable store.

    The state column matters: `initial` and `insert25` may have little or no overflow, and
    with no overflow the ON arm takes no extra indirection, so a flat ratio there is the
    correct answer rather than a failed measurement. `replace25` is where the containers are
    populated; the program throws if that state's overflow is empty, so it cannot be
    vacuously flat.
    """
    path = os.path.join(DIR, "results", "main", "e34_scan_o1.jsonl")

    out.write("\n-- scan: S2O1 / S2 --\n")

    if not os.path.exists(path):
        out.write("  (results/main/e34_scan_o1.jsonl absent -- run run_scan_o1.py)\n")
        return

    with open(path, encoding="utf-8") as f:
        rows = [json.loads(line) for line in f if line.strip()]

    reps = sorted({r["rep"] for r in rows})
    tags = sorted({r["run_tag"] for r in rows})
    idx = {(r["kind"], r["state"], r["n"], r["rep"], r["run_tag"], r["arm"]): r for r in rows}

    out.write("  %-7s %-9s %5s %9s %9s %7s   %s\n"
              % ("data", "state", "n", "procMed", "max/min", "dir", "per-process medians"))

    for kind in KINDS:
        for state in ("initial", "insert25", "replace25"):
            for n in (16, 128, 1024):
                per_process = []

                for tag in tags:
                    ratios = []

                    for rep in reps:
                        a = idx.get((kind, state, n, rep, tag, "S2O1"))
                        b = idx.get((kind, state, n, rep, tag, "S2"))

                        if a and b and b["ms"]:
                            ratios.append(a["ms"] / b["ms"])

                    if ratios:
                        per_process.append(st.median(ratios))

                if not per_process:
                    continue

                out.write("  %-7s %-9s %5d %9.3f %9.3f %5d/%d   %s\n"
                          % (kind, state, n, st.median(per_process),
                             max(per_process) / min(per_process),
                             sum(1 for v in per_process if v < 1.0), len(per_process),
                             " ".join("%.3f" % v for v in per_process)))


def main():
    same_rows, same_reps = load("same")
    grow_rows, grow_reps = load("grow")
    rows = same_rows + grow_rows

    out = open(OUT, "w", encoding="utf-8")
    w = out.write

    w("%s\nE34 O1 acceptance: the stable overflow store (C2 / O1 / ART)\n%s\n" % ("#" * 78, "#" * 78))
    w("  %d records  |  same: %d reps %s  |  grow: %d reps %s\n"
      % (len(rows), len(same_reps), same_reps, len(grow_reps), grow_reps))
    w("  C2 = the frozen version = the baseline (only the overflow organization differs)\n")
    w("  O1 = C2 + Config::overflow_stable_store, exactly one switch\n")

    idx = index(rows)

    w("\n%s\n## 1. full erase/replace time -- did the reduction become an operation gain?\n%s\n"
      % ("#" * 78, "#" * 78))
    w("  cum_ms is the whole maintenance span (erase + insert) for cycles N*frac.\n")
    ratio_table(idx, same_reps, "O1", "C2", "cum_ms", "cum_ms", out)
    ratio_table(idx, same_reps, "ART", "C2", "cum_ms", "cum_ms (scale reference)", out)

    w("\n%s\n## 2. per-operation latency -- did the long stalls go away?\n%s\n"
      % ("#" * 78, "#" * 78))
    w("  A ratio below 1.0 means O1's stall is shorter. The p99 and p99.9 rows are the\n")
    w("  ones that speak to the stall question; p50 is there to show the typical op.\n")
    ratio_table(idx, same_reps, "O1", "C2", "op_p99_ns", "op_p99", out)
    ratio_table(idx, same_reps, "O1", "C2", "op_p999_ns", "op_p99.9", out)

    latency_table(rows, out)

    w("\n%s\n## 3. insert / point query / scan -- was the cost moved elsewhere?\n%s\n"
      % ("#" * 78, "#" * 78))
    w("  query_ms covers the point queries and sits OUTSIDE the timed span, so it is a\n")
    w("  read-path measurement, not a share of cum_ms.\n")
    ratio_table(idx, same_reps, "O1", "C2", "query_ms", "query_ms", out)

    scan_section(out)

    w("\n%s\n## 4. own space and transient peak -- what did it cost resident?\n%s\n"
      % ("#" * 78, "#" * 78))
    ratio_table(idx, same_reps, "O1", "C2", "memory", "memory", out)
    ratio_table(idx, same_reps, "O1", "C2", "ov_bytes", "ov_bytes (the overflow share)", out)

    w("\n%s\n## 5. moved entries and bytes -- did the mechanism do what it claims?\n%s\n"
      % ("#" * 78, "#" * 78))
    w("  From the diagnostic build (the counters exist only under -DE25_DIAG, and its\n")
    w("  counting cost is asymmetric, so it supplies NO timing). Full table: analyze_o1_mech.py.\n")
    w("  ov_handle_moves is what the ON arm did INSTEAD of moving records;\n")
    w("  ov_record_moves_stable is what it must NOT have done.\n")

    diag = []
    for mode in ("same", "grow"):
        path = os.path.join(DIR, "results", "main", "long_o1_diag_%s.jsonl" % mode)
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                diag += [json.loads(line) for line in f if line.strip()]

    if diag:
        w("\n  %-7s %-6s %-4s %14s %14s %14s %10s\n"
          % ("data", "mode", "arm", "ov_handle_moves", "ov_hdl_bytes", "ov_rec_moves", "hw=live"))
        for kind in KINDS:
            for mode in ("same", "grow"):
                for arm in ("C2", "O1"):
                    cells = [r for r in diag
                             if r["kind"] == kind and r["mode"] == mode
                             and r["frac"] == 2.0 and r["arm"] == arm]
                    if not cells:
                        continue
                    w("  %-7s %-6s %-4s %14d %14d %14d %10s\n"
                      % (kind, mode, arm, cells[0]["ov_handle_moves"],
                         cells[0]["ov_handle_move_bytes"], cells[0]["ov_record_moves_stable"],
                         "yes" if cells[0]["ov_store_high_water"] == cells[0]["ov_store_live_water"] else "NO"))
    else:
        w("\n  (diagnostic files absent -- run run_o1_mech.py)\n")

    out.close()
    print("written: %s" % OUT)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

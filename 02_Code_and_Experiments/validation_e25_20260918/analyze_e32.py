#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E32 analysis: the checklist-gap closure round.

Two measurements, one script:

  results/main/long_hyst_same.jsonl   arms C2 / E31 / E31H / E31P / ART, 5 processes
                                      x 3 datasets x 3 repetitions x 4 checkpoints.
                                      Answers: does the checklist's section 4.3 hysteresis
                                      help (and which half of it hurts), and what are the
                                      per-operation latency percentiles -- the section 9
                                      "high-percentile stall" gap, never measured before.

  results/main/e27s2_scan_time.jsonl  arms S0 / S1 / S2 / ARR, 5 processes x 3 datasets
                                      x 5 repetitions x 3 states x 3 lengths.
                                      Answers: what the section 7.1 scan start cache buys.

Aggregation, stated once: a cell's per-process value is the median of the within-repetition
paired ratios; the headline is the median of those per-process values and the direction
count is how many of the 5 processes agree with it. Percentiles are medians over the
process-repetition cells, which is the right summary for a distribution summary statistic.

Nothing is written except results/main/e32_analysis.txt.
"""

import json
import os
import statistics as st

DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(DIR, "results", "main", "e32_analysis.txt")

KINDS = ("mixed", "skew", "tranco")
FRACS = (0.25, 0.50, 1.00, 2.00)
TAGS = ("P1", "P2", "P3", "P4", "P5")

HYST_ARMS = ("C2", "E31", "E31H", "E31P", "ART")
HYST_PAIRS = (
    ("E31", "C2", "E31 / C2   (does the reclaim help at all)"),
    ("E31H", "C2", "E31H / C2  (gap + state check -- the checklist rule read literally)"),
    ("E31P", "C2", "E31P / C2  (state check only -- the other half)"),
    ("E31P", "E31", "E31P / E31 (the same, against the ungated reclaim)"),
    ("ART", "C2", "ART / C2   (the standing baseline, for scale)"),
)
LAT_FIELDS = ("op_p50_ns", "op_p99_ns", "op_p999_ns", "op_max_ns")


def load(name):
    path = os.path.join(DIR, "results", "main", name)

    with open(path, "r", encoding="utf-8") as f:
        rows = [json.loads(line) for line in f if line.strip()]

    reps = sorted({row["rep"] for row in rows})

    return rows, reps


def hyst_index(rows):
    return {
        (row["kind"], row["rep"], row["arm"], row["run_tag"], row["frac"]): row
        for row in rows
    }


def ratio_table(idx, reps, num, den, label, out):
    out.write("\n-- %s --\n" % label)
    out.write("  %-7s %5s %9s %9s %6s   %s\n"
              % ("data", "frac", "procMed", "max/min", "dir", "per-process medians"))

    for kind in KINDS:
        for frac in FRACS:
            per_process = []

            for tag in TAGS:
                ratios = []

                for rep in reps:
                    a = idx.get((kind, rep, num, tag, frac))
                    b = idx.get((kind, rep, den, tag, frac))

                    if a and b and b["cum_ms"]:
                        ratios.append(a["cum_ms"] / b["cum_ms"])

                if ratios:
                    per_process.append(st.median(ratios))

            if not per_process:
                continue

            out.write("  %-7s %5.2f %9.3f %9.3f %4d/%d   %s\n"
                      % (kind, frac, st.median(per_process),
                         max(per_process) / min(per_process),
                         sum(1 for v in per_process if v < 1.0), len(per_process),
                         " ".join("%.3f" % v for v in per_process)))


def latency_table(rows, out):
    out.write("\n-- per-operation latency, ns (median over the process-repetition cells) --\n")
    out.write("  the maintenance operations are timed INDIVIDUALLY inside the timed span, so\n")
    out.write("  every arm pays the same clock reads; the RATIO stays usable, the absolute\n")
    out.write("  cum_ms of this program is not comparable with a build that has no per-op clock\n")
    out.write("  %-7s %5s %-5s %10s %10s %10s %12s %10s\n"
              % ("data", "frac", "arm", "p50", "p99", "p99.9", "max", "samples"))

    for kind in KINDS:
        for frac in (1.00, 2.00):
            for arm in HYST_ARMS:
                cells = [row for row in rows
                         if row["kind"] == kind and row["frac"] == frac and row["arm"] == arm]

                if not cells:
                    continue

                med = lambda key: st.median([c[key] for c in cells])

                out.write("  %-7s %5.2f %-5s %10.0f %10.0f %10.0f %12.0f %10.0f\n"
                          % (kind, frac, arm, med("op_p50_ns"), med("op_p99_ns"),
                             med("op_p999_ns"), med("op_max_ns"), med("op_samples")))


def hyst_section(out):
    rows, reps = load("long_hyst_same.jsonl")
    idx = hyst_index(rows)

    out.write("%s\n## 1. reclaim hysteresis (checklist 4.3)\n%s\n" % ("#" * 78, "#" * 78))
    out.write("  %d records, arts %s, reps %s\n"
              % (len(rows), sorted({r["arm"] for r in rows}), reps))

    for num, den, label in HYST_PAIRS:
        ratio_table(idx, reps, num, den, label, out)

    out.write("\n-- structural at the 200%% checkpoint (median over the cells) --\n")
    out.write("  %-7s %-5s %10s %12s %12s %12s\n"
              % ("data", "arm", "overflow", "compactions", "memory_MiB", "skipped"))

    for kind in KINDS:
        for arm in HYST_ARMS:
            cells = [r for r in rows
                     if r["kind"] == kind and r["frac"] == 2.0 and r["arm"] == arm]

            if not cells:
                continue

            skipped = st.median([c["reclaim_skipped_gap"] + c["reclaim_skipped_unchanged"]
                                 for c in cells])

            out.write("  %-7s %-5s %10.0f %12.0f %12.2f %12.0f\n"
                      % (kind, arm, st.median([c["overflow"] for c in cells]),
                         st.median([c["compactions"] for c in cells]),
                         st.median([c["memory"] for c in cells]) / 1048576.0,
                         skipped))

    latency_table(rows, out)


def scan_section(out):
    rows, reps = load("e27s2_scan_time.jsonl")

    out.write("\n%s\n## 2. scan start cache (checklist 7.1)\n%s\n" % ("#" * 78, "#" * 78))
    out.write("  %d records, arms %s, reps %s\n"
              % (len(rows), sorted({r["arm"] for r in rows}), reps))

    # (kind, state, n) is the cell; the S0/S1 arms must reproduce E27's direction, and S2 is
    # the new arm.
    cells = sorted({(r["kind"], r["state"], r["n"]) for r in rows})

    idx = {(r["kind"], r["state"], r["n"], r["rep"], r["arm"], r["run_tag"]): r for r in rows}

    for num, den, label in (("S2", "S1", "S2 / S1  (the scan start cache, both arms materialise)"),
                            ("S1", "S0", "S1 / S0  (the E27 result, recomputed here as a check)")):
        out.write("\n-- %s --\n" % label)
        out.write("  %-7s %-10s %5s %9s %9s %6s\n"
                  % ("data", "state", "n", "procMed", "max/min", "dir"))

        for kind, state, n in cells:
            per_process = []

            for tag in TAGS:
                ratios = []

                for rep in reps:
                    a = idx.get((kind, state, n, rep, num, tag))
                    b = idx.get((kind, state, n, rep, den, tag))

                    if a and b and b["ms"]:
                        ratios.append(a["ms"] / b["ms"])

                if ratios:
                    per_process.append(st.median(ratios))

            if not per_process:
                continue

            out.write("  %-7s %-10s %5d %9.3f %9.3f %4d/%d\n"
                      % (kind, state, n, st.median(per_process),
                         max(per_process) / min(per_process),
                         sum(1 for v in per_process if v < 1.0), len(per_process)))


def main():
    with open(OUT, "w", encoding="utf-8") as out:
        out.write("# E32 analysis: closing the optimisation-checklist gaps\n")
        hyst_section(out)
        scan_section(out)

    print("wrote", OUT)


if __name__ == "__main__":
    main()

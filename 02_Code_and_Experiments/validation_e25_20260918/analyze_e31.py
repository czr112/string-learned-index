#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E31 analysis: every number RESULTS_E31.md needs, from the raw records.

Reads results/main/long_e31_{same,grow}.jsonl -- the 4-arm long-replacement sweep
(C1 / C2 / C3 / E31, 5 processes x 3 datasets x 3 repetitions x 4 checkpoints).
Writes results/main/e31_analysis.txt (UTF-8).

Arms, by construction in bench_long.cpp:
  C1  = frozen minus compact_needs_garbage
  C2  = the FROZEN version                       <- the comparison base
  C3  = frozen + compact_repack_budget           <- the E29 mechanism that was rejected
  E31 = frozen + subblock_reclaim                <- the candidate

Aggregation rule, stated because the draft report's caption and its printed values do
not agree: a cell's per-process value is the median of the three WITHIN-REPETITION
paired ratios; the headline is the median of those per-process values, and the
direction count is how many of the 5 processes agree with it. The pooled median over
all 15 process-repetition ratios is printed alongside, because that is what the draft
actually printed. Both are reported for every cell; neither is presented alone.

Also reported, because they are integrity checks the timing run itself carries:
  * query_acc must be IDENTICAL across the four arms of a cell -- the four arms hold
    the same key set, so the fixed query sequence must return the same value+found sum.
    A difference means an arm lost or mis-valued a record, independently of check_e31.
  * cross-process spread (max/min) per cell, which is this project's ratio noise floor.

No index is touched; this script only reads records.
"""
import json, os, statistics as st
from collections import defaultdict

DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(DIR, "results", "main", "e31_analysis.txt")
MODES = ("same", "grow")
KINDS = ("mixed", "skew", "tranco")
FRACS = (0.25, 0.50, 1.00, 2.00)
TAGS = ("P1", "P2", "P3", "P4", "P5")
ARMS = ("C1", "C2", "C3", "E31")
REPS = (0, 1, 2)
MIB = 1024.0 * 1024.0


def load(mode):
    p = os.path.join(DIR, "results", "main", "long_e31_%s.jsonl" % mode)
    return [json.loads(l) for l in open(p, encoding="utf-8") if l.strip()]


def main():
    out = open(OUT, "w", encoding="utf-8")
    w = out.write

    w("# E31 analysis (generated from the raw records by analyze_e31.py)\n")
    w("# arms: C1 / C2=frozen / C3=frozen+compact_repack_budget / E31=frozen+subblock_reclaim\n")

    for mode in MODES:
        rows = load(mode)
        idx = {(r["kind"], r["rep"], r["arm"], r["run_tag"], r["frac"]): r for r in rows}
        w("\n%s\n## mode = %s  (%d records)\n%s\n" % ("#" * 78, mode, len(rows), "#" * 78))

        # ---- bookkeeping -------------------------------------------------
        cells = sorted({(r["kind"], r["frac"]) for r in rows})
        missing = [(k, f, a, t, rep) for (k, f) in cells for a in ARMS for t in TAGS
                   for rep in REPS if (k, rep, a, t, f) not in idx]
        w("\n-- cells --\n")
        w("  expected %d records for 5 procs x 3 data x 3 reps x %d arms x 4 checkpoints; found %d\n"
          % (len(cells) * len(ARMS) * len(TAGS) * len(REPS), len(ARMS), len(rows)))
        w("  missing (data, frac, arm, run_tag, rep) cells: %s\n" % (missing if missing else "none"))

        # ---- integrity: query_acc must agree across arms ----------------
        w("\n-- integrity: query_acc across the four arms of one cell --\n")
        bad = []
        for kind in KINDS:
            for frac in FRACS:
                for t in TAGS:
                    for rep in REPS:
                        v = {a: idx[(kind, rep, a, t, frac)]["query_acc"] for a in ARMS
                             if (kind, rep, a, t, frac) in idx}
                        if len(set(v.values())) > 1:
                            bad.append((kind, frac, t, rep, v))
        w("  cells where the arms disagree on the query answer sum: %d\n" % len(bad))
        for b in bad[:12]:
            w("    %s\n" % (b,))

        # ---- ratio tables ------------------------------------------------
        for num, den, label in ((("E31"), "C2", "E31 / C2 (frozen)"),
                                ("C3", "C2", "C3 / C2 (frozen)"),
                                ("C2", "C1", "C2 / C1"),
                                ("E31", "C3", "E31 / C3")):
            w("\n-- maintenance ratio, %s --\n" % label)
            w("  %-7s %5s %9s %9s %9s %6s   %s\n"
              % ("data", "frac", "procMed", "pooled", "max/min", "dir", "per-process medians"))
            for kind in KINDS:
                for frac in FRACS:
                    per, pooled = [], []
                    for t in TAGS:
                        rr = []
                        for rep in REPS:
                            a = idx.get((kind, rep, num, t, frac))
                            b = idx.get((kind, rep, den, t, frac))
                            if a and b and b["cum_ms"]:
                                rr.append(a["cum_ms"] / b["cum_ms"])
                        if rr:
                            per.append(st.median(rr))
                            pooled.extend(rr)
                    if not per:
                        continue
                    w("  %-7s %5.2f %9.3f %9.3f %9.3f %4d/%d   %s\n"
                      % (kind, frac, st.median(per), st.median(pooled),
                         max(per) / min(per), sum(1 for v in per if v < 1.0), len(per),
                         " ".join("%.3f" % v for v in per)))

        # ---- structural --------------------------------------------------
        w("\n-- structural (median over the 15 process-repetition cells) --\n")
        w("  %-7s %5s %-4s %10s %12s %12s %12s\n"
          % ("data", "frac", "arm", "overflow", "compactions", "memory_B", "memory_MiB"))
        for kind in KINDS:
            for frac in FRACS:
                for arm in ARMS:
                    g = [idx[(kind, rep, arm, t, frac)] for t in TAGS for rep in REPS
                         if (kind, rep, arm, t, frac) in idx]
                    if not g:
                        continue
                    med = lambda k: st.median([x[k] for x in g])
                    w("  %-7s %5.2f %-4s %10.0f %12.0f %12.0f %12.2f\n"
                      % (kind, frac, arm, med("overflow"), med("compactions"),
                         med("memory"), med("memory") / MIB))
            w("\n")

        # ---- query side --------------------------------------------------
        w("-- query side: query_ms at each checkpoint, median over 15 cells --\n")
        w("  %-7s %5s %9s %9s %9s %9s   %9s %6s\n"
          % ("data", "frac", "C1", "C2", "C3", "E31", "E31/C2", "dir"))
        for kind in KINDS:
            for frac in FRACS:
                med = {}
                for arm in ARMS:
                    v = [idx[(kind, rep, arm, t, frac)]["query_ms"] for t in TAGS for rep in REPS
                         if (kind, rep, arm, t, frac) in idx]
                    med[arm] = st.median(v) if v else float("nan")
                per = [st.median([idx[(kind, rep, "E31", t, frac)]["query_ms"] /
                                  idx[(kind, rep, "C2", t, frac)]["query_ms"] for rep in REPS])
                       for t in TAGS]
                w("  %-7s %5.2f %9.3f %9.3f %9.3f %9.3f   %9.3f %4d/%d\n"
                  % (kind, frac, med["C1"], med["C2"], med["C3"], med["E31"],
                     st.median(per), sum(1 for v in per if v < 1.0), len(per)))

        # ---- payback: maintenance cost vs per-batch query saving ---------
        w("\n-- payback: maintenance delta vs the per-batch query saving (E29's accounting) --\n")
        w("  maintenance delta = E31 - C2 (ms, negative = E31 cheaper);\n")
        w("  one batch = the fixed 16384-key query sequence; batches to break even = |delta| / saving\n")
        w("  %-7s %5s %12s %12s %12s %14s\n"
          % ("data", "frac", "maint_delta", "query_saving", "batch", "batches_to_break_even"))
        for kind in KINDS:
            for frac in FRACS:
                m = lambda arm: st.median([idx[(kind, rep, arm, t, frac)]["cum_ms"]
                                           for t in TAGS for rep in REPS])
                q = lambda arm: st.median([idx[(kind, rep, arm, t, frac)]["query_ms"]
                                           for t in TAGS for rep in REPS])
                mdelta = m("E31") - m("C2")
                qsave = q("C2") - q("E31")
                if qsave <= 0:
                    payoff = "never (query not cheaper)"
                else:
                    payoff = "%.1f" % (abs(mdelta) / qsave) if mdelta > 0 else "0 (maintenance already cheaper)"
                w("  %-7s %5.2f %12.2f %12.3f %14s\n"
                  % (kind, frac, mdelta, qsave, payoff))

        # ---- counters the report quotes ----------------------------------
        w("\n-- counters at frac=2.00 (median over 15 cells) --\n")
        for kind in KINDS:
            for arm in ("C2", "E31"):
                g = [idx[(kind, rep, arm, t, 2.0)] for t in TAGS for rep in REPS]
                w("  %-7s %-4s replayed=%9.0f copied=%12.0f garbage=%11.0f helped=%6.0f\n"
                  % (kind, arm, st.median([x["replayed"] for x in g]),
                     st.median([x["copied"] for x in g]),
                     st.median([x["garbage"] for x in g]),
                     st.median([x["helped"] for x in g])))

    out.close()
    print("wrote", OUT)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Do the two long-replacement programs agree in ABSOLUTE terms?

The four-arm run (long_e31_*.jsonl, 3 repetitions, fixed arm order) and the five-arm
run (long_e31art_*.jsonl, 5 repetitions, rotated order, ART present) measure the same
four Config arms on the same workloads. Ratios inside each file are paired and safe;
this script asks the separate question of whether adding the ART arm and rotating the
order moved the Index arms themselves, which would make a cross-file comparison of
absolute times unsafe (and would mean the ART arm's heap churn perturbs its neighbours).

Prints, per (mode, data, frac): median cum_ms per arm in each file, and the ratio
five-arm / four-arm. A common multiplier across all four arms is a program-level
offset; an arm-specific one would be a real problem.

Writes results/main/e31art_program_compare.txt.
"""
import json, os, statistics as st

DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(DIR, "results", "main", "e31art_program_compare.txt")
KINDS = ("mixed", "skew", "tranco")
FRACS = (0.25, 0.50, 1.00, 2.00)
ARMS = ("C1", "C2", "C3", "E31")


def load(mode, tag):
    p = os.path.join(DIR, "results", "main", "long_e31%s_%s.jsonl" % (tag, mode))
    return [json.loads(l) for l in open(p, encoding="utf-8") if l.strip()]


def med(rows, kind, frac, arm, field="cum_ms"):
    v = [r[field] for r in rows if r["kind"] == kind and r["frac"] == frac and r["arm"] == arm]
    return st.median(v) if v else float("nan")


def main():
    out = open(OUT, "w", encoding="utf-8")
    w = out.write
    w("# absolute-time comparison of the two long-replacement programs\n")
    w("# four-arm: 3 reps, fixed order, no ART   |   five-arm: 5 reps, rotated order, ART present\n")
    for mode in ("same", "grow"):
        r4 = load(mode, "")
        r5 = load(mode, "art")
        w("\n%s\n## mode = %s\n%s\n" % ("#" * 74, mode, "#" * 74))
        w("  %-7s %5s %-4s %12s %12s %9s\n"
          % ("data", "frac", "arm", "4arm_ms", "5arm_ms", "5/4"))
        for kind in KINDS:
            for frac in FRACS:
                for arm in ARMS:
                    a = med(r4, kind, frac, arm)
                    b = med(r5, kind, frac, arm)
                    w("  %-7s %5.2f %-4s %12.2f %12.2f %9.3f\n"
                      % (kind, frac, arm, a, b, (b / a) if a else float("nan")))
        w("\n  -- 5/4 per arm, pooled over data and checkpoints --\n")
        for arm in ARMS:
            rr = [med(r5, k, f, arm) / med(r4, k, f, arm)
                  for k in KINDS for f in FRACS if med(r4, k, f, arm)]
            w("     %-4s median=%.3f  min=%.3f  max=%.3f\n"
              % (arm, st.median(rr), min(rr), max(rr)))
        w("\n  -- query_ms 5/4 per arm --\n")
        for arm in ARMS:
            rr = [med(r5, k, f, arm, "query_ms") / med(r4, k, f, arm, "query_ms")
                  for k in KINDS for f in FRACS if med(r4, k, f, arm, "query_ms")]
            w("     %-4s median=%.3f  min=%.3f  max=%.3f\n"
              % (arm, st.median(rr), min(rr), max(rr)))
    out.close()
    print("wrote", OUT)


if __name__ == "__main__":
    main()

# -*- coding: utf-8 -*-
"""G5 2x2 cross-backend analysis: S0 / WIDE / S0L / WIDEL / ART, paired within one repetition.

THE QUESTION. G3 measured A1' (`subblock_reclaim` + `reclaim_widest`) under Context16 only.
A1' fires when a placement fails and the rank-selected sub-block's repack did not rescue it;
then it tries the group's other sub-blocks, dirtiest first. A different mapping groups records
differently, and a better mapping places more records in the primary in the first place -- so
both the opportunity and the geometry change. The 2x2 re-measures the row instead of assuming it.

  M_old = Context16   : S0  -> WIDE   (the G3 row, re-measured in this same round)
  M_ref = LitsHpt     : S0L -> WIDEL
  libart              : ART           (external, same process, same repetition)

THE STATISTICS, identical to G3 so the two rounds read the same way:
  * Five independent processes, five repetitions, arms ROTATED inside a repetition.
  * Every ratio is formed WITHIN ONE REPETITION -- same process, same repetition, both arms.
  * Per cell: median over repetitions per process, median across processes, direction count
    (processes with ratio < 1), and NOISE FLOOR = max/min of the five process medians. A
    headline inside its own noise floor is reported as no change.
  * Ratios < 1 mean the FIRST arm is faster or smaller.

NOT A CHAIN. WIDE/S0 and WIDEL/S0L are two separate paired measurements. Their quotient is a
ratio of medians, not a measurement, and is not reported as one.

Usage:  python3 analyze_lits2x2.py --run-id <TAG>
"""

import argparse
import io
import os
import statistics

import analyze_common as ac

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

ARMS = ["S0", "WIDE", "S0L", "WIDEL", "ART"]

# Ordered from "the mechanism under each mapping" out to the external comparison.
PAIRS = [
    ("WIDE", "S0"),        # A1' effect, M_old
    ("WIDEL", "S0L"),      # A1' effect, M_ref
    ("S0L", "S0"),         # model effect, no A1'
    ("WIDEL", "WIDE"),     # mapping effect, A1' on both sides
    ("WIDE", "ART"),
    ("WIDEL", "ART"),
    ("S0", "ART"),
]

KINDS = ["mixed", "skew", "tranco"]
MODES = ["same", "grow"]
CP = [0.25, 0.50, 1.00, 2.00]

# What each column means, so the tables cannot be read as something else:
#   cum_ms    cumulative index-operation time to this checkpoint (maintenance included)
#   overflow  live overflow records at this checkpoint
#   memory    index bytes as the library accounts them (deterministic, not a timed quantity)
#   query_ms  a fixed query sequence, timed at this checkpoint
METRICS = [
    ("cum_ms", "maintenance + operations, cumulative", True),
    ("overflow", "overflow records", False),
    ("memory", "resident index bytes", False),
    ("query_ms", "fixed query sequence", True),
]

# Diagnostic-build counters. Deterministic per arm, so they get counts, not noise floors.
COUNTERS = ["widened_examined", "widened", "widened_helped"]


def load(prefix, run_id, mode):
    path = os.path.join(MAIN, "%s_%s_%s.jsonl" % (prefix, run_id, mode))

    if not os.path.exists(path):
        return []

    rows = ac.load_records(path, arms=ARMS, arm_field="arm")
    ac.assert_no_duplicate_pairings(rows, arm_field="arm")
    ac.assert_ops_consistent(rows, field="cycles", arm_field="arm")

    return rows


def cell(rows, kind, mode, frac, key, arm):
    out = {}

    for r in rows:
        if (r.get("kind") == kind and r.get("mode") == mode and r.get("arm") == arm
                and abs(r.get("frac", -1) - frac) < 1e-9):
            out[(r.get("run_tag"), r.get("rep"))] = r.get(key)

    return out


def pair_stats(a, b):
    per_proc = {}

    for (tag, rep), va in a.items():
        vb = b.get((tag, rep))
        if vb is None or va is None or vb == 0:
            continue
        per_proc.setdefault(tag, []).append(va / vb)

    meds = {t: statistics.median(v) for t, v in per_proc.items() if v}
    if not meds:
        return None

    vals = sorted(meds.values())

    return {
        "median": statistics.median(vals),
        "min": vals[0],
        "max": vals[-1],
        "below": sum(1 for v in vals if v < 1.0),
        "n": len(vals),
        "noise": (vals[-1] / vals[0]) if vals[0] > 0 else float("inf"),
    }


def fmt(s, label=""):
    if s is None:
        return "%-22s      --" % label

    return ("%-22s %7.3f  (%d/%d below)  range %.3f-%.3f  noise %.3f"
            % (label, s["median"], s["below"], s["n"], s["min"], s["max"], s["noise"]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--prefix", default="long_lits2x2")
    parser.add_argument("--allow-drift", action="store_true",
                        help="analyze records whose source has since changed, as HISTORY")
    parser.add_argument("--counters", action="store_true",
                        help="the records came from the DIAGNOSTIC build, which carries the "
                             "widened-reclaim counters; the timing build does not")
    opts = parser.parse_args()

    ac.assert_source_identity("bench_lits2x2", allow_drift=opts.allow_drift)
    ac.assert_not_counter_build("bench_lits2x2")

    out = []
    w = out.append

    w("G5 2x2 cross-backend   S0 / WIDE / S0L / WIDEL / ART   run_id=%s" % opts.run_id)
    w("ratios within one repetition; per-process medians; direction counted over processes")
    w("WIDE/S0 = A1' under M_old (Context16).  WIDEL/S0L = A1' under M_ref (LitsHpt).")
    w("The two are separate measurements; their quotient is not formed.")
    w("=" * 108)

    for key, meaning, lower_is_better in METRICS:
        w("")
        w("### %s  -- %s" % (key, meaning))
        w("")

        for kind in KINDS:
            for mode in MODES:
                rows = load(opts.prefix, opts.run_id, mode)
                if not rows:
                    continue

                w("  %s / %s" % (kind, mode))

                for frac in CP:
                    head = "    at %3d%%" % int(frac * 100)

                    for a, b in PAIRS:
                        va = cell(rows, kind, mode, frac, key, a)
                        vb = cell(rows, kind, mode, frac, key, b)
                        s = pair_stats(va, vb)
                        if s is None:
                            continue

                        w(head + "  " + fmt(s, "%s/%s" % (a, b)))
                        head = "             "

                for arm in ARMS:
                    v = cell(rows, kind, mode, 2.00, key, arm)
                    vals = [x for x in v.values() if x is not None]
                    if vals:
                        w("             absolute %-5s at 200%%: %.6g" % (arm, statistics.median(vals)))

    w("")
    w("### A1' EFFECT, SIDE BY SIDE at 200%  (two separate paired medians)")
    w("")
    w("  %-10s %-8s %14s %14s %10s" % ("kind", "mode", "M_old WIDE/S0", "M_ref WIDEL/S0L", "keep"))
    w("  " + "-" * 62)

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode)
            if not rows:
                continue

            a = pair_stats(cell(rows, kind, mode, 2.00, "cum_ms", "WIDE"),
                           cell(rows, kind, mode, 2.00, "cum_ms", "S0"))
            b = pair_stats(cell(rows, kind, mode, 2.00, "cum_ms", "WIDEL"),
                           cell(rows, kind, mode, 2.00, "cum_ms", "S0L"))

            if a is None or b is None:
                continue

            # The share of the M_old effect that survives under M_ref. Printed as a ratio of
            # the two paired medians and LABELLED as such -- it inherits the uncertainty of
            # both, and it is not itself a paired quantity.
            keep = (1.0 - b["median"]) / (1.0 - a["median"]) if a["median"] != 1.0 else float("nan")

            w("  %-10s %-8s %14.3f %14.3f %9.0f%%"
              % (kind, mode, a["median"], b["median"], 100.0 * keep))

    w("")
    w("  (\"keep\" = (1 - WIDEL/S0L) / (1 - WIDE/S0). It is a ratio of two paired medians, so")
    w("   it inherits both uncertainties; it is a summary, not a measurement.)")

    # Counters come from the DIAGNOSTIC build only; the timing build does not carry them (see
    # bench_lits2x2.cpp). They are emitted on request rather than on presence, so that a run
    # that has no counter data cannot quietly print a table of zeros.
    have_counters = opts.counters

    if have_counters:
        w("")
        w("### WHY, at 200%  (diagnostic counters -- counts, not timings)")
        w("")
        w("  %-10s %-8s %-6s %10s %10s %10s %10s %10s"
          % ("kind", "mode", "arm", "examined", "widened", "helped", "overflow", "memory"))

        for kind in KINDS:
            for mode in MODES:
                rows = load(opts.prefix, opts.run_id, mode)
                if not rows:
                    continue

                for arm in ("S0", "WIDE", "S0L", "WIDEL"):

                    def med(k, arm=arm, kind=kind, mode=mode, rows=rows):
                        v = cell(rows, kind, mode, 2.00, k, arm)
                        vals = [x for x in v.values() if x is not None]
                        return statistics.median(vals) if vals else None

                    ex, wd, hp = (med(k) for k in COUNTERS)
                    ov, mem = med("overflow"), med("memory")

                    if ov is None:
                        continue

                    w("  %-10s %-8s %-6s %10s %10s %10s %10d %10.2fMB"
                      % (kind, mode, arm,
                         "-" if ex is None else "%d" % ex,
                         "-" if wd is None else "%d" % wd,
                         "-" if hp is None else "%d" % hp,
                         int(ov), mem / (1 << 20)))

    text = "\n".join(out) + "\n"

    with io.open(os.path.join(MAIN, "g5_lits2x2_analysis_%s.txt" % opts.run_id), "w",
                 encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

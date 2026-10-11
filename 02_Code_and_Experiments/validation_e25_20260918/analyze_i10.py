# -*- coding: utf-8 -*-
"""I10 closing round: the old compaction path against the safe one, paired within a repetition.

THE QUESTION. `compact_plan_commit` moves the placement decisions, the capacity proof and the
directory construction to BEFORE the group is touched. The work is the same; only when it
happens changes. So the question is a COST question, and the plan's cost is an extra
O(n * nb) pass over a group's live records -- which should show up in the TAIL and under high
compaction frequency, not in the mean.

  OLD  the shipped configuration            -- compact_plan_commit = false
  NEW  the same configuration + the plan    -- compact_plan_commit = true
  ART  libart, the standing external anchor

THE STATISTICS, identical to the other rounds so they read the same way:
  * Five independent processes, six repetitions, arms ROTATED inside a repetition.
  * Every ratio is formed WITHIN ONE REPETITION.
  * Per cell: median over repetitions per process, median across processes, direction count,
    and NOISE FLOOR = max/min of the five process medians. Inside its own noise floor is
    reported as no change.
  * Ratios < 1 mean the FIRST arm is faster or smaller.

TAIL LATENCY IS REPORTED AS ITSELF, not as a mean: p99 and p99.9 are the reason this
benchmark exists. They come from the per-operation distribution the source already builds.

Usage:  python3 analyze_i10.py --run-id <TAG>
"""

import argparse
import io
import os
import statistics

import analyze_common as ac

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

ARMS = ["OLD", "NEW", "ART"]

PAIRS = [("NEW", "OLD"), ("NEW", "ART"), ("OLD", "ART")]

KINDS = ["mixed", "skew", "tranco"]
MODES = ["same", "grow"]
CP = [0.25, 0.50, 1.00, 2.00]

# The two groups the review asked for: what the plan costs in time, and what it costs in
# latency. `build_ms` covers the construction the review also asked to see.
TIME_METRICS = [
    ("cum_ms", "maintenance + operations, cumulative"),
    ("build_ms", "construction of the loaded index"),
    ("query_ms", "fixed query sequence"),
]

TAIL_METRICS = [
    ("op_p50_ns", "per-operation p50"),
    ("op_p99_ns", "per-operation p99"),
    ("op_p999_ns", "per-operation p99.9"),
    ("op_max_ns", "per-operation maximum"),
]

STRUCTURE = [
    ("memory", "resident index bytes"),
    ("overflow", "overflow records"),
]


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

        # A zero means "not applicable for this arm" (ART has no index bytes), never "zero".
        if vb is None or va is None or vb == 0 or va == 0:
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
        return "%-20s      --" % label

    return ("%-20s %8.3f  (%d/%d below)  range %.3f-%.3f  noise %.3f"
            % (label, s["median"], s["below"], s["n"], s["min"], s["max"], s["noise"]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--prefix", default="long_i10")
    parser.add_argument("--allow-drift", action="store_true")
    opts = parser.parse_args()

    ac.assert_source_identity("bench_i10", allow_drift=opts.allow_drift)
    ac.assert_not_counter_build("bench_i10")

    out = []
    w = out.append

    w("I10 closing round   OLD / NEW / ART   run_id=%s" % opts.run_id)
    w("ratios within one repetition; per-process medians; direction counted over processes")
    w("NEW/OLD = the plan's cost. < 1 means the safe path is FASTER (expected: it is the")
    w("same work, and the plan also removes the per-front-insert directory rebuild).")
    w("=" * 104)

    for title, metrics in (("TIME", TIME_METRICS), ("PER-OPERATION LATENCY", TAIL_METRICS),
                           ("DETERMINISTIC STRUCTURE", STRUCTURE)):
        w("")
        w("#" * 104)
        w("## %s" % title)
        w("#" * 104)

        for key, meaning in metrics:
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
                            s = pair_stats(cell(rows, kind, mode, frac, key, a),
                                           cell(rows, kind, mode, frac, key, b))
                            if s is None:
                                continue

                            w(head + "  " + fmt(s, "%s/%s" % (a, b)))
                            head = "             "

                    for arm in ARMS:
                        v = cell(rows, kind, mode, 2.00, key, arm)
                        vals = [x for x in v.values() if x is not None]
                        if vals:
                            w("             absolute %-4s at 200%%: %.6g"
                              % (arm, statistics.median(vals)))

    # The headline: does the plan cost anything, and if so where?
    w("")
    w("#" * 104)
    w("## HEADLINE at 200%%  (NEW/OLD; < 1 favours the safe path)")
    w("#" * 104)
    w("")
    w("  %-10s %-6s %10s %10s %10s %10s %10s"
      % ("kind", "mode", "cum_ms", "build_ms", "p99", "p99.9", "max"))

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode)
            if not rows:
                continue

            vals = []

            for key in ("cum_ms", "build_ms", "op_p99_ns", "op_p999_ns", "op_max_ns"):
                s = pair_stats(cell(rows, kind, mode, 2.00, key, "NEW"),
                               cell(rows, kind, mode, 2.00, key, "OLD"))
                vals.append("%10.3f" % s["median"] if s else "         -")

            w("  %-10s %-6s %s" % (kind, mode, " ".join(vals)))

    text = "\n".join(out) + "\n"

    with io.open(os.path.join(MAIN, "i10_analysis_%s.txt" % opts.run_id), "w",
                 encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

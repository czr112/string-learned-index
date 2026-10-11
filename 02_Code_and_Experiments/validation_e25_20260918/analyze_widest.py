# -*- coding: utf-8 -*-
"""G3/A1' analysis: S0 / E31 / WIDE / ART, paired within one repetition.

THE STATISTICS, in one place so the tables below can be read against them:

  * Five independent processes. Each runs four repetitions. The arms are ROTATED inside a
    repetition, so no arm is systematically first (coldest) or last (warmest).
  * Every ratio is formed WITHIN ONE REPETITION -- the same process, the same repetition, the
    two arms of that pair.
  * Reported per cell: the median over repetitions of each process's ratio, the median across
    the five processes, the direction count (how many processes put the ratio below 1), and the
    NOISE FLOOR -- max/min of the five process medians. A headline inside its own noise floor
    is reported as no change.
  * Ratios < 1 mean the FIRST arm is faster or smaller.

THE ARMS ARE NOT A CHAIN. S0 -> E31 is `subblock_reclaim`; E31 -> WIDE is `reclaim_widest`.
Both are reported, and E31/S0 x WIDE/E31 is NOT presented as WIDE/S0 -- multiplying medians of
ratios is not a measurement of the round.

WHAT THIS ROUND MEASURES: the long-replacement workload out to 200% of N with the live count
held constant. Cumulative operation time, overflow records, resident bytes, and a fixed query
sequence. It does NOT measure tail latency -- this benchmark has no timing instrumentation.

Usage:  python3 analyze_widest.py --run-id <TAG>
"""

import argparse
import io
import os
import statistics

import analyze_common as ac

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

ARMS = ["S0", "E31", "WIDE", "ART"]
PAIRS = [("E31", "S0"), ("WIDE", "E31"), ("WIDE", "S0"), ("WIDE", "ART"), ("S0", "ART")]

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
    parser.add_argument("--prefix", default="long_widest")
    parser.add_argument("--allow-drift", action="store_true",
                        help="analyze records whose source has since changed, as HISTORY")
    opts = parser.parse_args()

    ac.assert_source_identity("bench_widest", allow_drift=opts.allow_drift)
    ac.assert_not_counter_build("bench_widest")

    out = []
    w = out.append

    w("G3/A1'  S0 / E31 / WIDE / ART   run_id=%s" % opts.run_id)
    w("ratios within one repetition; per-process medians; direction counted over processes")
    w("E31/S0 = the reclaim itself.  WIDE/E31 = widening its trigger surface.  NOT multiplied.")
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

                        # The baseline pair gets the absolute numbers next to it, because a
                        # ratio alone cannot be read against a different round.
                        w(head + "  " + fmt(s, "%s/%s" % (a, b)))
                        head = "             "

                # Absolute values at the last checkpoint, for the two arms that matter here.
                for arm in ("S0", "E31", "WIDE", "ART"):
                    v = cell(rows, kind, mode, 2.00, key, arm)
                    vals = [x for x in v.values() if x is not None]
                    if vals:
                        w("             absolute %-5s at 200%%: %.6g" % (arm, statistics.median(vals)))

    # The structural columns, which are counters rather than times and are reported as such.
    w("")
    w("### STRUCTURE at 200%  (counts and bytes, not timings -- no noise-floor language)")
    w("")

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode)
            if not rows:
                continue

            cells = []

            for arm in ARMS:
                ov = cell(rows, kind, mode, 2.00, "overflow", arm)
                mem = cell(rows, kind, mode, 2.00, "memory", arm)
                ovs = [x for x in ov.values() if x is not None]
                mems = [x for x in mem.values() if x is not None]

                if ovs and mems:
                    cells.append("%s ov=%d mem=%.3fMB"
                                 % (arm, int(statistics.median(ovs)),
                                    statistics.median(mems) / (1 << 20)))

            w("  %s / %s: %s" % (kind, mode, "   ".join(cells)))

    text = "\n".join(out) + "\n"

    with io.open(os.path.join(MAIN, "g3_widest_analysis_%s.txt" % opts.run_id), "w",
                 encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

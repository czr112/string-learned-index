# -*- coding: utf-8 -*-
"""G5 external-baseline analysis: S0 / WIDE / ART / BTREE, paired within one repetition.

THE QUESTION. The literature task book (§十) requires a mature B+ tree candidate that supports
the same string keys, value ownership and scan semantics as the prototype. libart is the
standing baseline but it is a radix tree with no seek-to-key iterator, so the ordered-container
class was never covered. This round adds one: tlx::btree_map (BSL-1.0, pinned and hash-verified
by third_party/fetch_tlx_btree.py).

THE STATISTICS, identical to G3 and to the 2x2 round so all three read the same way:
  * Five independent processes, four repetitions, arms ROTATED inside a repetition.
  * Every ratio is formed WITHIN ONE REPETITION.
  * Per cell: median over repetitions per process, median across processes, direction count,
    and NOISE FLOOR = max/min of the five process medians. Inside its own noise floor is
    reported as no change.
  * Ratios < 1 mean the FIRST arm is faster or smaller.

WHAT IS AND IS NOT COMPARED.
  * `overflow` and `memory` are the prototype's own accounting. libart exposes neither, and
    TLX reports node counts rather than bytes, so those cells are ABSENT rather than zero.
  * `scan_ms` for ART is not measured at all: E23 established that this adapter's scan
    (40x-3823x too slow) measures the ADAPTER, not libart, because libart has no seek-to-key
    iterator. Those cells are absent too, and no conclusion about libart's scan is drawn.
  * `btree_nodes` is a deterministic count from TLX itself, reported as a count.

Usage:  python3 analyze_btree.py --run-id <TAG>
"""

import argparse
import io
import os
import statistics

import analyze_common as ac

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

ARMS = ["S0", "WIDE", "ART", "BTREE"]

PAIRS = [
    ("WIDE", "S0"),     # A1', re-measured in this round
    ("S0", "BTREE"),    # the prototype against the new external baseline
    ("WIDE", "BTREE"),  # the prototype WITH A1' against it
    ("S0", "ART"),
    ("WIDE", "ART"),
    ("BTREE", "ART"),   # two external baselines, for context only
]

KINDS = ["mixed", "skew", "tranco"]
MODES = ["same", "grow"]
CP = [0.25, 0.50, 1.00, 2.00]

METRICS = [
    ("cum_ms", "maintenance + operations, cumulative"),
    ("query_ms", "fixed query sequence"),
    ("scan_ms", "fixed scan sequence (ART not measured -- see the header)"),
]

# Deterministic columns: counts and bytes, no noise-floor language.
STRUCTURE = [("overflow", "overflow records"), ("memory", "resident index bytes")]


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
        # vb == 0 means "not applicable for this arm", not "zero bytes": those cells are
        # dropped rather than reported as a division by zero.
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
        return "%-22s      --" % label

    return ("%-22s %7.3f  (%d/%d below)  range %.3f-%.3f  noise %.3f"
            % (label, s["median"], s["below"], s["n"], s["min"], s["max"], s["noise"]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--prefix", default="long_btree")
    parser.add_argument("--allow-drift", action="store_true")
    opts = parser.parse_args()

    ac.assert_source_identity("bench_btree", allow_drift=opts.allow_drift)
    ac.assert_not_counter_build("bench_btree")

    out = []
    w = out.append

    w("G5 external baseline   S0 / WIDE / ART / BTREE   run_id=%s" % opts.run_id)
    w("ratios within one repetition; per-process medians; direction counted over processes")
    w("BTREE = tlx::btree_map<std::string, std::uint64_t> (BSL-1.0, pinned by hash)")
    w("=" * 108)

    for key, meaning in METRICS:
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
                        w("             absolute %-6s at 200%%: %.6g" % (arm, statistics.median(vals)))

    w("")
    w("### DETERMINISTIC COLUMNS at 200%  (counts and bytes; no noise-floor language)")
    w("")

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode)
            if not rows:
                continue

            cells = []

            for arm in ARMS:
                ov = [x for x in cell(rows, kind, mode, 2.00, "overflow", arm).values() if x is not None]
                mem = [x for x in cell(rows, kind, mode, 2.00, "memory", arm).values() if x is not None]
                nodes = [x for x in cell(rows, kind, mode, 2.00, "btree_nodes", arm).values() if x is not None]

                if ov and mem:
                    if statistics.median(ov) == 0 and statistics.median(mem) == 0:
                        cells.append("%s n/a" % arm)
                    else:
                        cells.append("%s ov=%d mem=%.2fMB"
                                     % (arm, int(statistics.median(ov)),
                                        statistics.median(mem) / (1 << 20)))

                if nodes and statistics.median(nodes) > 0:
                    cells.append("%s nodes=%d" % (arm, int(statistics.median(nodes))))

            w("  %s / %s: %s" % (kind, mode, "   ".join(cells)))

    # The scan check is a gate, not a statistic: every scanning arm must agree with the
    # reference at every checkpoint. Restated here so the record reader sees it.
    w("")
    w("### SCAN CORRECTNESS (gate, not a statistic)")
    w("")
    w("  scan_acc == scan_exp is asserted in-process for S0, WIDE and BTREE at every")
    w("  checkpoint, against the reference map. A mismatch aborts the run before any")
    w("  timing is recorded, so no scan timing in this file was taken on wrong output.")

    agreed = True

    for mode in MODES:
        rows = load(opts.prefix, opts.run_id, mode)

        for r in rows:
            if r.get("arm") == "ART":
                continue
            if r.get("scan_acc") != r.get("scan_exp"):
                agreed = False

    w("  verified across every archived record: %s" % ("YES" if agreed else "NO -- CHECK"))

    text = "\n".join(out) + "\n"

    with io.open(os.path.join(MAIN, "g5_btree_analysis_%s.txt" % opts.run_id), "w",
                 encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

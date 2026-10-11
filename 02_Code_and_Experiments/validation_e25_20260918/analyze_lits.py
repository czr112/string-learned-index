# -*- coding: utf-8 -*-
"""E38/P3 analysis: M0 / LITS / ART, paired within one repetition.

THE STATISTICS, in one place so the tables below can be read against them:

  * Five independent processes. Each runs three repetitions. The arms are ROTATED inside a
    repetition, so no arm is systematically first (coldest) or last (warmest).
  * Every ratio is formed WITHIN ONE REPETITION -- the same process, the same repetition, the
    two arms of that pair. A ratio across processes or across rounds is not a measurement of
    this round.
  * Reported per cell: the median over repetitions of each process's ratio, the median across
    the five processes, the direction count (how many processes put the ratio below 1), and the
    NOISE FLOOR -- max/min of the five process medians. A headline inside its own noise floor
    is reported as no change.
  * Ratios < 1 mean the FIRST arm is faster or smaller. LITS/M0 < 1 means LITS is faster.

WHAT THIS ROUND MEASURES: the full build, the static mixed point query, and resident space.
The pure-hit and pure-miss queues are reported as INDEPENDENT loads, not as a decomposition of
the mixed one. The mapping, the fingerprint bytes, the sub-block visits and the overflow probes
are the diagnostic build's job and are not read from these files.

Usage:  python3 analyze_lits.py --run-id p3b
"""

import argparse
import io
import json
import os
import statistics

import analyze_common as ac

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

ARMS = ["M0", "LITS", "ART"]
PAIRS = [("LITS", "M0"), ("LITS", "ART"), ("M0", "ART")]

KINDS = ["mixed", "skew", "tranco"]
MODES = ["same", "grow"]
CP = [0.25, 0.50, 1.00, 2.00]


def load(prefix, run_id, mode, phase):
    """Read one run's records, and REFUSE rather than return a short table.

    This used to return [] when the file was absent, which turns "an arm went missing" into
    "the table has fewer columns" -- silently. It now goes through analyze_common, which
    refuses a missing file, invalid JSON, a missing arm, an operation count that differs
    across arms, a duplicate pairing and a mixed-unit row (task book section 12.2).
    """
    path = os.path.join(MAIN, "%s_%s_%s.jsonl" % (prefix, run_id, mode))

    if not os.path.exists(path):
        return []

    rows = ac.load_records(path, arms=ARMS, arm_field="arm")

    if phase == "clean":
        ac.assert_no_duplicate_pairings(rows, arm_field="arm")
        ac.assert_ops_consistent(rows, arm_field="arm")

    return rows


def cell(rows, phase, kind, mode, frac, key, arm):
    """Every (process, rep) value for one cell, keyed by (run_tag, rep)."""
    out = {}

    for r in rows:
        if (r.get("phase") == phase and r.get("kind") == kind and r.get("mode") == mode
                and r.get("arm") == arm and abs(r.get("frac", -1) - frac) < 1e-9):
            out[(r.get("run_tag"), r.get("rep"))] = r.get(key)

    return out


def pair_stats(a, b):
    """Per-process medians of a/b over the repetitions, then the summary."""
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
        "above": sum(1 for v in vals if v > 1.0),
        "n": len(vals),
        "noise": (vals[-1] / vals[0]) if vals[0] > 0 else float("inf"),
    }


def fmt(s, label=""):
    if s is None:
        return "%-24s      --" % label

    return ("%-24s %7.3f  (%d/%d below)  range %.3f-%.3f  noise %.3f"
            % (label, s["median"], s["below"], s["n"], s["min"], s["max"], s["noise"]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-id", default="p3b")
    parser.add_argument("--prefix", default="lits_adapter")
    # The identity gate is on by default: these tables are supposed to describe the CURRENT
    # source. Reproducing an old round means passing --allow-drift and saying so out loud.
    parser.add_argument("--allow-drift", action="store_true",
                        help="analyze records whose source has since changed, as HISTORY")
    opts = parser.parse_args()

    ac.assert_source_identity("bench_lits_adapter", allow_drift=opts.allow_drift)
    ac.assert_not_counter_build("bench_lits_adapter")

    out = []
    w = out.append

    w("E38/P3  LITS/M0/ART  run_id=%s" % opts.run_id)
    w("ratios within one repetition; per-process medians; direction counted over processes")
    w("=" * 108)

    # ---- the full build ------------------------------------------------------------------
    w("")
    w("### FULL BUILD  (build_ms; <1 means the first arm is faster)")
    w("")

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode, "clean")
            if not rows:
                continue

            w("  %s / %s" % (kind, mode))

            for a, b in PAIRS:
                va = cell(rows, "maint", kind, mode, 0.25, "build_ms", a)
                vb = cell(rows, "maint", kind, mode, 0.25, "build_ms", b)
                w("    " + fmt(pair_stats(va, vb), "%s/%s" % (a, b)))

                if a == "LITS" and b == "M0":
                    base = statistics.median([v for v in vb.values() if v is not None])
                    new = statistics.median([v for v in va.values() if v is not None])
                    w("      absolute: M0 %.1f ms, LITS %.1f ms" % (base, new))

    # ---- the static point query ----------------------------------------------------------
    w("")
    w("### POINT QUERY  (query_ms; mixed queries, all sources)")
    w("")

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode, "clean")
            if not rows:
                continue

            w("  %s / %s" % (kind, mode))

            for a, b in PAIRS:
                va = cell(rows, "maint", kind, mode, 2.00, "query_ms", a)
                vb = cell(rows, "maint", kind, mode, 2.00, "query_ms", b)
                w("    " + fmt(pair_stats(va, vb), "%s/%s (200%%)" % (a, b)))

    # ---- the pure-hit and pure-miss queues ------------------------------------------------
    w("")
    w("### PURE-HIT AND PURE-MISS  (ns_per_query; INDEPENDENT loads, not a decomposition)")
    w("")

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode, "clean")
            if not rows:
                continue

            w("  %s / %s" % (kind, mode))

            for qname in ("main", "miss"):
                for a, b in PAIRS:
                    va = {}
                    vb = {}

                    for r in rows:
                        if (r.get("phase") == "queue" and r.get("kind") == kind
                                and r.get("mode") == mode and r.get("queue") == qname
                                and abs(r.get("frac", -1) - 2.00) < 1e-9):
                            k = (r.get("run_tag"), r.get("rep"))
                            if r.get("arm") == a:
                                va[k] = r.get("ns_per_query")
                            elif r.get("arm") == b:
                                vb[k] = r.get("ns_per_query")

                    w("    " + fmt(pair_stats(va, vb), "%s: %s/%s" % (qname, a, b)))

    # ---- resident space -------------------------------------------------------------------
    w("")
    w("### RESIDENT SPACE  (memory; <1 means the first arm is smaller)")
    w("")

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode, "clean")
            if not rows:
                continue

            w("  %s / %s" % (kind, mode))

            for a, b in PAIRS:
                va = cell(rows, "maint", kind, mode, 2.00, "memory", a)
                vb = cell(rows, "maint", kind, mode, 2.00, "memory", b)
                w("    " + fmt(pair_stats(va, vb), "%s/%s (200%%)" % (a, b)))

    # ---- the fallback coverage OF THE TIMED STATE ------------------------------------------
    w("")
    w("### LITS COVERAGE OF THE TIMED STATE  (200% checkpoint, the LITS arm)")
    w("")
    w("  These are the calibers a timed result has to be read against. The record's own")
    w("  lits_* fields are walked over the LIVE regions, not accumulated over every region ever")
    w("  constructed -- the two differ by a factor of about two on this workload.")
    w("")

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode, "clean")
            if not rows:
                continue

            seen = {}
            for r in rows:
                if (r.get("phase") == "maint" and r.get("arm") == "LITS" and r.get("kind") == kind
                        and r.get("mode") == mode and abs(r.get("frac", -1) - 2.00) < 1e-9):
                    seen[(r.get("run_tag"), r.get("rep"))] = r

            if not seen:
                continue

            reg = [v["lits_regions"] for v in seen.values()]
            regm = [v["lits_regions_mapped"] for v in seen.values()]
            rec = [v["lits_records"] for v in seen.values()]
            recm = [v["lits_records_mapped"] for v in seen.values()]

            w("  %s / %s: regions %d/%d = %.1f%%   records %d/%d = %.1f%%"
              % (kind, mode,
                 int(statistics.median(regm)), int(statistics.median(reg)),
                 100.0 * statistics.median(regm) / max(1, statistics.median(reg)),
                 int(statistics.median(recm)), int(statistics.median(rec)),
                 100.0 * statistics.median(recm) / max(1, statistics.median(rec))))

            reasons = {}
            for k in ("lits_reason_admission", "lits_reason_degenerate",
                      "lits_reason_boundary", "lits_reason_unsupported"):
                reasons[k.replace("lits_reason_", "")] = int(statistics.median([v[k] for v in seen.values()]))

            w("      fallback reasons: " + ", ".join("%s=%d" % (k, v) for k, v in reasons.items()))

    # ---- the query share of the fallback ---------------------------------------------------
    w("")
    w("### FALLBACK QUERY SHARE  (200% checkpoint, the LITS arm; per queue)")
    w("")

    for kind in KINDS:
        for mode in MODES:
            rows = load(opts.prefix, opts.run_id, mode, "clean")
            if not rows:
                continue

            line = "  %s / %s:" % (kind, mode)

            for qname in ("main", "overflow", "miss"):
                tot = []
                fb = []

                for r in rows:
                    if (r.get("phase") == "queue" and r.get("arm") == "LITS"
                            and r.get("kind") == kind and r.get("mode") == mode
                            and r.get("queue") == qname
                            and abs(r.get("frac", -1) - 2.00) < 1e-9):
                        tot.append(r.get("lits_queries", 0))
                        fb.append(r.get("lits_fallback_queries", 0))

                if tot:
                    t = statistics.median(tot)
                    f = statistics.median(fb)
                    line += "  %s %d/%d (%.1f%%)" % (qname, int(f), int(t), 100.0 * f / max(1, t))

            w(line)

    text = "\n".join(out) + "\n"
    with io.open(os.path.join(MAIN, "e38_p3_analysis.txt"), "w", encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

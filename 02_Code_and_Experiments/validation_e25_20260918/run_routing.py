#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Driver and analyzer for the compact-region-routing A/B.

PROTOCOL. Five independent processes, four repetitions each, the four arms rotating position
within every repetition so no arm is systematically first or last. Ratios are formed WITHIN
one repetition -- on the same process, the same machine state and the same query sequence --
and the reported figure is the median of those per-repetition ratios. Direction is counted
over the ratios, and the noise floor is max/min of the per-process medians, because a
difference smaller than that is not a difference this device can see.

WHY THE POSITION ROTATION MATTERS HERE. The candidate changes how a key is routed, and the
cost it removes is a cache effect. A cache effect is exactly the kind of thing that depends on
what ran immediately before, so an arm that always ran first would be measured against a
colder cache than the arm after it. Rotating is not decoration.

WHAT THE ANALYZER REFUSES TO DO. It does not compute a ratio between two arms' absolute
medians from different repetitions, and it does not multiply or chain ratios across runs. It
also does not read a win out of a difference that sits inside the noise floor -- that case is
reported as unresolved, in those words.

Usage:  python3 run_routing.py [--procs 5] [--reps 4] [--out routing_grow]
"""

import argparse
import collections
import json
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")
TR = "../validation_e5_20260913/data/tranco_XN6XN.csv"

# The corpora and the key counts this round uses. tranco is the 262k set because that is what
# the earlier rounds used and what the frozen threshold was tuned on.
CORPORA = [("mixed", 131072), ("skew", 131072), ("tranco", 262144)]
ARMS = ["C16-off", "C16-on", "LITS-off", "LITS-on"]


def run(procs, reps, out_name, mode, kind, n, seed_tag):
    # The bench appends to ONE file per output stem, and every process writes into it. The
    # stem is passed through rather than derived from the mode, so a re-measurement lands in
    # a new file instead of overwriting the records a previous conclusion rests on.
    path = os.path.join(MAIN, "%s.jsonl" % out_name)

    for p in range(1, procs + 1):
        tag = "%s-p%d-%s" % (seed_tag, p, kind)
        args = [os.path.join(HERE, "build", "bench_routing.exe"), kind, str(n), str(reps), TR,
                tag, mode, out_name]
        result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)

        if result.returncode != 0:
            print("process %d FAILED: %s" % (p, result.stderr.strip()[:300]))
            return None

    rows = []

    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()

            if line:
                rows.append(json.loads(line))

    return rows


def per_process_median(rows, kind, arm, metric):
    """The median over the repetitions of ONE process. The unit the direction count is over."""
    by_proc = collections.defaultdict(list)

    for r in rows:
        if r["kind"] == kind and r["arm"] == arm:
            by_proc[r["run_tag"].split("-")[0] + "-" + r["run_tag"].split("-")[1]].append(
                r[metric])

    return {p: statistics.median(v) for p, v in by_proc.items()}


def paired_ratios(rows, kind, base, cand, metric):
    """Per-process median of the within-repetition ratio. Pairing is by (process, rep)."""
    pairs = collections.defaultdict(dict)

    for r in rows:
        if r["kind"] != kind:
            continue

        if r["arm"] not in (base, cand):
            continue

        pairs[(r["run_tag"], r["rep"])][r["arm"]] = r[metric]

    by_proc = collections.defaultdict(list)

    for (tag, _rep), v in pairs.items():
        if base in v and cand in v and v[base] > 0:
            by_proc[tag.split("-")[1]].append(v[cand] / v[base])

    return {p: statistics.median(v) for p, v in by_proc.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--procs", type=int, default=5)
    ap.add_argument("--reps", type=int, default=4)
    ap.add_argument("--out", default="routing_grow",
                    help="output stem; must be a NEW name when re-measuring")
    ap.add_argument("--mode", default="grow")
    ap.add_argument("--analyze-only", action="store_true")
    opts = ap.parse_args()

    path = os.path.join(MAIN, "%s.jsonl" % opts.out)

    if not opts.analyze_only:
        if os.path.exists(path):
            print("refusing to overwrite existing evidence: %s" % path)
            return 2

        for kind, n in CORPORA:
            got = run(opts.procs, opts.reps, opts.out, opts.mode, kind, n, "rt")

            if got is None:
                return 2

            print("%-8s processes=%d reps=%d" % (kind, opts.procs, opts.reps))

    if not os.path.exists(path):
        print("no records at %s" % path)
        return 2

    with open(path, encoding="utf-8") as f:
        rows = [json.loads(l) for l in f if l.strip()]

    print()
    print("=" * 100)
    print("COMPACT REGION ROUTING  --  candidate / baseline, within-repetition paired ratios")
    print("  <1 means the candidate is faster.  noise = max/min of the per-process medians.")
    print("=" * 100)

    for metric, label in [("static_ns", "lookup -- STATIC phase (initial keys, mixed hit/miss)"),
                          ("post_ns", "lookup -- POST-UPDATE phase (live keys, mixed hit/miss)"),
                          ("maint_ns", "maintenance"),
                          ("build_ns", "build"),
                          ("index_bytes", "index bytes"),
                          ("routing_bytes", "routing metadata bytes")]:
        print("\n%s" % label)
        print("  %-8s %-18s %10s %8s %16s %9s  %s"
              % ("corpus", "arm pair", "median", "faster", "range", "noise", "reading"))
        print("  " + "-" * 94)

        for kind, _n in CORPORA:
            # The routing metadata has no baseline to pair against -- the switch-off arm
            # reports zero -- so it is reported as an absolute candidate figure rather than
            # as a ratio. Dividing by zero and then filtering the result is how this row came
            # out empty in the first run.
            if metric == "routing_bytes":
                cand_bytes = [r["routing_bytes"] for r in rows
                              if r["kind"] == kind and r["arm"] == "C16-on"]
                regions = [r["regions"] for r in rows if r["kind"] == kind and r["arm"] == "C16-on"]

                if cand_bytes:
                    print("  %-8s %-18s %10d %8s %16s %9s  %s"
                          % (kind, "candidate (absolute)", statistics.median(cand_bytes), "-", "-",
                             "-", "%d regions, %.1f bytes/region"
                             % (statistics.median(regions),
                                statistics.median(cand_bytes) / max(1, statistics.median(regions)))))
                continue

            for base, cand in [("C16-off", "C16-on"), ("LITS-off", "LITS-on")]:
                d = paired_ratios(rows, kind, base, cand, metric)

                if not d:
                    continue

                vals = sorted(d.values())
                med = statistics.median(vals)
                faster = sum(1 for v in vals if v < 1.0)
                n = len(vals)
                noise = (max(vals) / min(vals)) if min(vals) > 0 else float("inf")

                if max(vals) == min(vals) == 1.0:
                    # Not "slower" -- exactly equal, which is what a size that does not
                    # depend on the switch should be. Labelling this "SLOWER" was the first
                    # version's mistake and it read as a regression.
                    reading = "IDENTICAL (the switch does not change this size)"
                elif metric.endswith("_bytes") and abs(med - 1.0) < 0.005:
                    # The candidate's own metadata showing up in a total four orders of
                    # magnitude larger: a few hundred bytes against tens of megabytes. This
                    # branch is guarded on the SIZE metrics on purpose -- an earlier version
                    # let it fire for timings too, and a timing ratio of 1.001 got labelled
                    # "the metadata itself", which says nothing about a duration.
                    reading = ("negligible (%.4f%%, the metadata itself)"
                               % ((med - 1.0) * 100.0))
                elif abs(med - 1.0) < (noise - 1.0):
                    reading = "UNRESOLVED (inside the noise floor)"
                elif faster >= n - 1:
                    reading = "candidate faster, direction %d/%d" % (faster, n)
                elif faster <= 1:
                    reading = "candidate SLOWER, direction %d/%d" % (faster, n)
                else:
                    reading = "mixed direction %d/%d" % (faster, n)

                print("  %-8s %-18s %10.3f %8s %16s %9.3f  %s"
                      % (kind, "%s/%s" % (cand, base), med,
                         "%d/%d" % (faster, n),
                         "%.3f-%.3f" % (vals[0], vals[-1]), noise, reading))

    # ------------------------------------------------------------------ evidence checks
    # These are not decoration. The first version of this driver was accepted on a checksum
    # that matched across arms -- which it did, because EVERY arm was missing every key. A
    # cross-arm agreement check cannot see a workload that went degenerate for all arms at
    # once, so the hit counts and the per-arm probe hashes are checked against the EXPECTED
    # values instead.
    print("\n" + "=" * 100)
    print("EVIDENCE CHECKS (a cross-arm checksum match is NOT one of these)")
    print("=" * 100 + "\n")
    print("  %-8s %-10s %10s %10s %10s %10s %12s"
          % ("corpus", "phase", "expected", "observed", "mismatch", "probe_hash", "verdict"))
    print("  " + "-" * 88)

    problems = 0

    for kind, _n in CORPORA:
        for phase, exp_key, hit_key, mis_key, hash_key in [
            ("static", "static_expected_hits", "static_hits", "static_mismatches",
             "static_probe_hash"),
            ("post", "post_expected_hits", "post_hits", "post_mismatches", "post_probe_hash"),
        ]:
            exp = {r[exp_key] for r in rows if r["kind"] == kind}
            obs = {r[hit_key] for r in rows if r["kind"] == kind}
            mis = sum(r[mis_key] for r in rows if r["kind"] == kind)
            hashes = {r[hash_key] for r in rows if r["kind"] == kind}

            ok = (mis == 0 and len(exp) == 1 and len(obs) == 1 and len(hashes) == 1
                  and list(exp)[0] > 0 and list(exp)[0] == list(obs)[0])

            if not ok:
                problems += 1

            print("  %-8s %-10s %10s %10s %10s %12s  %s"
                  % (kind, phase,
                     list(exp)[0] if len(exp) == 1 else "MIXED",
                     list(obs)[0] if len(obs) == 1 else "MIXED",
                     mis,
                     str(list(hashes)[0])[:12] if len(hashes) == 1 else "MIXED",
                     "ok" if ok else "*** PROBLEM ***"))

    print("\n  expected > 0 means the phase actually contained hits, which is exactly what the")
    print("  first version of this bench did not have: every query was a miss and the checksum")
    print("  still matched across arms.")
    print("  probe_hash identical across arms means all four arms queried the same keys.")

    if problems:
        print("\n  %d phase(s) failed the evidence checks -- the timings above are NOT usable."
              % problems)

    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

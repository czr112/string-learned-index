#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verdict roll-up for the combination round, in the round plan's own language.

For every metric and every arm pair it counts, over all 6 (backend x mode) x 3 corpora x
5 checkpoints = 90 cells... no: over the 2 backends x 2 modes x 3 corpora x 5 checkpoints
= 60 cells, how many are 'candidate faster', 'candidate slower', 'unresolved' or 'mixed'
under the SAME rule the analyzer uses: a difference smaller than max/min of the per-process
medians is UNRESOLVED, and unresolved is not called a win.

It also prints, for the questions the round was asked to answer, the per-cell detail for
the key metrics at the 200% checkpoint plus the full hit series.
"""

import collections
import glob
import json
import statistics
import sys

MAIN = "results/main"
CPS = [0.0, 0.25, 0.5, 1.0, 2.0]
PAIRS = [("B", "W", "W/B"), ("B", "C", "C/B"), ("W", "WC", "WC/W"), ("C", "WC", "WC/C"),
         ("B", "WC", "WC/B")]
METRICS = ["seg_ns", "cum_ns", "hit_ns", "miss_ns", "scan16_ns", "scan256_ns", "scan4096_ns",
           "index_bytes", "overflow", "routing_bytes"]


def load(run_id):
    rows = []

    for path in sorted(glob.glob("%s/combo_%s_*.jsonl" % (MAIN, run_id))):
        if "combo_work_" in path:
            continue

        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()

                if line:
                    rows.append(json.loads(line))

    return rows


def ratios(rows, backend, mode, kind, frac, base, cand, metric):
    pairs = collections.defaultdict(dict)

    for r in rows:
        if (r["backend"] != backend or r["mode"] != mode or r["kind"] != kind
                or float(r["frac"]) != frac or r["arm"] not in (base, cand)):
            continue

        pairs[(r["run_tag"], r["rep"])][r["arm"]] = r[metric]

    by_proc = collections.defaultdict(list)

    for (tag, _rep), v in pairs.items():
        if base in v and cand in v and v[base] > 0:
            proc = tag.split("-")[1] if "-" in tag else tag
            by_proc[proc].append(float(v[cand]) / float(v[base]))

    return {p: statistics.median(vals) for p, vals in by_proc.items() if vals}


def verdict(vals):
    vals = sorted(vals)
    med = statistics.median(vals)
    faster = sum(1 for v in vals if v < 1.0)
    n = len(vals)
    noise = (vals[-1] / vals[0]) if vals[0] > 0 else float("inf")

    if max(vals) == min(vals) == 1.0:
        return med, "IDENTICAL"

    if abs(med - 1.0) < max(0.0, noise - 1.0):
        return med, "UNRESOLVED"

    if faster >= n - 1:
        return med, "FASTER"

    if faster <= 1:
        return med, "SLOWER"

    return med, "MIXED"


def regressions(rows, backends, modes, kinds):
    """Every cell whose verdict is SLOWER, with its median, direction and noise floor. These
    are the cells the round plan's adoption rule is about: a resolvable regression on point
    queries or scans keeps the combination a scenario candidate rather than a default."""
    print("=" * 120)
    print("EVERY RESOLVABLE REGRESSION (verdict SLOWER), all metrics")
    print("=" * 120)
    n = 0

    for metric in METRICS:
        for base, cand, label in PAIRS:
            for b in backends:
                for m in modes:
                    for k in kinds:
                        for frac in CPS:
                            d = ratios(rows, b, m, k, frac, base, cand, metric)

                            if not d:
                                continue

                            vals = sorted(d.values())
                            med, v = verdict(list(d.values()))

                            if v == "SLOWER":
                                n += 1
                                print("  %-12s %-5s %-9s %-5s %-7s frac=%.2f  med=%.3f "
                                      "dir=%d/%d  noise=%.2f"
                                      % (metric, label, b, m, k, frac, med,
                                         sum(1 for x in vals if x < 1.0), len(vals),
                                         vals[-1] / vals[0]))

    print()
    print("  %d resolvable regression cell(s) in total" % n)
    return n


def main():
    run_id = sys.argv[1] if len(sys.argv) > 1 else "combo_20261010a"
    rows = load(run_id)
    backends = ["Context16", "LitsHpt"]
    modes = ["same", "grow"]
    kinds = ["mixed", "skew", "tranco"]

    print("VERDICT COUNTS over %d cells (2 backends x 2 modes x 3 corpora x 5 checkpoints)"
          % (len(backends) * len(modes) * len(kinds) * len(CPS)))
    print("  %-14s %-6s %8s %6s %8s %6s %8s %6s %8s %6s"
          % ("metric", "pair", "FASTER", "", "SLOWER", "", "UNRES", "", "MIXED", ""))
    print("  " + "-" * 100)

    for metric in METRICS:
        for base, cand, label in PAIRS:
            counts = collections.Counter()

            for b in backends:
                for m in modes:
                    for k in kinds:
                        for frac in CPS:
                            d = ratios(rows, b, m, k, frac, base, cand, metric)

                            if not d:
                                continue

                            _med, v = verdict(list(d.values()))
                            counts[v] += 1

            print("  %-14s %-6s %8d %6s %8d %6s %8d %6s %8d %6s"
                  % (metric, label, counts["FASTER"], "", counts["SLOWER"], "",
                     counts["UNRESOLVED"], "", counts["MIXED"], ""))

        print()

    # The three questions, cell by cell at 200%, plus the hit series.
    print("=" * 120)
    print("DETAIL: pair medians at 200% by (backend, mode, corpus); cell = median direction "
          "noise-floor")
    print("=" * 120)

    for metric in ["seg_ns", "hit_ns", "miss_ns", "scan4096_ns", "index_bytes", "overflow"]:
        print("metric=%s" % metric)
        print("  %-9s %-5s %-7s %28s %28s %28s %28s %28s"
              % ("backend", "mode", "corpus", "W/B", "C/B", "WC/W", "WC/C", "WC/B"))

        for b in backends:
            for m in modes:
                for k in kinds:
                    cells = []

                    for base, cand, _lbl in PAIRS:
                        d = ratios(rows, b, m, k, 2.0, base, cand, metric)

                        if not d:
                            cells.append("%28s" % "n/a")
                            continue

                        vals = sorted(d.values())
                        med = statistics.median(vals)
                        faster = sum(1 for v in vals if v < 1.0)
                        noise = vals[-1] / vals[0] if vals[0] > 0 else float("inf")
                        _md, v = verdict(list(d.values()))
                        cells.append("%8.3f %d/%d %-4s %6.2f"
                                     % (med, faster, len(vals), v[:4], noise))

                    print("  %-9s %-5s %-7s %s" % (b, m, k, " ".join(cells)))

        print()

    print("=" * 120)
    print("HIT SERIES: Context16 same, three corpora, all checkpoints (W/B, C/B, WC/W, WC/B)")
    print("=" * 120)

    for k in kinds:
        for frac in CPS:
            cells = []

            for base, cand, _lbl in PAIRS:
                d = ratios(rows, "Context16", "same", k, frac, base, cand, "hit_ns")

                if not d:
                    cells.append("n/a")
                    continue

                vals = sorted(d.values())
                med = statistics.median(vals)
                faster = sum(1 for v in vals if v < 1.0)
                _md, v = verdict(list(d.values()))
                cells.append("%.3f %d/5 %s" % (med, faster, v[:4]))

            print("  %-7s frac=%.2f | W/B %-16s C/B %-16s WC/W %-16s WC/B %s"
                  % (k, frac, cells[0], cells[1], cells[2], cells[4]))

        print()

    regressions(rows, backends, modes, kinds)


if __name__ == "__main__":
    main()
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""matrix3 roll-up: the external static point-query matrix at the CURRENT identity,
with the compact-routing candidate as its own arms.

Reads results/matrix3_{mixed,skew,tranco}/summary.csv and prints, per corpus:
  * median lookup ns per arm, and each arm's ratio against art-libart;
  * the candidate's own comparison, C-on against C-off, per model backend;
and prints the previous matrix (matrix2) side by side for the arms that existed there,
so a reader can see what changed in the re-measurement rather than trusting that nothing did.
"""

import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
CORPORA = ["mixed", "skew", "tranco"]
ARMS = ["ours-context16", "ours-context16-routing", "ours-lits", "ours-lits-routing",
        "art-libart", "hot"]


def load(matrix, corpus):
    path = os.path.join(HERE, "results", "%s_%s" % (matrix, corpus), "summary.csv")

    if not os.path.exists(path):
        return None

    rows = {}

    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            rows[row["implementation_id"]] = row

    return rows


def num(row, field):
    try:
        return float(row[field])
    except (TypeError, ValueError, KeyError):
        return None


print("=" * 108)
print("matrix3 (current identity f573480d...), static point query, eval entry default protocol")
print("=" * 108)

for corpus in CORPORA:
    m3 = load("matrix3", corpus)
    m2 = load("matrix2", corpus)

    if not m3:
        print("%s: MISSING" % corpus)
        continue

    art = num(m3.get("art-libart", {}), "median_lookup_ns")
    print()
    print("%s  (median lookup ns; vs ART; matrix2 median in brackets)" % corpus)
    print("  %-24s %14s %8s %14s %8s" % ("arm", "lookup_ns", "x ART", "m2 lookup", "m2 x ART"))
    print("  " + "-" * 76)

    for arm in ARMS:
        row = m3.get(arm)

        if not row or row.get("status") != "passed":
            print("  %-24s   NOT PASSED" % arm)
            continue

        v = num(row, "median_lookup_ns")
        r = (v / art) if (v and art) else float("nan")
        v2 = num(m2.get(arm, {}) if m2 else {}, "median_lookup_ns")
        r2 = (v2 / art) if (v2 and art) else float("nan")
        print("  %-24s %14.0f %8.3f %14s %8s"
              % (arm, v, r, ("%.0f" % v2) if v2 else "-", ("%.3f" % r2) if v2 else "-"))

    # The candidate's own comparison, per backend.
    print()
    print("  candidate effect (C-on / C-off, same process block, same repeats):")

    for off, on, label in [("ours-context16", "ours-context16-routing", "Context16"),
                           ("ours-lits", "ours-lits-routing", "LitsHpt")]:
        a = num(m3.get(off, {}), "median_lookup_ns")
        b = num(m3.get(on, {}), "median_lookup_ns")

        if a and b:
            print("    %-10s lookup %8.0f -> %8.0f  ratio %.3f   throughput ratio %.3f"
                  % (label, a, b, b / a,
                     num(m3.get(on, {}), "median_throughput_ops_per_second")
                     / num(m3.get(off, {}), "median_throughput_ops_per_second")))

    print()
    print("  p50/p99 ns: " + " | ".join(
        "%s %s/%s" % (a, m3[a].get("latency_p50_ns", "?"), m3[a].get("latency_p99_ns", "?"))
        for a in ARMS if a in m3))

print()
print("=" * 108)
print("NOTE. This entry measures STATIC bulk_load+find only. It cannot evaluate the WIDE")
print("candidate (subblock_reclaim+reclaim_widest), whose effect appears under replacement;")
print("those arms stay in the project's own combination round.")
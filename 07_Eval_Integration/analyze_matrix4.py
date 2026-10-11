#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""matrix4 roll-up: partner BlockLearn arms vs ours vs ART/HOT on the same corpora.

Reads results/matrix4_{mixed,skew,tranco}/summary.csv (one python invocation per corpus,
so all arms share the same process blocks) and prints, per corpus:
  * median lookup ns, ratio against art-libart, build ms, throughput, p50/p99,
    index-owned bytes and retained-input bytes;
  * the partner's ablation ladder block-plain -> block-hash -> block-residual ->
    block-residual-delta (same planning, different verification) against ART.
Also writes results/matrix4_rollup.json with the same numbers for the report.
"""

import csv
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
CORPORA = ["mixed", "skew", "tranco"]
ARMS = ["ours-context16", "ours-lits", "art-libart", "hot",
        "block-plain", "block-hash", "block-residual", "block-residual-delta"]
ABLATION = ["block-plain", "block-hash", "block-residual", "block-residual-delta"]


def load(corpus):
    path = os.path.join(HERE, "results", "matrix4_%s" % corpus, "summary.csv")
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


def main():
    rollup = {}
    for corpus in CORPORA:
        rows = load(corpus)
        if not rows:
            print("%s: MISSING" % corpus)
            continue
        art = num(rows.get("art-libart", {}), "median_lookup_ns")
        print("=" * 118)
        print("matrix4 / %s  (median over 3 repeats of 10k uniform point queries; ns; x = vs ART)"
              % corpus)
        print("=" * 118)
        print("  %-22s %12s %7s %11s %12s %8s %8s %13s %13s"
              % ("arm", "lookup_ns", "x ART", "build_ms", "throughput",
                 "p50_ns", "p99_ns", "index_bytes", "retained_bytes"))
        print("  " + "-" * 114)
        cell = {}
        for arm in ARMS:
            r = rows.get(arm)
            if not r or r.get("status") != "passed":
                reason = (r or {}).get("reason", "missing")
                print("  %-22s   NOT PASSED (%s)" % (arm, reason[:60]))
                cell[arm] = {"status": (r or {}).get("status", "missing")}
                continue
            v = num(r, "median_lookup_ns")
            ratio = (v / art) if (v and art) else float("nan")
            print("  %-22s %12.0f %7.3f %11.1f %12.0f %8s %8s %13s %13s"
                  % (arm, v, ratio, num(r, "median_build_ns") / 1e6,
                     num(r, "median_throughput_ops_per_second"),
                     r.get("latency_p50_ns"), r.get("latency_p99_ns"),
                     r.get("index_owned_bytes"), r.get("retained_input_bytes")))
            cell[arm] = {
                "status": "passed",
                "median_lookup_ns": v,
                "ratio_vs_art": ratio,
                "median_build_ms": num(r, "median_build_ns") / 1e6,
                "median_throughput_ops_per_second": num(r, "median_throughput_ops_per_second"),
                "latency_p50_ns": num(r, "latency_p50_ns"),
                "latency_p99_ns": num(r, "latency_p99_ns"),
                "index_owned_bytes": num(r, "index_owned_bytes"),
                "retained_input_bytes": num(r, "retained_input_bytes"),
            }
        print()
        print("  partner ablation ladder (same planning; verification differs):")
        for arm in ABLATION:
            c = cell.get(arm, {})
            if c.get("status") != "passed":
                print("    %-22s NOT PASSED" % arm)
                continue
            print("    %-22s lookup %12.0f ns  xART %6.3f  build %8.1f ms  index %12s B"
                  % (arm, c["median_lookup_ns"], c["ratio_vs_art"],
                     c["median_build_ms"], c["index_owned_bytes"]))
        rollup[corpus] = cell
        print()
    out = os.path.join(HERE, "results", "matrix4_rollup.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(rollup, f, indent=2, sort_keys=True)
        f.write("\n")
    print("rollup written: %s" % out)


if __name__ == "__main__":
    main()
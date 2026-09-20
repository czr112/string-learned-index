#!/usr/bin/env python3
"""Render E25_TIME.md from results/main/e25_time.jsonl.

PAIRED, NOT DIVIDED. V1/V0, V2/V1, V2/V0 and V2/ART are formed inside a single
repetition, where all four arms ran the same operation sequence from the same
initial state. Ratios are then aggregated per PROCESS (median over that process's
repetitions) and the cross-process spread is reported. Dividing one aggregate
median by another is never done here -- with a rotated arm order the pairing is
what controls for order.

A ratio below 1 means the numerator is FASTER. `dir` counts how many independent
processes agree on that direction.

Usage:  python3 analyze_e25_time.py
"""
import collections
import json
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "results", "main", "e25_time.jsonl")
OUT = os.path.join(HERE, "E25_TIME.md")

KINDS = ["mixed", "skew", "tranco"]
WORKLOADS = ["update_only", "insert_only", "replace"]
ARMS = ["V0", "V1", "V2", "ART"]
PAIRS = [("V1", "V0"), ("V2", "V1"), ("V2", "V0"), ("V2", "ART")]


def load():
    with open(SRC, encoding="utf-8") as f:
        return [json.loads(l) for l in f if l.strip()]


def fmt(x):
    return "--" if x is None else "%.2f" % x


def med(v):
    return st.median(v) if v else None


def build_series(rows):
    """series[(kind, wl, tag, rep)][arm][cycles] = cumulative ms through that cycle.

    Rows are written in checkpoint order per arm, so a running sum per key gives the
    cumulative cost -- which is the quantity a "did the update get cheaper overall"
    question is actually about. Summing only the last segment would ignore everything
    paid before it.
    """
    series = collections.defaultdict(lambda: collections.defaultdict(dict))
    acc = collections.defaultdict(float)
    for r in rows:
        if r["query_cp"] != -1:
            continue
        kk = (r["kind"], r["workload"], r["run_tag"], r["rep"])
        acc[(kk, r["arm"])] += r["update_ms"]
        # Keyed by the checkpoint FRACTION the bench recorded, not by a cycle count:
        # a cycle means different things for replace (delete+insert) and for the
        # single-op workloads, so labelling by ratio-to-last would misname them.
        series[kk][r["arm"]][r["frac"]] = acc[(kk, r["arm"])]
    return series


def main():
    rows = load()
    series = build_series(rows)
    tags = sorted({r["run_tag"] for r in rows})
    checkpoints = collections.defaultdict(set)
    ops_at = {}
    cycles_at = {}
    for r in rows:
        if r["query_cp"] == -1:
            checkpoints[(r["kind"], r["workload"])].add(r["frac"])
            if r["ops"] > 0:
                ops_at[(r["kind"], r["workload"], r["frac"])] = r["ops"]
                cycles_at[(r["kind"], r["workload"], r["frac"])] = r["cycles"]

    w = []
    a = w.append
    a("# E25 formal comparison: V0 / V1 / V2 / ART")
    a("")
    a("Source: `results/main/e25_time.jsonl`, %d records." % len(rows))
    a("")
    a("**Ratios are PAIRED inside one repetition**, where all four arms ran the same")
    a("operation sequence from the same initial state, with the arm order rotated per")
    a("repetition. Each process's ratios are reduced to that process's median, and the")
    a("cross-process spread is reported. **No ratio here is the quotient of two")
    a("aggregate medians.**")
    a("")
    a("**A ratio below 1 means the numerator is faster.** `dir` is how many independent")
    a("processes agree on that direction, over the processes that produced the cell.")
    a("")
    a("The timed span contains the index operations **and everything they trigger** --")
    a("compaction, region rebuilds, container growth, string allocation. Key")
    a("generation, reference upkeep and answer checking run outside it. An arm that")
    a("disagreed with the reference aborted the run instead of recording a time.")
    a("")
    a("`update_only` / `insert_only` / `replace` are **three separately built states**, ")
    a("each from its own fresh initial state. They are not a decomposition of one mixed")
    a("workload. `replace` reports cycles (one delete + one insert) and operations.")
    a("")
    a("Every arm uses **M0, the existing maintenance rule**. No variant adds a")
    a("compaction or migration policy of its own.")
    a("")

    for kind in KINDS:
        a("## %s" % kind)
        a("")
        for wl in WORKLOADS:
            cps = sorted(checkpoints[(kind, wl)])
            if not cps:
                continue
            last = max(cps)
            a("### %s" % wl)
            a("")
            a("| checkpoint | cycles | ops | V0 | V1 | V2 | ART |")
            a("|---|---:|---:|---:|---:|---:|---:|")
            for c in cps:
                vals = {arm: [] for arm in ARMS}
                for kk, per_arm in series.items():
                    if (kk[0], kk[1]) != (kind, wl):
                        continue
                    for arm in ARMS:
                        if arm in per_arm and c in per_arm[arm]:
                            vals[arm].append(per_arm[arm][c])
                a("| %d%% | %d | %d | %s | %s | %s | %s |" % (
                    int(round(100.0 * c)), cycles_at.get((kind, wl, c), 0), ops_at.get((kind, wl, c), 0),
                    fmt(med(vals["V0"])), fmt(med(vals["V1"])),
                    fmt(med(vals["V2"])), fmt(med(vals["ART"]))))
            a("")
            a("Cumulative ms through each checkpoint (median over all repetitions).")
            a("")
            a("**Paired ratios** at each checkpoint:")
            a("")
            a("| checkpoint | pair | cross-process median | range | dir |")
            a("|---|---|---:|---|---:|")
            for c in cps:
                for (num, den) in PAIRS:
                    perproc = []
                    for p in tags:
                        reps = []
                        for kk, per_arm in series.items():
                            if (kk[0], kk[1], kk[2]) != (kind, wl, p):
                                continue
                            if num in per_arm and den in per_arm:
                                if c in per_arm[num] and c in per_arm[den]:
                                    d = per_arm[den][c]
                                    if d > 0:
                                        reps.append(per_arm[num][c] / d)
                        if reps:
                            perproc.append(st.median(reps))
                    if not perproc:
                        continue
                    faster = sum(1 for x in perproc if x < 1.0)
                    a("| %d%% | %s/%s | **%.3f** | %.3f–%.3f | %d/%d |" % (
                        int(round(100.0 * c)), num, den,
                        st.median(perproc), min(perproc), max(perproc), faster, len(perproc)))
            a("")

    # ---- query regression ---------------------------------------------------
    a("## Point-query regression")
    a("")
    a("Fixed key sequence, timed outside the update span. Ratio below 1 = numerator")
    a("faster. This is the check that giving `find_impl` an optional out-parameter did")
    a("not move the read path.")
    a("")
    a("| dataset | V0 | V1 | V2 | ART | V1/V0 | V2/V0 | V2/ART |")
    a("|---|---:|---:|---:|---:|---:|---:|---:|")
    for kind in KINDS:
        medv = {arm: [] for arm in ARMS}
        pairvals = collections.defaultdict(list)
        for p in tags:
            per_rep = collections.defaultdict(dict)
            for r in rows:
                if r["kind"] != kind or r["run_tag"] != p or r["query_cp"] != 0:
                    continue
                per_rep[(r["workload"], r["rep"])][r["arm"]] = r["query_ms"]
            for v in per_rep.values():
                for arm in ARMS:
                    if arm in v:
                        medv[arm].append(v[arm])
                if "V0" in v and v["V0"] > 0:
                    if "V1" in v:
                        pairvals["V1/V0"].append(v["V1"] / v["V0"])
                    if "V2" in v:
                        pairvals["V2/V0"].append(v["V2"] / v["V0"])
                    if "ART" in v and v["ART"] > 0:
                        pairvals["V2/ART"].append(v["V2"] / v["ART"])
        a("| %s | %s | %s | %s | %s | %s | %s | %s |" % (
            kind, fmt(med(medv["V0"])), fmt(med(medv["V1"])), fmt(med(medv["V2"])),
            fmt(med(medv["ART"])),
            fmt(med(pairvals["V1/V0"])), fmt(med(pairvals["V2/V0"])),
            fmt(med(pairvals["V2/ART"]))))
    a("")

    a("## Build time (context only -- building is not this round's subject)")
    a("")
    a("| dataset | arm | median build ms |")
    a("|---|---|---:|")
    for kind in KINDS:
        for arm in ARMS:
            v = [r["build_ms"] for r in rows
                 if r["kind"] == kind and r["arm"] == arm and r["build_ms"] > 0]
            if v:
                a("| %s | %s | %.2f |" % (kind, arm, st.median(v)))
    a("")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(w) + "\n")
    print("wrote %s (%d lines)" % (OUT, len(w)))


if __name__ == "__main__":
    main()

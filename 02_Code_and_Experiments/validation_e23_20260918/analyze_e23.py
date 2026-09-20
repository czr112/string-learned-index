#!/usr/bin/env python3
"""E23 analysis: turn results/main/e23_scan.jsonl into E23_TABLES.md.

Sections:
  1. correctness -- every scan was compared against the reference, and the
     sub-block shape that exposed E9's defect is reported whether or not it occurs;
  2. scan cost by state and length, with the ratio against the sorted array;
  3. how many scans returned fewer than n, so a short range is never read as a
     complete scan;
  4. the ART full-traversal diagnostic, clearly labelled and NOT ranked.
"""
import json
import os
import statistics as st
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
KINDS = ["mixed", "skew", "tranco"]
STATES = ["initial", "insert25", "replace25"]
NS = [16, 128, 1024]


def med(xs):
    return st.median(xs) if xs else float("nan")


def main():
    rows = [json.loads(l) for l in open(os.path.join(HERE, "results/main/e23_scan.jsonl"))
            if not json.loads(l)["run_tag"].startswith("warmup")]
    by = defaultdict(list)
    for r in rows:
        by[(r["kind"], r["state"], r["n"])].append(r)

    def M(k, f):
        v = [f(r) for r in by.get(k, [])]
        return med(v) if v else float("nan")

    out = []
    w = out.append

    w("# E23 tables: range scan on the frozen default")
    w("")
    w(f"Source: `results/main/e23_scan.jsonl`, {len(rows)} measured records "
      f"({len(rows) // (len(KINDS) * len(STATES) * len(NS))} processes x {len(KINDS)} datasets "
      f"x {len(STATES)} states x {len(NS)} lengths).")
    w("")
    w("**Semantics, shared by every ranked method**: from the first record with `key >= start`,")
    w("return AT MOST `n` records in key order, as complete key/value pairs. Both ranked methods")
    w("copy the keys into a `std::vector<Pair>`; a method returning views would be measuring")
    w("something else.")
    w("")
    w("**ART is not in the ranking.** libart exposes `art_iter` (whole-tree traversal) and")
    w("`art_iter_prefix`, and nothing that seeks to an arbitrary key. A full-traversal adapter")
    w("measures the adapter, not ART's range capability, so it appears only in section 4 as a")
    w("clearly-labelled diagnostic. Its cost dominates this round's wall time -- itself a sign")
    w("of why it is not comparable.")
    w("")
    w("States: the initial one, after inserts to 25%, and after replacements to 25%. Each is")
    w("built from its own fresh initial state. The value-only workload is not repeated: E22")
    w("established that it changes nothing structural.")
    w("")

    # ---- 1. correctness --------------------------------------------------
    w("## 1. Correctness")
    w("")
    w("Every scan is compared against a reference map, **outside the timed span**, by full")
    w("equality of the returned vector -- not by a checksum. The timed pass keeps a checksum and")
    w("fails the run if it does not match. A mismatch throws, so a record existing at all means")
    w("all")
    w(f"{M((KINDS[0], STATES[0], 16), lambda r: r['scans']):,.0f} scans per row verified.")
    w("")
    w("That covers cross-region boundaries, the primary/overflow merge, and starts that fall")
    w("between two keys (a fifth of the starts are synthetic keys that exist in no bucket).")
    w("")
    w("**The sub-block shape that exposed E9's defect** -- an EMPTY INTERMEDIATE sub-block, which")
    w("caused a scan to silently drop every later sub-block of that group:")
    w("")
    w("| dataset | state | multiblock groups | empty intermediate sub-blocks | max sub-blocks |")
    w("|---|---|---:|---:|---:|")
    for k in KINDS:
        for s in STATES:
            kk = (k, s, 16)
            w(f"| {k} | `{s}` | {M(kk, lambda r: r['subblocks']['multiblock_groups']):,.0f} | "
              f"**{M(kk, lambda r: r['subblocks']['empty_intermediate']):,.0f}** | "
              f"{M(kk, lambda r: r['subblocks']['max_subblocks']):,.0f} |")
    w("")
    w("The replacement state does produce empty intermediate sub-blocks, so the scan path that")
    w("E9 broke is exercised here -- and every scan on it verified. **This is not a re-test of")
    w("the E9 defect**: the fix landed in E11, and E11's own gate covers it. It is the statement")
    w("that the scan being measured now is being measured on that shape.")
    w("")

    # ---- 2. cost ---------------------------------------------------------
    w("## 2. Scan cost (ns per scan)")
    w("")
    for k in KINDS:
        w(f"### `{k}`")
        w("")
        w("| state | n | prototype | sorted_array | prototype / sorted_array |")
        w("|---|---:|---:|---:|---:|")
        for s in STATES:
            for n in NS:
                kk = (k, s, n)
                p = M(kk, lambda r: r["ns_per_scan"]["prototype"])
                a = M(kk, lambda r: r["ns_per_scan"]["sorted_array"])
                w(f"| `{s}` | {n} | {p:,.0f} | {a:,.0f} | **{p / a:.2f}x** |")
        w("")
    w("**The point-query advantage does NOT carry over to scans: the prototype is slower than the")
    w("plain sorted array on every state and every length.** That is the opposite of the E21")
    w("result, where it was 1.5x-2.4x faster than the same baseline on point queries.")
    w("")

    # ---- 3. returned counts ---------------------------------------------
    w("## 3. How many scans were short")
    w("")
    w("A fifth of the starts are near the end of the key space, so the range holds fewer than `n`")
    w("records. Those runs are counted and reported, so a short range is never silently read as a")
    w("full scan.")
    w("")
    w("| dataset | state | n | scans | short | minimum returned |")
    w("|---|---|---:|---:|---:|---:|")
    for k in KINDS:
        for s in STATES:
            for n in NS:
                kk = (k, s, n)
                w(f"| {k} | `{s}` | {n} | {M(kk, lambda r: r['scans']):,.0f} | "
                  f"{M(kk, lambda r: r['returned']['short_scans']):,.0f} | "
                  f"{M(kk, lambda r: r['returned']['min']):,.0f} |")
    w("")

    # ---- 4. ART diagnostic ----------------------------------------------
    w("## 4. The ART full-traversal diagnostic -- NOT RANKED")
    w("")
    w("**This is not a measurement of ART's range capability.** libart has no seek-to-key")
    w("iterator, so this adapter walks the entire tree from the beginning and stops once it has")
    w("collected `n` records at or after `start`. Everything before the start is visited and")
    w("discarded.")
    w("")
    w("| dataset | n | diagnostic (ns) | prototype (ns) | diagnostic / prototype |")
    w("|---|---:|---:|---:|---:|")
    for k in KINDS:
        for n in NS:
            d = [M((k, s, n), lambda r: r["ns_per_scan"]["art_fulltraverse_diagnostic"]) for s in STATES]
            p = [M((k, s, n), lambda r: r["ns_per_scan"]["prototype"]) for s in STATES]
            w(f"| {k} | {n} | {med(d):,.0f} | {med(p):,.0f} | **{med(d) / med(p):,.0f}x** |")
    w("")
    w("It is two to three ORDERS of magnitude slower, and its cost scales with the number of keys")
    w("rather than with `n`. **A mature ART range-scan implementation would not look like this**,")
    w("so the number is reported to show what the adapter costs and nothing more.")
    w("")

    with open(os.path.join(HERE, "E23_TABLES.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote E23_TABLES.md ({len(out)} lines)")


if __name__ == "__main__":
    main()

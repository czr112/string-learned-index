#!/usr/bin/env python3
"""Render E26_TAGS.md: the C0/C1 tag-reuse ablation, plus the compaction diagnostics.

Two sources:
  results/main/e26_diag.jsonl  -- counts: why compaction runs and whether it helped
  results/main/e26_time.jsonl  -- clean timing: C1/C0 paired inside one repetition

The diagnostic build deliberately CANNOT show C1's hashing saving: it re-computes
every tag to check the reuse is sound, which is the very work C1 removes. So the
saving appears here as the copy volume C0 hashes and C1 does not, and as time. The
per-record equality check is an assertion in the diagnostic program, not a metric.

Usage:  python3 analyze_e26.py
"""
import collections
import json
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
DIAG = os.path.join(HERE, "results", "main", "e26_diag.jsonl")
TIME = os.path.join(HERE, "results", "main", "e26_time.jsonl")
OUT = os.path.join(HERE, "E26_TAGS.md")

KINDS = ["mixed", "skew", "tranco"]
WORKLOADS = ["insert_only", "replace"]


def load(p):
    with open(p, encoding="utf-8") as f:
        return [json.loads(l) for l in f if l.strip()]


def main():
    diag = load(DIAG)
    time = load(TIME)

    w = []
    a = w.append
    a("# E26: what compaction does, and what reusing the stored tag saves")
    a("")
    a("Sources: `results/main/e26_diag.jsonl` (%d records, counts) and "
      "`results/main/e26_time.jsonl` (%d records, clean timing)." % (len(diag), len(time)))
    a("")
    a("Both arms sit on the **E25 default candidate** (V1 + V2). C0 and C1 differ by")
    a("**one `Config` flag**, `compact_reuse_tag`. Membership, placement order,")
    a("capacity, the compaction trigger and the boundary maintenance are untouched by")
    a("design, so the difference is attributable to the removed recomputation.")
    a("")

    # ---- 1. what compaction does -------------------------------------------
    a("## 1. Why compaction runs, and whether it helped")
    a("")
    a("`entered` counts entries into the branch that can compact (a primary placement")
    a("failed); `ran` is when it actually compacted (`group_has_room` was true);")
    a("`helped` is when the retry then placed the record in the primary.")
    a("")
    a("**A `no help` is recorded as \"did not enable THIS placement\", not as \"the")
    a("compaction had no value\"** -- compaction also redistributes records across")
    a("sub-blocks, and that can matter for later operations.")
    a("")
    a("| dataset | workload | ran | helped | no help | helped / ran |")
    a("|---|---|---:|---:|---:|---:|")
    for k in KINDS:
        for wl in WORKLOADS:
            r = [x for x in diag if x["kind"] == k and x["workload"] == wl and x["arm"] == "C0"]
            if not r:
                continue
            r = r[0]
            ran = r["compactions"]
            a("| %s | %s | %d | %d | %d | %s |" % (
                k, wl, ran, r["compact_helped"], r["compact_no_help"],
                ("%.1f%%" % (100.0 * r["compact_helped"] / ran)) if ran else "--"))
    a("")
    a("## 2. What a compaction costs, and what it reclaims")
    a("")
    a("`garbage_before` is the un-referenced byte total **in the groups being")
    a("compacted** -- not an index-wide figure. `copied` is the suffix volume the")
    a("replay copies, and is exactly the volume C0 re-hashes.")
    a("")
    a("| dataset | workload | records replayed | copied (MB) | garbage before (KB) | garbage / copied |")
    a("|---|---|---:|---:|---:|---:|")
    for k in KINDS:
        for wl in WORKLOADS:
            r = [x for x in diag if x["kind"] == k and x["workload"] == wl and x["arm"] == "C0"]
            if not r:
                continue
            r = r[0]
            cp, gb = r["compact_bytes_copied"], r["compact_garbage_before"]
            a("| %s | %s | %d | %.2f | %.1f | %s |" % (
                k, wl, r["compact_records_replayed"], cp / 1048576.0, gb / 1024.0,
                ("%.2f%%" % (100.0 * gb / cp)) if cp else "--"))
    a("")
    a("## 3. Hashing C1 removes (from the copy volume, NOT from the diag counter)")
    a("")
    a("The diagnostic build re-hashes every record to check the reuse, so its")
    a("`fp_hash_bytes` cannot show C1's saving. The column below is the volume C0")
    a("hashes during replay (`compact_bytes_copied`) against C0's total hashing.")
    a("")
    a("| dataset | workload | C0 total hash (MB) | replay hash removed (MB) | share removed |")
    a("|---|---|---:|---:|---:|")
    for k in KINDS:
        for wl in WORKLOADS:
            r = [x for x in diag if x["kind"] == k and x["workload"] == wl and x["arm"] == "C0"]
            if not r:
                continue
            r = r[0]
            tot, cp = r["fp_hash_bytes"], r["compact_bytes_copied"]
            a("| %s | %s | %.2f | %.2f | **%s** |" % (
                k, wl, tot / 1048576.0, cp / 1048576.0,
                ("%.1f%%" % (100.0 * cp / tot)) if tot else "--"))
    a("")

    # ---- 4. timing ----------------------------------------------------------
    a("## 4. C1 / C0, paired inside each repetition")
    a("")
    a("Ratio below 1 = C1 faster. Ratios are formed within one repetition and reduced")
    a("to a per-process median; `dir` counts processes agreeing on the direction.")
    a("")
    tags = sorted({r["run_tag"] for r in time})
    series = collections.defaultdict(lambda: collections.defaultdict(dict))
    acc = collections.defaultdict(float)
    for r in time:
        kk = (r["kind"], r["workload"], r["run_tag"], r["rep"])
        acc[(kk, r["arm"])] += r["ms"]
        series[kk][r["arm"]][r["frac"]] = acc[(kk, r["arm"])]

    a("| dataset | workload | checkpoint | C0 | C1 | C2 | ART | C1/C0 | C2/C1 | **C2/C0** | **C2/ART** |")
    a("|---|---|---:|---:|---:|---:|---:|---|---|---|---|")
    for k in KINDS:
        for wl in WORKLOADS:
            fr = sorted({r["frac"] for r in time if r["kind"] == k and r["workload"] == wl})
            for f in fr:
                med = {}
                for arm in ("C0", "C1", "C2", "ART"):
                    v = [series[kk][arm][f] for kk in series
                         if kk[0] == k and kk[1] == wl and arm in series[kk] and f in series[kk][arm]]
                    med[arm] = st.median(v) if v else None

                def paired(num, den):
                    perproc = []
                    for p in tags:
                        reps = []
                        for kk in series:
                            if (kk[0], kk[1], kk[2]) != (k, wl, p):
                                continue
                            if (num in series[kk] and den in series[kk]
                                    and f in series[kk][num] and f in series[kk][den]
                                    and series[kk][den][f] > 0):
                                reps.append(series[kk][num][f] / series[kk][den][f])
                        if reps:
                            perproc.append(st.median(reps))
                    if not perproc:
                        return None
                    return (st.median(perproc), min(perproc), max(perproc),
                            sum(1 for x in perproc if x < 1.0), len(perproc))

                def cell(pr):
                    if pr is None:
                        return "--"
                    return "**%.3f** (%d/%d)" % (pr[0], pr[3], pr[4])

                a("| %s | %s | %d%% | %.2f | %.2f | %.2f | %.2f | %s | %s | %s | %s |" % (
                    k, wl, int(round(100.0 * f)),
                    med["C0"] or 0, med["C1"] or 0, med["C2"] or 0, med["ART"] or 0,
                    cell(paired("C1", "C0")), cell(paired("C2", "C1")),
                    cell(paired("C2", "C0")), cell(paired("C2", "ART"))))
    a("")
    a("`(dir)` is how many of the independent processes agree the numerator is faster.")
    a("C1/C0 isolates the removed recomputation. C2/C1 isolates the trigger gate. C2/C0")
    a("is the two together. **None of these is formed by multiplying the other two** --")
    a("each is measured directly inside the same repetition.")
    a("")
    a("**ART runs in the SAME repetition.** A historical ART time is not evidence about")
    a("a new arm, so the C2/ART column is a same-round comparison or it is nothing.")
    a("")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(w) + "\n")
    print("wrote %s (%d lines)" % (OUT, len(w)))


if __name__ == "__main__":
    main()

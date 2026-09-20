#!/usr/bin/env python3
"""Render E29_C3.md: does relaxing the compaction trigger pay for itself?

Source: results/main/long_c3_{same,grow}.jsonl -- three arms in one binary
(C1 = tag reuse, C2 = frozen version, C3 = frozen + relaxed trigger), so the
C3/C2 comparison is paired inside a repetition.

The question is not only "does it recover space" -- the gate already showed it does.
It is whether the recovered space is worth what it costs, because C3 runs far more
compactions.

Usage:  python3 analyze_c3.py
"""
import collections
import json
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
MODES = [("same", "results/main/long_c3_same.jsonl"),
         ("grow", "results/main/long_c3_grow.jsonl")]
OUT = os.path.join(HERE, "E29_C3.md")
KINDS = ["mixed", "skew", "tranco"]


def load(rel):
    p = os.path.join(HERE, rel)
    if not os.path.exists(p):
        return []
    with open(p, encoding="utf-8") as f:
        return [json.loads(l) for l in f if l.strip()]


def main():
    rows = load(MODES[0][1]) + load(MODES[1][1])
    if not rows:
        raise SystemExit("no data yet")
    tags = sorted({r["run_tag"] for r in rows if r["run_tag"] != "D1"})

    w = []
    a = w.append
    a("# E29: does the relaxed compaction trigger pay for itself?")
    a("")
    a("Source: `results/main/long_c3_{same,grow}.jsonl`, %d records." % len(rows))
    a("")
    a("**C2 is the frozen version**; **C3 = C2 + one switch** (`compact_repack_budget`).")
    a("Both run in the same process, so every ratio is paired inside a repetition.")
    a("")
    a("C3 relaxes the trigger from \"some single sub-block has a free slot AND enough")
    a("suffix bytes\" to \"a full repack of the group would fit\". The E28 measurement")
    a("showed the old condition is exactly the one that is usually false while the")
    a("group still holds reclaimable garbage.")
    a("")
    a("Correctness is checked by `check_c3`: content and key order identical to the")
    a("frozen version and to the reference at every checkpoint, on all three datasets.")
    a("")

    for mode, _ in MODES:
        mr = [r for r in rows if r.get("mode") == mode]
        if not mr:
            continue
        a("## Mode `%s`" % mode)
        a("")
        for kind in KINDS:
            kr = [r for r in mr if r["kind"] == kind]
            if not kr:
                continue
            a("### %s" % kind)
            a("")
            a("| cycles | C2 cum ms | C3 cum ms | **C3/C2** | dir | C2 overflow | C3 overflow | C2 compact | C3 compact | C2 MB | C3 MB |")
            a("|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|")
            for frac in sorted({r["frac"] for r in kr if r["run_tag"] != "D1"}):
                def med(arm, field):
                    v = [r[field] for r in kr
                         if r["arm"] == arm and r["frac"] == frac and r["run_tag"] != "D1"]
                    return st.median(v) if v else float("nan")

                perproc = []
                for p in tags:
                    byrep = collections.defaultdict(dict)
                    for r in kr:
                        if r["run_tag"] == p and r["frac"] == frac:
                            byrep[r["rep"]][r["arm"]] = r["cum_ms"]
                    reps = [d["C3"] / d["C2"] for d in byrep.values()
                            if "C2" in d and "C3" in d and d["C2"] > 0]
                    if reps:
                        perproc.append(st.median(reps))
                ratio = st.median(perproc) if perproc else float("nan")
                dirn = ("%d/%d" % (sum(1 for x in perproc if x < 1.0), len(perproc))
                        if perproc else "--")
                a("| %d%% | %.2f | %.2f | **%.3f** | %s | %d | %d | %d | %d | %.2f | %.2f |" % (
                    int(round(100 * frac)), med("C2", "cum_ms"), med("C3", "cum_ms"),
                    ratio, dirn, med("C2", "overflow"), med("C3", "overflow"),
                    med("C2", "compactions"), med("C3", "compactions"),
                    med("C2", "memory") / 1048576.0, med("C3", "memory") / 1048576.0))
            a("")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(w) + "\n")
    print("wrote %s (%d lines)" % (OUT, len(w)))


if __name__ == "__main__":
    main()

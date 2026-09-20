#!/usr/bin/env python3
"""Render E26_LONG.md: cumulative replacement out to 200%, C1 vs C2, two key modes.

Sources:
  results/main/long_replace_grow.jsonl   replacement key carries a bounded suffix
  results/main/long_replace_same.jsonl   replacement key is the SAME LENGTH as the one
                                         it replaces, anchored in the same place

The mode matters. If space fails to stabilise in BOTH, the cause is not key-length
inflation; if it only fails in `grow`, the cause is a key distribution the plan was
never built for. Reporting one without the other leaves that open.

The comparison is C1 vs C2, because the question is C2's LONG-RUN value. Whether the
frozen version beats ART is a different question, answered in E25_TIME/E26_TAGS.

Usage:  python3 analyze_long.py
"""
import collections
import json
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
MODES = [("grow", "results/main/long_replace_grow.jsonl"),
         ("same", "results/main/long_replace_same.jsonl")]
OUT = os.path.join(HERE, "E26_LONG.md")

KINDS = ["mixed", "skew", "tranco"]


def load(rel):
    p = os.path.join(HERE, rel)
    if not os.path.exists(p):
        return []
    with open(p, encoding="utf-8") as f:
        return [json.loads(l) for l in f if l.strip()]


def main():
    rows = load(MODES[0][1]) + load(MODES[1][1])
    tags = sorted({r["run_tag"] for r in rows if r["run_tag"] != "D1"})

    w = []
    a = w.append
    a("# E26 long run: cumulative replacement to 200%, C1 vs C2")
    a("")
    a("Source: `results/main/long_replace_{grow,same}.jsonl`, %d records." % len(rows))
    a("")
    a("**The comparison is C1 vs C2.** The question is whether C2's benefit holds up")
    a("over a long run; the frozen version against ART is a different question, answered")
    a("elsewhere with a same-round ART arm.")
    a("")
    a("**Workload.** Each cycle removes one live key and inserts a different one, so the")
    a("live count stays at ~N -- the task book's \"活跃记录数基本不变\". Checkpoints are")
    a("25% / 50% / 100% / 200% of N cycles. The index is spot-checked against the")
    a("reference after every segment; a drifted run aborts instead of recording a time.")
    a("")
    a("**Two key modes, and why both are here:**")
    a("")
    a("- `grow` -- the replacement carries a bounded suffix (~10 chars), so the key-length")
    a("  distribution drifts too. This is what inserting a genuinely *different* key does.")
    a("- `same` -- the replacement has the **same length** as the key it replaces and sits")
    a("  at the same place, so the key-length distribution is exactly the one the capacity")
    a("  plan was built for. **Only this mode can say whether space fails to stabilise")
    a("  under pure churn.**")
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
            a("| cycles (% of N) | live | C1 cum ms | C2 cum ms | **C2/C1** | dir | C1 overflow | C2 overflow | C1 compact | C2 compact | C1 MB | C2 MB |")
            a("|---|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|")
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
                    reps = [d["C2"] / d["C1"] for d in byrep.values()
                            if "C1" in d and "C2" in d and d["C1"] > 0]
                    if reps:
                        perproc.append(st.median(reps))
                ratio = st.median(perproc) if perproc else float("nan")
                dirn = ("%d/%d" % (sum(1 for x in perproc if x < 1.0), len(perproc))
                        if perproc else "--")
                a("| %d%% | %d | %.2f | %.2f | **%.3f** | %s | %d | %d | %d | %d | %.2f | %.2f |" % (
                    int(round(100 * frac)), med("C1", "live"),
                    med("C1", "cum_ms"), med("C2", "cum_ms"),
                    ratio, dirn, med("C1", "overflow"), med("C2", "overflow"),
                    med("C1", "compactions"), med("C2", "compactions"),
                    med("C1", "memory") / 1048576.0, med("C2", "memory") / 1048576.0))
            a("")

    a("## Compaction work over the run (diagnostic build, counters reset per arm)")
    a("")
    a("Counters are cumulative WITHIN an arm. The per-arm reset matters: without it arm")
    a("C2's figures are C1's plus its own, and C2 looks like it replays far MORE work than")
    a("C1 instead of less. Growth between checkpoints is what that quarter added.")
    a("")
    a("| mode | dataset | arm | cycles | compactions | records replayed | copied (MB) | garbage (MB) | placements enabled |")
    a("|---|---|---|---:|---:|---:|---:|---:|---:|")
    for mode, _ in MODES:
        for kind in KINDS:
            sel = [x for x in rows if x.get("mode") == mode and x["kind"] == kind
                   and x["run_tag"] == "D1"]
            for r in sorted(sel, key=lambda x: (x["arm"], x["frac"])):
                a("| %s | %s | %s | %d | %d | %d | %.3f | %.3f | %d |" % (
                    mode, kind, r["arm"], r["cycles"], r["compactions"], r["replayed"],
                    r["copied"] / 1048576.0, r["garbage"] / 1048576.0, r["helped"]))
    a("")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(w) + "\n")
    print("wrote %s (%d lines)" % (OUT, len(w)))


if __name__ == "__main__":
    main()

# -*- coding: utf-8 -*-
"""Candidate S targeted A/B analyzer (scan-entry round, 2026-10-10).

Reads results/sround_20261010/sr_<grid>.jsonl -- arms B / W / S / WS, five processes x
four balanced repetitions, arms rotated inside every repetition. Ratios are formed WITHIN
one process and one repetition, reduced to a per-process median, then reported across the
five processes with direction counts and the noise floor, exactly like the round's other
verdict tables. No ratio is ever multiplied out of a historical round.
"""
import collections, json, os, statistics, sys

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results", "sround_20261010")
GRIDS = {"sr_sc_lits_same_mixed": ("LitsHpt", "same", "mixed"),
         "sr_sc_ctx16_grow_tranco": ("Context16", "grow", "tranco"),
         "xsr_ctx16_same_mixed": ("Context16", "same", "mixed"),
         "xsr_lits_same_skew": ("LitsHpt", "same", "skew"),
         "xsr_ctx16_same_skew": ("Context16", "same", "skew"),
         "xsr_lits_same_tranco": ("LitsHpt", "same", "tranco"),
         "xsr_ctx16_same_tranco": ("Context16", "same", "tranco")}
MISSING_OK = True   # the extension grids may not exist yet; skip them silently
PAIRS = [("S", "B"), ("W", "B"), ("WS", "W"), ("WS", "S"), ("WS", "B")]
METRICS = ["seg_ns", "cum_ns", "hit_ns", "miss_ns", "scan16_ns", "scan256_ns",
           "scan4096_ns", "index_bytes", "overflow"]

def fail(msg):
    print("REFUSED: " + msg, file=sys.stderr)
    sys.exit(2)

for stem, grid in GRIDS.items():
    path = os.path.join(DIR, stem + ".jsonl")
    if not os.path.exists(path):
        if MISSING_OK and stem.startswith("xsr_"):
            continue
        fail("missing " + path)
    rows = [json.loads(x) for x in open(path, encoding="utf-8") if x.strip()]
    for r in rows:
        if (r["backend"], r["mode"], r["kind"]) != grid:
            fail("%s: foreign row" % stem)
    arms = sorted({r["arm"] for r in rows})
    tags = sorted({r["run_tag"] for r in rows})
    if arms != ["B", "S", "W", "WS"] or len(tags) < 3:
        fail("%s: arms %s tags %s" % (stem, arms, tags))

    def cell(tag, arm, frac, rep, metric):
        v = [r[metric] for r in rows if r["run_tag"] == tag and r["arm"] == arm
             and abs(r["frac"] - frac) < 1e-9 and r["rep"] == rep]
        return statistics.median(v) if v else None

    fracs = sorted({round(r["frac"], 2) for r in rows})
    print("=" * 112)
    print("%s  (backend/mode/corpus = %s)  processes=%s" % (stem, grid, tags))
    print("  paired ratios (median over processes; dir = processes with candidate faster/5)")

    for metric in METRICS:
        for frac in fracs:
            pieces = []
            for cand, base in PAIRS:
                per_proc = []
                for tag in tags:
                    reps = []
                    for rep in range(4):
                        c = cell(tag, cand, frac, rep, metric)
                        b = cell(tag, base, frac, rep, metric)
                        if c is None or b is None or b == 0:
                            continue
                        reps.append(c / b)
                    if reps:
                        per_proc.append(statistics.median(reps))
                if not per_proc:
                    pieces.append("%s/%s n/a" % (cand, base))
                    continue
                med = statistics.median(per_proc)
                dirs = sum(1 for x in per_proc if x < 1)
                pieces.append("%s/%s %.3f %d/%d" % (cand, base, med, dirs, len(per_proc)))
            print("  %-11s frac=%-5s | %s" % (metric, frac, " | ".join(pieces)))
        print()
    print("ANALYZE_SROUND", stem, "OK")

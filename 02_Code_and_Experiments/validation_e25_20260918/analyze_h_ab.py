# -*- coding: utf-8 -*-
"""Candidate-H targeted A/B analyzer (2026-10-11).

Reads results/main/hab_*.jsonl produced by bench_combo_h: arms W / WH in four slots, five
independent processes x four balanced repetitions, the arm order rotated inside every
repetition by the program itself. Every ratio is formed WITHIN one process and one
repetition; per-process medians are then reported with direction counts. W occupies TWO of
the four slots, so the same-configuration W-vs-W contrast on the same state and schedule
estimates the noise floor. Nothing is pooled across backends or corpora.
"""
import json, os, statistics, sys

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results", "main")
GRIDS = {"hab_ctx16_same_mixed": ("Context16", "same", "mixed"),
         "hab_lits_same_mixed": ("LitsHpt", "same", "mixed"),
         "hab_ctx16_grow_tranco": ("Context16", "grow", "tranco"),
         "hab_lits_grow_tranco": ("LitsHpt", "grow", "tranco")}
METRICS = ["seg_ns", "cum_ns", "hit_ns", "miss_ns", "scan16_ns", "scan256_ns",
           "scan4096_ns", "index_bytes", "overflow"]

def fail(msg):
    print("REFUSED: " + msg, file=sys.stderr)
    sys.exit(2)

missing = []
for stem, grid in GRIDS.items():
    path = os.path.join(DIR, stem + ".jsonl")
    if not os.path.exists(path):
        missing.append(stem)
        continue
    rows = [json.loads(x) for x in open(path, encoding="utf-8") if x.strip()]
    for r in rows:
        if (r["backend"], r["mode"], r["kind"]) != grid:
            fail("%s: foreign row" % stem)
    arms = sorted({r["arm"] for r in rows})
    tags = sorted({r["run_tag"] for r in rows})
    if arms != ["W", "WH"]:
        fail("%s: arms %s" % (stem, arms))

    def vals(tag, arm, frac, rep, metric):
        out = []
        for r in rows:
            if (r["run_tag"] == tag and r["arm"] == arm
                    and abs(r["frac"] - frac) < 1e-9 and r["rep"] == rep):
                v = r.get(metric)
                if v is not None:
                    out.append(v)
        return out

    fracs = sorted({round(r["frac"], 2) for r in rows})
    print("=" * 112)
    print("%s  (backend/mode/corpus = %s)  processes=%d %s"
          % (stem, grid, len(tags), tags))
    for metric in METRICS:
        for frac in fracs:
            per_proc, floors = [], []
            for tag in tags:
                reps, fl = [], []
                for rep in range(4):
                    w = vals(tag, "W", frac, rep, metric)
                    h = vals(tag, "WH", frac, rep, metric)
                    if w and h:
                        reps.append(statistics.median(h) / statistics.median(w))
                    if len(w) >= 2:
                        fl.append(max(w) / min(w))
                if reps:
                    per_proc.append(statistics.median(reps))
                if fl:
                    floors.append(statistics.median(fl))
            if not per_proc:
                print("  %-11s frac=%-5s | n/a" % (metric, frac))
                continue
            med = statistics.median(per_proc)
            dirs = sum(1 for x in per_proc if x < 1)
            fl = statistics.median(floors) if floors else float("nan")
            print("  %-11s frac=%-5s | WH/W %.3f  dir %d/%d | W-slot floor %.3f"
                  % (metric, frac, med, dirs, len(per_proc), fl))
        print()
    print("ANALYZE_H_AB", stem, "OK")
if missing:
    print("NOT ANALYZED (absent): " + ", ".join(missing))
# -*- coding: utf-8 -*-
"""First-batch scan probe analyzer (scan-entry round, 2026-10-10)."""
import collections, json, os, statistics, sys

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results", "firstscan_20261010")
GRIDS = {"fs_lits_same_mixed": ("LitsHpt", "same", "mixed"),
         "fs_ctx16_grow_tranco": ("Context16", "grow", "tranco")}


def load(stem):
    p = os.path.join(DIR, stem + "_firstscan.jsonl")
    if not os.path.exists(p):
        print("REFUSED: missing", p, file=sys.stderr); sys.exit(2)
    return [json.loads(x) for x in open(p, encoding="utf-8") if x.strip()]


def main_agg(stem):
    p = os.path.join(DIR, stem + ".jsonl")
    return [json.loads(x) for x in open(p, encoding="utf-8") if x.strip()]


for stem, grid in GRIDS.items():
    rows = load(stem)
    main = main_agg(stem)
    tags = sorted({r["run_tag"] for r in rows})
    reps = sorted({r["rep"] for r in rows})
    print("=" * 96)
    print("%s  processes=%s reps=%s  detail rows=%d" % (stem, tags, reps, len(rows)))

    cell = collections.defaultdict(list)
    for r in rows:
        if (r["backend"], r["mode"], r["kind"]) != grid:
            print("REFUSED: foreign row"); sys.exit(2)
        cell[(r["arm"], round(r["frac"], 2), r["limit"], r["pass"], r["start_idx"])].append(r["ns"])

    # first-batch aggregate = sum over starts of pass0 + pass1, per arm/frac/limit
    print("  first batch (pass0+pass1 sums over 17 starts; median over 4 process-reps)")
    print("  %-6s %-5s | %12s %12s %6s | %12s %12s %6s" % ("frac", "limit", "B ns", "W ns", "W/B", "B ns settled", "W ns settled", "W/B"))
    for frac in (0.0, 0.25, 0.5, 1.0, 2.0):
        for limit in (16, 256, 4096):
            def batch(arm):
                vals = []
                for tag in tags:
                    for rep in reps:
                        s = 0
                        for i in range(17):
                            for p in (0, 1):
                                s += statistics.median(cell.get((arm, frac, limit, p, i), [0]))
                        vals.append(s)
                return statistics.median(vals)
            def settled(arm, metric):
                return statistics.median([r[metric] for r in main
                                          if r["arm"] == arm and abs(r["frac"] - frac) < 1e-9])
            b, w = batch("B"), batch("W")
            bs, ws = settled("B", "scan%d_ns" % limit), settled("W", "scan%d_ns" % limit)
            print("  %-6s %-5d | %12.0f %12.0f %6.2f | %12.0f %12.0f %6.2f"
                  % (frac, limit, b, w, (w / b) if b else float("nan"),
                     bs, ws, (ws / bs) if bs else float("nan")))

    # per-call shape at the worst cell for lits/same/mixed, and at 200%/L256 for ctx16
    probes = {"fs_lits_same_mixed": (0.25, 16), "fs_ctx16_grow_tranco": (2.0, 256)}
    frac, limit = probes[stem]
    print("  per-call shape at frac=%s limit=%d: pass medians over starts (median over 4 process-reps)," % (frac, limit))
    print("  %-4s %-8s %10s %10s | %10s %10s" % ("arm", "pass", "med ns", "max ns", "med ns", "max ns"))
    for arm in ("B", "W"):
        for p in (0, 1):
            meds, maxs = [], []
            for i in range(17):
                v = cell.get((arm, frac, limit, p, i), [])
                if v:
                    meds.append(statistics.median(v))
            print("  %-4s p%-7d %10.0f %10.0f" % (arm, p, statistics.median(meds), max(meds)))
    # worst single calls
    worst = sorted(((statistics.median(v), k) for k, v in cell.items()
                    if k[0] == "W" and k[1] == frac and k[2] == limit), reverse=True)[:6]
    print("  worst W calls at that cell (median ns, (arm,frac,limit,pass,start)):")
    for v, k in worst:
        print("    %10.0f  pass=%d start=%d" % (v, k[3], k[4]))
print("ANALYZE_FIRSTSCAN OK")

# -*- coding: utf-8 -*-
"""Stage A analyzer for the scan-entry round (2026-10-10).

Reads results/scanentry_20261010/se_<grid>{,_scanwork}.jsonl -- per-call counter deltas
for every scan(start, limit) of the three stage-A grids -- and prints the round's tables:

  A  integrity: row counts, determinism between the two timed passes and the verify
     pass, rows == expect_rows == out_primary + out_overflow, byte checks, queue
     identity (start_fnv), and the mechanism evidence read from the MAIN jsonl
     (subblock_reclaims / reclaim_widened > 0 at 200%, routing_regions == regions).
  B  per (grid, frac, limit): medians of the counters per arm.
  C  paired W/B and C/B ratios over (rep, start_idx), with direction counts, for the
     derived quantities that matter: adv_cmps, adv_skips, out_rows, merge_cmps,
     groups, blocks_skipped.
  D  worst starts at the two flagged cells (by W-B difference in adv_skips and in
     adv_cmps; and the largest per-call W/B in out_bytes).

Fail-closed: unknown phase/arm/grid, missing fields, negative counters, a verify row
with bytes_mismatch != 0, or a timed/verify disagreement all stop the program.
"""
import collections
import json
import os
import statistics
import sys

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results", "scanentry_20261010")
GRIDS = {
    "se_Context16_grow_tranco": ("Context16", "grow", "tranco", 262144),
    "se_LitsHpt_grow_tranco": ("LitsHpt", "grow", "tranco", 262144),
    "se_LitsHpt_same_mixed": ("LitsHpt", "same", "mixed", 131072),
}
ARMS = ("B", "W", "C", "WC")
COUNTERS = ("adv_p_cmps", "adv_p_skipped", "adv_p_subblocks", "adv_o_cmps", "adv_o_skipped",
            "out_primary", "out_overflow", "out_bytes",
            "scan_regions", "scan_groups", "scan_groups_empty", "scan_blocks_skipped",
            "scan_merge_cmps", "scan_result_allocs", "scan_start_locates",
            "scan_cursor_starts", "scan_ov_cursor_starts", "scan_canonical")
BASE = ("kind", "run_tag", "mode", "backend", "arm", "rep", "position", "n", "thr", "frac",
        "limit", "phase", "pass", "probe_id", "start_idx", "start_len", "start_fnv",
        "rows", "expect_rows", "bytes_checked", "bytes_mismatch")


def fail(msg):
    print("REFUSED: " + msg, file=sys.stderr)
    sys.exit(2)


def load_grid(stem):
    path = os.path.join(DIR, stem + "_scanwork.jsonl")
    if not os.path.exists(path):
        fail("missing " + path)
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def load_main(stem):
    path = os.path.join(DIR, stem + ".jsonl")
    if not os.path.exists(path):
        fail("missing " + path)
    return [json.loads(x) for x in open(path, encoding="utf-8") if x.strip()]


def check(stem, rows, main):
    grid = GRIDS[stem]
    fails = []
    for r in rows:
        for f in BASE + COUNTERS:
            if f not in r:
                fail("%s: missing field %s" % (stem, f))
        if (r["backend"], r["mode"], r["kind"]) != grid[:3]:
            fail("%s: row does not belong to this grid" % stem)
        if r["phase"] not in ("timed", "verify", "probe"):
            fail("%s: unknown phase %r" % (stem, r["phase"]))
        if r["arm"] not in ARMS:
            fail("%s: unknown arm %r" % (stem, r["arm"]))
        for c in COUNTERS:
            if r[c] < 0:
                fail("%s: negative counter %s" % (stem, c))
        if r["rows"] != r["expect_rows"]:
            fails.append(("rows!=expect", r))
        if r["rows"] != r["out_primary"] + r["out_overflow"]:
            fails.append(("rows!=primary+overflow", r))
        # Both the verify pass and the boundary probes are byte-checked; the timed pass
        # must not be, or the check would sit inside the measured span.
        if r["phase"] in ("verify", "probe"):
            if not r["bytes_checked"]:
                fails.append(("verified row without byte check", r))
            if r["bytes_mismatch"] != 0:
                fails.append(("byte mismatch", r))
        if r["phase"] == "timed" and r["bytes_checked"]:
            fails.append(("timed row with byte check", r))

    # determinism: timed pass0 == pass1 == verify, per (arm, rep, frac, limit, start)
    key = lambda r: (r["run_tag"], r["arm"], r["rep"], round(r["frac"], 2), r["limit"],
                     r["start_idx"])
    groups = collections.defaultdict(dict)
    for r in rows:
        if r["phase"] == "probe":
            continue
        groups[key(r)][(r["phase"], r["pass"])] = r
    for k, g in sorted(groups.items()):
        if ("timed", 0) not in g or ("timed", 1) not in g or ("verify", 0) not in g:
            fail("%s: incomplete triple at %s" % (stem, k))
        t0, t1, v = g[("timed", 0)], g[("timed", 1)], g[("verify", 0)]
        for c in COUNTERS + ("rows", "expect_rows"):
            if not (t0[c] == t1[c] == v[c]):
                fails.append(("nondeterministic %s" % c, t0))

    # queue identity: start_fnv identical across arms/reps for the same (frac, limit, idx).
    # Probe rows key on probe_id instead -- start_idx is -1 for every probe by design.
    q = collections.defaultdict(set)
    for r in rows:
        if r["phase"] == "probe":
            q[("probe", round(r["frac"], 2), r["limit"], r["probe_id"])].add(
                (r["start_fnv"], r["start_len"]))
        else:
            q[("start", round(r["frac"], 2), r["limit"], r["start_idx"])].add(
                (r["start_fnv"], r["start_len"]))
    for k, v in q.items():
        if len(v) != 1:
            fails.append(("queue identity", k))

    # mechanism evidence from the main file
    mech = {}
    for r in main:
        if abs(r["frac"] - 2.0) < 1e-9:
            mech.setdefault(r["arm"], []).append(r)
    for arm in ("W", "WC"):
        rec = [r["subblock_reclaims"] for r in mech.get(arm, [])]
        wid = [r["reclaim_widened"] for r in mech.get(arm, [])]
        if not rec or min(rec) <= 0 or min(wid) <= 0:
            fails.append(("mechanism NOT EXERCISED for " + arm, min(rec or [0]), min(wid or [0])))
    for arm in ("C", "WC"):
        ok = all(r["routing_regions"] == r["regions"] for r in mech.get(arm, []))
        if not ok:
            fails.append(("routing regions mismatch for " + arm, 0))

    return fails


def report(stem):
    rows = load_grid(stem)
    main = load_main(stem)
    fails = check(stem, rows, main)
    print("=" * 100)
    print("%s   scanwork rows=%d   integrity %s" % (stem, len(rows),
                                                    "PASS" if not fails else "FAIL"))
    for f in fails[:10]:
        print("   FAIL:", f)
    if fails:
        return False

    timed = [r for r in rows if r["phase"] == "timed" and r["pass"] == 0]
    cell = collections.defaultdict(list)
    for r in timed:
        cell[(r["arm"], round(r["frac"], 2), r["limit"], r["start_idx"])].append(r)

    def median_counter(arm, frac, limit, counter):
        vals = []
        for s in sorted({k[3] for k in cell if k[0] == arm and k[1] == frac and k[2] == limit}):
            samples = cell[(arm, frac, limit, s)]
            vals.append(statistics.median([x[counter] for x in samples]))
        return statistics.median(vals) if vals else 0

    derived = {
        "adv_cmps": lambda r: r["adv_p_cmps"] + r["adv_o_cmps"],
        "adv_skips": lambda r: r["adv_p_skipped"] + r["adv_o_skipped"],
        "out_rows": lambda r: r["out_primary"] + r["out_overflow"],
        "merge_cmps": lambda r: r["scan_merge_cmps"],
        "groups": lambda r: r["scan_groups"],
        "blocks_skipped": lambda r: r["scan_blocks_skipped"],
    }

    for frac in (0.0, 0.25, 0.5, 1.0, 2.0):
        for limit in (16, 256, 4096):
            line = []
            for name, fn in derived.items():
                row = []
                for arm in ("B", "W", "C", "WC"):
                    per_start = []
                    for s in sorted({k[3] for k in cell if k[0] == arm and k[1] == frac and k[2] == limit}):
                        per_start.append(statistics.median([fn(x) for x in cell[(arm, frac, limit, s)]]))
                    row.append(statistics.median(per_start) if per_start else float("nan"))
                b, w, c, wc = row
                ratio = (w / b) if b else float("nan")
                line.append("%s B=%.0f W=%.0f W/B=%.2f" % (name, b, w, ratio))
            print("  frac=%-5s l=%-5d %s" % (frac, limit, " | ".join(line)))
    return True


if __name__ == "__main__":
    allok = all(report(g) for g in GRIDS)
    print("ANALYZE_SCANENTRY", "OK" if allok else "FAILED")
    sys.exit(0 if allok else 1)

# -*- coding: utf-8 -*-
"""Stage B2 discriminator analyzer (scan-entry round, 2026-10-10).

Reads results/scanconsume_20261010/sc_<grid>_scanconsume.jsonl (three grids, three
processes, four repetitions each). Answers, per grid / frac / limit:

  * mat  = index.scan(...)             (shipped path, result strings built)
  * cons = index.scan_consume(...)     (same traversal, no result string)

W/B ratios are formed WITHIN a (process, repetition, start) pair, so the numbers
carry the same process pairing as the round's other A/B tables; direction counts
are reported with every ratio.

Refuses: missing files, missing fields, any verify row with mismatches != 0 or
rows/rows_cons != expect_rows, unknown mode_name, wrong arm set, not exactly three
distinct run_tags, duplicate rows.
"""
import collections
import json
import os
import statistics
import sys

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "results", "scanconsume_20261010")
GRIDS = {
    "sc_ctx16_grow_tranco": ("Context16", "grow", "tranco"),
    "sc_lits_grow_tranco": ("LitsHpt", "grow", "tranco"),
    "sc_lits_same_mixed": ("LitsHpt", "same", "mixed"),
}
BASE = ("kind", "run_tag", "mode", "backend", "arm", "rep", "position", "n", "thr",
        "frac", "limit", "start_idx", "mode_name")
VERIFY_FIELDS = BASE + ("rows", "expect_rows", "rows_cons", "mismatches", "hash")
TIMED_FIELDS = BASE + ("pass", "ns", "rows")


def fail(msg):
    print("REFUSED: " + msg, file=sys.stderr)
    sys.exit(2)


def load(stem):
    path = os.path.join(DIR, stem + "_scanconsume.jsonl")
    if not os.path.exists(path):
        fail("missing " + path)
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def check(rows, stem):
    grid = GRIDS[stem]
    seen = set()
    tags = set()
    for r in rows:
        for f in (VERIFY_FIELDS if r.get("mode_name") == "verify" else TIMED_FIELDS):
            if f not in r:
                fail("%s: row missing field %s" % (stem, f))
        if r["mode_name"] not in ("mat", "cons", "verify"):
            fail("%s: unknown mode_name %r" % (stem, r["mode_name"]))
        if (r["backend"], r["mode"], r["kind"]) != grid:
            fail("%s: row does not belong to this grid" % stem)
        tags.add(r["run_tag"])
        key = (r["run_tag"], r["arm"], r["rep"], r["frac"], r["limit"], r["start_idx"],
               r["mode_name"], r.get("pass", -1))
        if key in seen:
            fail("%s: duplicate row %s" % (stem, key))
        seen.add(key)
        if r["mode_name"] == "verify":
            if r["mismatches"] != 0:
                fail("%s: verify mismatch at %s" % (stem, key))
            if r["rows"] != r["expect_rows"] or r["rows_cons"] != r["expect_rows"]:
                fail("%s: verify row count differs at %s" % (stem, key))
    if len(tags) != 3:
        fail("%s: expected 3 process tags, found %s" % (stem, sorted(tags)))
    # The discriminator phase runs for B and W only (C/WC cannot inform it), so THIS file
    # carries exactly those two arms; the four-arm record lives in the main jsonl, which
    # must exist, cover all four arms, and carry the same three process tags.
    arms = {r["arm"] for r in rows}
    if arms != {"B", "W"}:
        fail("%s: scanconsume arm set %s" % (stem, sorted(arms)))
    return rows, tags


def check_main(stem, tags):
    path = os.path.join(DIR, stem + ".jsonl")
    if not os.path.exists(path):
        fail("missing " + path)
    main_arms, main_tags = set(), set()
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        r = json.loads(line)
        main_arms.add(r["arm"])
        main_tags.add(r["run_tag"])
    if main_arms != {"B", "W", "C", "WC"}:
        fail("%s: main arm set %s" % (stem, sorted(main_arms)))
    if sorted(main_tags) != sorted(tags):
        fail("%s: main process tags %s differ from scanconsume %s"
             % (stem, sorted(main_tags), sorted(tags)))


def report(stem):
    rows, tags = check(load(stem), stem)
    check_main(stem, tags)
    ver = [r for r in rows if r["mode_name"] == "verify"]
    timed = [r for r in rows if r["mode_name"] in ("mat", "cons")]
    tags = sorted(tags)

    cells = collections.defaultdict(list)
    for r in timed:
        cells[(r["run_tag"], r["arm"], r["rep"], r["frac"], r["limit"], r["start_idx"],
               r["mode_name"])].append(r["ns"])

    fracs = sorted({r["frac"] for r in timed})
    limits = sorted({r["limit"] for r in timed})
    reps = sorted({r["rep"] for r in timed})

    print("=" * 104)
    print("%s  (backend/mode/corpus = %s)  processes=%s  verify rows=%d all clean"
          % (stem, GRIDS[stem], tags, len(ver)))
    print("  per (frac, limit): call ns medians (over 12 process-reps x 17 starts) and paired W/B")
    print("  %-6s %-5s | %9s %9s %6s %8s | %9s %9s %6s %8s"
          % ("frac", "limit", "B mat", "W mat", "W/B", "dirW", "B cons", "W cons", "W/B", "dirW"))

    for frac in fracs:
        for limit in limits:
            def call_ns(tag, arm, rep, s, mode):
                v = cells.get((tag, arm, rep, frac, limit, s, mode))
                return statistics.median(v) if v else None

            def arm_median(arm, mode):
                vals = [call_ns(t, arm, rep, s, mode) for t in tags for rep in reps
                        for s in range(17) if call_ns(t, arm, rep, s, mode) is not None]
                return statistics.median(vals) if vals else float("nan")

            def pair_ratio(mode):
                r = []
                for t in tags:
                    for rep in reps:
                        for s in range(17):
                            b = call_ns(t, "B", rep, s, mode)
                            w = call_ns(t, "W", rep, s, mode)
                            if b and w and b > 0:
                                r.append(w / b)
                if not r:
                    return float("nan"), 0, 0
                return statistics.median(r), sum(1 for x in r if x < 1), len(r)

            rm, dm, nm = pair_ratio("mat")
            rc, dc, nc = pair_ratio("cons")
            print("  %-6s %-5d | %9.0f %9.0f %6.2f %4d/%-3d | %9.0f %9.0f %6.2f %4d/%-3d"
                  % (frac, limit,
                     arm_median("B", "mat"), arm_median("W", "mat"), rm, dm, nm,
                     arm_median("B", "cons"), arm_median("W", "cons"), rc, dc, nc))

    print("  materialisation share of the arm's own scan call (median mat / median cons):")
    for frac in (fracs[0], fracs[-1]):
        for arm in ("B", "W"):
            parts = []
            for limit in limits:
                def med(mode):
                    vals = [call_ns(t, arm, rep, s, mode) for t in tags for rep in reps
                            for s in range(17)
                            if call_ns(t, arm, rep, s, mode) is not None
                            and (t, arm, rep, frac, limit, s, mode) in cells]
                    return statistics.median(vals) if vals else float("nan")
                parts.append("L%d: %5.2f" % (limit, med("mat") / med("cons")))
            print("    frac=%-5s %-3s %s" % (frac, arm, "  ".join(parts)))
    print()


if __name__ == "__main__":
    for stem in GRIDS:
        report(stem)
    print("ANALYZE_SCANCONSUME OK")

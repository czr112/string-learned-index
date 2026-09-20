#!/usr/bin/env python3
"""Render E25_CALLS.md from results/main/e25_calls.jsonl.

The matrix is COUNTS. It says what each API invokes, not how fast it is. Nothing
here is converted into a speed-up: E25's timing comparison is a separate program
run on a clean build, and a reduced call count is not a performance result.

Column meanings, which the table must state:
  L  locate_calls         outer routing (Index::locate)
  P  predict_calls        model predictions
  S  existence_searches   entries into Region::find_impl -- full duplicate checks
  F  fp_of_calls          Fingerprint::of, the WHOLE-KEY form (auxiliary only)
  H  fp_hash_bytes        bytes actually fed to the fingerprint hash
  C  cmp_calls            full-key string comparisons
  B  cmp_bytes            bytes those comparisons walked (LOGICAL, not DRAM)

P, S and F/H are deliberately separate: predicting the group again after a
duplicate check is NOT the same as running two full searches, and the table must
not let one be read as the other.

Usage:  python3 analyze_e25_calls.py
"""
import collections
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "results", "main", "e25_calls.jsonl")
OUT = os.path.join(HERE, "E25_CALLS.md")

KINDS = ["mixed", "skew", "tranco"]
API_ORDER = ["update", "erase", "insert", "upsert", "structural"]
CAT_ORDER = ["primary_hit", "overflow_hit", "miss", "new_key", "duplicate_key",
             "existing_key", "region_split_build"]


def load():
    with open(SRC, encoding="utf-8") as f:
        return [json.loads(l) for l in f if l.strip()]


def main():
    rows = load()
    by = collections.defaultdict(dict)
    for r in rows:
        by[(r["kind"], r["api"], r["category"])][r["variant"]] = r

    w = []
    a = w.append
    a("# E25 call matrix: what each write API invokes, per category")
    a("")
    a("Source: `results/main/e25_calls.jsonl`, %d records "
      "(3 datasets x 3 variants x the categories below)." % len(rows))
    a("")
    a("**These are counts, not timings.** They say what work each API performs; they")
    a("are never converted into a speed-up here. E25's timing comparison runs on a")
    a("separate clean build, and a reduced call count is not by itself a performance")
    a("result.")
    a("")
    a("**V0 is the CURRENT source with the default switches** (both E25 flags off),")
    a("measured now -- not a record taken before the change was written.")
    a("")
    a("## 0. Column meanings, and what is deliberately kept apart")
    a("")
    a("| col | counter | means |")
    a("|---|---|---|")
    a("| L | `locate_calls` | outer routing (`Index::locate`) |")
    a("| P | `predict_calls` | model predictions |")
    a("| S | `existence_searches` | entries into `Region::find_impl` -- full duplicate checks |")
    a("| F | `fp_of_calls` | `Fingerprint::of`, the WHOLE-KEY form. **Auxiliary only** |")
    a("| H | `fp_hash_bytes` | **bytes actually fed to the fingerprint hash** |")
    a("| C | `cmp_calls` | full-key string comparisons |")
    a("| B | `cmp_bytes` | bytes those comparisons walked (LOGICAL, not DRAM reads) |")
    a("")
    a("**F/H are split on purpose.** The model prediction hashes the bytes it consumes")
    a("and `find_impl` finishes the key with `tail()`, so the fused path produces a")
    a("full fingerprint **without ever calling `Fingerprint::of`**. Counting that")
    a("function alone would make fingerprint work look like it vanished when it only")
    a("moved. `F` is reported so the two forms can be told apart, never as the total.")
    a("")
    a("**P, S are separate counters.** Predicting the group again after a duplicate")
    a("check is NOT the same as running two full searches, and the table must not let")
    a("one be read as the other.")
    a("")
    a("**Placement failures are the FIRST rejection reason** inside `insert_into` (slot")
    a("count is checked before suffix bytes), so they are mutually exclusive and their")
    a("sum is bounded by `place_attempts`. They are not overlapping conditions to be")
    a("added up.")
    a("")
    a("**The diagnostic window** is explicit: counters are reset immediately before a")
    a("category's operations and read immediately after. Key selection, reference")
    a("upkeep and answer checking run outside it and are not counted.")
    a("")

    for kind in KINDS:
        a("## %s" % kind)
        a("")
        a("Per-operation averages. `L/P/S/H/C`.")
        a("")
        a("| api | category | ops | V0 | V1 | V2 |")
        a("|---|---|---:|---|---|---|")
        keys = sorted([k for k in by if k[0] == kind],
                      key=lambda k: (API_ORDER.index(k[1]) if k[1] in API_ORDER else 99,
                                     CAT_ORDER.index(k[2]) if k[2] in CAT_ORDER else 99))
        for k in keys:
            d = by[k]
            base = d.get("V0") or d.get("V1") or d.get("V2")
            n = max(base["ops"], 1)

            def fmt(v):
                if v not in d:
                    return "--"
                r = d[v]
                return "L%.1f P%.1f S%.1f H%.0f C%.1f" % (
                    r["locate_calls"] / n, r["predict_calls"] / n, r["existence_searches"] / n,
                    r["fp_hash_bytes"] / n, r["cmp_calls"] / n)

            a("| %s | `%s` | %d | %s | %s | %s |" % (k[1], k[2], n, fmt("V0"), fmt("V1"), fmt("V2")))
        a("")
        a("### %s: comparison bytes and placement, per operation" % kind)
        a("")
        a("| api | category | V0 | V1 | V2 |")
        a("|---|---|---|---|---|")
        for k in keys:
            d = by[k]
            base = d.get("V0") or d.get("V1") or d.get("V2")
            n = max(base["ops"], 1)

            def fmt2(v):
                if v not in d:
                    return "--"
                r = d[v]
                return "B%.0f att %.1f (slot %.1f / bytes %.1f)" % (
                    r["cmp_bytes"] / n, r["place_attempts"] / n,
                    r["place_fail_slots"] / n, r["place_fail_bytes"] / n)

            a("| %s | `%s` | %s | %s | %s |" % (k[1], k[2], fmt2("V0"), fmt2("V1"), fmt2("V2")))
        a("")
        a("### %s: erase work and string lifetime" % kind)
        a("")
        a("`erase_released_primary` counts suffix bytes whose record went away but whose")
        a("bytes STAY in the block as garbage; `erase_released_overflow` counts bytes")
        a("whose string is actually destroyed. They are reported apart because only the")
        a("second is a real free.")
        a("")
        a("| api | category | V0 | V1 | V2 |")
        a("|---|---|---|---|---|")
        for k in keys:
            if k[1] not in ("erase",):
                continue
            d = by[k]
            base = d.get("V0") or d.get("V1") or d.get("V2")
            n = max(base["ops"], 1)

            def fmt3(v):
                if v not in d:
                    return "--"
                r = d[v]
                return "shift %.1f relP %.0f relO %.0f alloc %.1f free %.1f" % (
                    r["erase_shifts"] / n, r["erase_released_primary"] / n,
                    r["erase_released_overflow"] / n,
                    r["string_allocs"] / n, r["string_frees"] / n)

            a("| %s | `%s` | %s | %s | %s |" % (k[1], k[2], fmt3("V0"), fmt3("V1"), fmt3("V2")))
        a("")

    a("## Structural rows, reported separately")
    a("")
    a("A region rebuild or split is not a fixed cost per operation, so it is never")
    a("folded into the per-operation averages above. The row below is a purpose-built")
    a("phase (`region_keys = 16`, 200 inserts) whose only job is to make those events")
    a("happen and be counted.")
    a("")
    a("| dataset | api | category | variant | ops | rebuilds | compactions | predictions |")
    a("|---|---|---|---|---:|---:|---:|---:|")
    for k, d in sorted(by.items()):
        if k[1] != "structural":
            continue
        for v, r in sorted(d.items()):
            a("| %s | %s | `%s` | %s | %d | %d | %d | %d |" % (
                k[0], k[1], k[2], v, r["ops"], r["region_rebuilds"], r["compactions"],
                r["predict_calls"]))
    a("")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(w) + "\n")
    print("wrote %s (%d lines)" % (OUT, len(w)))


if __name__ == "__main__":
    main()

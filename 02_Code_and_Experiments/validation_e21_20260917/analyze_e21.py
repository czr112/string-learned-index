#!/usr/bin/env python3
"""E21 analysis: turn the sweep results into E21_TABLES.md.

Sections:
  1. the digest gate -- the two S methods must be the same structure;
  2. point-query cost by class, and the three ratios that matter;
  3. cross-process stability;
  4. build time and space.

Query timings come from the clean build only; ART's space comes from the
diagnostic build.
"""
import csv
import json
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
KINDS = ["mixed", "skew", "tranco"]
KEYS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
CLASSES = ["all", "hit", "miss"]
METHODS = ["ART_armong", "S_frozen_R1", "S_original_two_pass", "sorted_array"]
SHORT = {"ART_armong": "ART", "S_frozen_R1": "S_frozen", "S_original_two_pass": "S_original",
         "sorted_array": "sorted_array"}


def med(xs):
    return st.median(xs) if xs else float("nan")


def load_trim(path):
    if not os.path.exists(path):
        return []
    out = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["run_tag"].startswith("warmup"):
                continue
            out.append(r)
    return out


def main():
    rows = load_trim(os.path.join(HERE, "results/main/e21_timings.csv"))
    builds = [json.loads(l) for l in open(os.path.join(HERE, "results/main/e21_build.jsonl"))]
    clean_builds = [b for b in builds if b["run_tag"].startswith("P")]
    mem_builds = [b for b in builds if b["run_tag"] == "MEM"]
    out = []
    w = out.append

    # per (kind, class, method): median over ALL rows (3 seeds x 5 rounds x processes)
    per = {}
    for r in rows:
        per.setdefault((r["kind"], r["class"], r["method"]), []).append(float(r["ns_per_query"]))

    w("# E21 tables: point queries on the frozen default")
    w("")
    w(f"Source: `results/main/e21_timings.csv` ({len(rows)} timed rows = 5 processes x 3 seeds x "
      f"5 rounds x 3 query classes x 4 methods) and `results/main/e21_build.jsonl`.")
    w("")
    w("| method | what it is |")
    w("|---|---|")
    w("| `sorted_array` | `std::lower_bound` over the shared sorted keys, which it does NOT copy |")
    w("| `ART` | native armong/libart |")
    w("| `S_original` | the two-pass construction (`bulk_load` + `rebuild_with_caps`) |")
    w("| **`S_frozen`** | **the frozen default**: statistics -> plan -> direct build + R1 |")
    w("")
    w("Every method is verified query by query outside the timed region, then rounds are shuffled,")
    w("warmed and checksummed. Timings are ns per query, median over every batch. Data, query")
    w("sequence, leaf config, routing and overflow policy are identical across methods.")
    w("")

    # ---- 1. digest -------------------------------------------------------
    w("## 1. Gate: the two S methods are the same structure")
    w("")
    w("`S_frozen` optimises CONSTRUCTION. It is not expected to query faster than `S_original`, and")
    w("it should not be: the two build the same structure, which is asserted by digest before any")
    w("query is timed. Any timing difference between them is therefore noise, and the round's job")
    w("is to CONFIRM that rather than assume it.")
    w("")
    ok = True
    w("| dataset | S digest, frozen vs original |")
    w("|---|---|")
    for k in KINDS:
        ds = {b["s_digest"] for b in clean_builds if b["kind"] == k}
        ok &= (len(ds) == 1)
        w(f"| {k} | {'**identical**' if len(ds) == 1 else '**DIFFERENT**'} |")
    w("")

    # ---- 2. query cost ---------------------------------------------------
    w("## 2. Point-query cost (ns/query)")
    w("")
    for k in KINDS:
        w(f"### `{k}`")
        w("")
        w("| class | " + " | ".join(SHORT[m] for m in METHODS) + " |")
        w("|---|---:|" + "---:|" * (len(METHODS) - 1))
        for cls in CLASSES:
            w(f"| {cls} | " + " | ".join(f"{med(per.get((k, cls, m), [])):.1f}" for m in METHODS) + " |")
        w("")
        w("| ratio | all | hit | miss |")
        w("|---|---:|---:|---:|")
        for lab, num, den in [("**S_frozen / ART**", "S_frozen_R1", "ART_armong"),
                              ("**S_frozen / sorted_array**", "S_frozen_R1", "sorted_array"),
                              ("S_frozen / S_original", "S_frozen_R1", "S_original_two_pass")]:
            cells = []
            for cls in CLASSES:
                a = med(per.get((k, cls, num), []))
                b = med(per.get((k, cls, den), []))
                cells.append(f"{a / b:.3f}x")
            w(f"| {lab} | " + " | ".join(cells) + " |")
        w("")

    w("### The three classes are three LOADS, not a decomposition")
    w("")
    w("`hits` and `misses` are separate query vectors, each timed as its own pass -- they are not")
    w("the mixed run split into two halves. So the three columns answer \"how does S compare with")
    w("ART on this KIND of load\", not \"where does the mixed time go\". E13 documented the same")
    w("property for its own three classes.")
    w("")
    w("The check below makes that concrete: `(hit + miss) / 2` is what the mixed load would cost")
    w("if the two halves simply averaged, and the measured mixed load is well above it.")
    w("")
    w("| dataset | method | all | hit | miss | (hit+miss)/2 | all / that |")
    w("|---|---|---:|---:|---:|---:|---:|")
    for k in KINDS:
        for mm in ["ART_armong", "S_frozen_R1"]:
            a = med(per.get((k, "all", mm), []))
            hh = med(per.get((k, "hit", mm), []))
            ms_ = med(per.get((k, "miss", mm), []))
            imp = (hh + ms_) / 2
            w(f"| {k} | {SHORT[mm]} | {a:.1f} | {hh:.1f} | {ms_:.1f} | {imp:.1f} | **{a / imp:.3f}x** |")
    w("")
    w("The weighting above is exact, not approximate: `make_queries` sets `hit = (i % 2 == 0)`,")
    w("so every batch is exactly 50% hits (the run logs confirm `hits=65536 misses=65536`).")
    w("")
    w("**The mixing penalty is larger for S than for ART**, and that is what decides the mixed")
    w("column. **It is a descriptive quantity, not an explanation** -- cache state, branch")
    w("behaviour, working-set size and execution order can all contribute, and this round does")
    w("not separate them, so no single mechanism is claimed:")
    w("")
    w("| dataset | S penalty (all / implied) | ART penalty | S - ART |")
    w("|---|---:|---:|---:|")
    for k in KINDS:
        aS = med(per.get((k, "all", "S_frozen_R1"), []))
        iS = (med(per.get((k, "hit", "S_frozen_R1"), [])) + med(per.get((k, "miss", "S_frozen_R1"), []))) / 2
        aA = med(per.get((k, "all", "ART_armong"), []))
        iA = (med(per.get((k, "hit", "ART_armong"), [])) + med(per.get((k, "miss", "ART_armong"), []))) / 2
        w(f"| {k} | {aS / iS:.3f}x | {aA / iA:.3f}x | {aS / iS - aA / iA:+.3f} |")
    w("")
    w("**`skew` is where this is decisive.** S is faster on BOTH homogeneous loads --")
    w(f"hit {med(per.get(('skew', 'hit', 'S_frozen_R1'), [])) / med(per.get(('skew', 'hit', 'ART_armong'), [])):.3f}x, "
      f"miss {med(per.get(('skew', 'miss', 'S_frozen_R1'), [])) / med(per.get(('skew', 'miss', 'ART_armong'), [])):.3f}x "
      "-- yet slower on the mixed load")
    w("because its mixing penalty is far larger. **So the three columns must not be read as")
    w("\"where S loses\": they are three loads, and the mixed column is not implied by the other")
    w("two.** A summary like \"the gap is concentrated in hits\" does not follow, and is withdrawn.")
    w("")
    w("What can be said is only that **mixing hits and misses costs S proportionally more than it")
    w("costs ART**, most sharply on `skew`. Why is not established here.")
    w("")

    # ---- 3. stability ----------------------------------------------------
    w("## 3. Cross-process stability")
    w("")
    w("Ordering of the four methods, per dataset and class. The two S methods are the same")
    w("structure and are expected to interleave.")
    w("")
    w("| dataset | class | fastest -> slowest |")
    w("|---|---|---|")
    for k in KINDS:
        for cls in CLASSES:
            vals = {m: med(per.get((k, cls, m), [])) for m in METHODS}
            order = sorted(vals, key=lambda m: vals[m])
            w(f"| {k} | {cls} | " + " < ".join(SHORT[m] for m in order) + " |")
    w("")

    # ---- 4. build and space ---------------------------------------------
    w("## 4. Build time and space")
    w("")
    w("**Build time is where the frozen version differs from `S_original`** -- that is what R1 and")
    w("the construction changes were for. Query cost is not.")
    w("")
    w("| method | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for m in METHODS:
        vals = []
        for k in KINDS:
            xs = [mm["build_ms"] for b in clean_builds if b["kind"] == k for mm in b["methods"]
                  if mm["name"] == m]
            vals.append(f"{med(xs):.2f}")
        w(f"| {SHORT[m]} build (ms) | " + " | ".join(vals) + " |")
    w("")
    w("| method | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for m in METHODS:
        vals = []
        for k in KINDS:
            xs = [mm["bytes"] for b in (mem_builds or clean_builds) if b["kind"] == k
                  for mm in b["methods"] if mm["name"] == m and mm["bytes_known"]]
            vals.append(f"{med(xs) / 1e6:.2f}" if xs else "n/a")
        w(f"| {SHORT[m]} space (MB) | " + " | ".join(vals) + " |")
    w("")
    w("Space accounting differs by method and the numbers are not directly comparable across")
    w("rows: `S_*` use the prototype's own `memory()` (allocator metadata excluded, not RSS),")
    w("`ART` is measured by wrapping its allocator, and `sorted_array` counts the shared key")
    w("vectors it does not copy. The per-row caveat matters more than the comparison.")
    w("")

    with open(os.path.join(HERE, "E21_TABLES.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote E21_TABLES.md ({len(out)} lines); digest gate {'OK' if ok else 'FAILED'}")


if __name__ == "__main__":
    main()

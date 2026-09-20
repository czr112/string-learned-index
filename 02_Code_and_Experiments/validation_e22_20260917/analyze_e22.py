#!/usr/bin/env python3
"""E22 analysis: turn results/main/e22_dynamic.jsonl into E22_TABLES.md.

Sections:
  1. THE CONTROL -- update_only must not move anything structural. If it does,
     the implementation or the measurement is wrong, and nothing below is worth
     reading.
  2. structural metrics per workload and checkpoint;
  3. space over time, and how long the skew advantage lasts;
  4. query cost at the checkpoints, on both query sets, with the noise floor;
  5. update cost.

Timings come from the clean build; ART's running footprint comes from the separate
instrumented build.
"""
import json
import os
import statistics as st
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
KINDS = ["mixed", "skew", "tranco"]
KEYS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
WORKLOADS = ["update_only", "insert_only", "replace"]


def med(xs):
    return st.median(xs) if xs else float("nan")


def main():
    raw = [json.loads(l) for l in open(os.path.join(HERE, "results/main/e22_dynamic.jsonl"))]
    cl = [r for r in raw if r["run_tag"].startswith("P")]
    mm = [r for r in raw if r["run_tag"] == "MEM"]

    by = defaultdict(list)
    for r in cl:
        by[(r["kind"], r["workload"], round(r["checkpoint"]["fraction_of_initial_keys"], 3))].append(r)
    bym = defaultdict(list)
    for r in mm:
        bym[(r["kind"], r["workload"], round(r["checkpoint"]["fraction_of_initial_keys"], 3))].append(r)

    def M(k, f, src=None):
        v = [f(r) for r in (src or by).get(k, [])]
        return med(v) if v else float("nan")

    out = []
    w = out.append

    w("# E22 tables: the frozen index under an update load")
    w("")
    w(f"Source: `results/main/e22_dynamic.jsonl` ({len(cl)} measured records = "
      f"{len(cl) // (len(KINDS) * len(WORKLOADS) * 4)} processes x {len(KINDS)} datasets x "
      f"{len(WORKLOADS)} workloads x 4 checkpoints, plus {len(mm)} instrumented records).")
    w("")
    w("| workload | what it does |")
    w("|---|---|")
    w("| **`update_only`** | **THE CONTROL.** Every key exists; only the value changes. Record count, capacity tables and the overflow count must NOT move. |")
    w("| `insert_only` | new keys that do not exist, anchored on real keys so they spread over the key space rather than piling up at one end |")
    w("| `replace` | one erase + one insert per cycle, holding the live record count constant |")
    w("")
    w("Each workload starts from its OWN fresh initial state. The timed update span contains")
    w("only index operations -- query verification, key generation and accounting are all")
    w("outside it. A replace cycle is reported as one cycle AND two operations.")
    w("")
    w("**This round observes only. No maintenance policy is changed.**")
    w("")

    # ---- 1. control ------------------------------------------------------
    w("## 1. The control")
    w("")
    w("`update_only` at every checkpoint against its own start:")
    w("")
    w("| dataset | records | overflow | capacity tables changed | compactions | rebuilds | splits | space |")
    w("|---|---|---|---|---|---|---|---|")
    ok = True
    for k in KINDS:
        cells = []
        for field, get in [("records", lambda r: r["records"]),
                           ("overflow", lambda r: r["overflow_records"]),
                           ("fallback", lambda r: r["fallback"]["regions"]),
                           ("compactions", lambda r: r["compactions"]),
                           ("rebuilds", lambda r: r["rebuilds"]),
                           ("splits", lambda r: r["splits"]),
                           ("space", lambda r: r["space_prototype"])]:
            const = True
            for f in (0.05, 0.10, 0.25):
                if M((k, "update_only", f), lambda r: get(r)) != M((k, "update_only", 0.0), lambda r: get(r)):
                    const = False
            ok &= const
            cells.append("**unchanged**" if const else "**CHANGED**")
        w(f"| {k} | " + " | ".join(cells) + " |")
    w("")
    if ok:
        w("**The control holds exactly on every dataset: nothing structural moves under")
        w("value-only updates.** That is what makes the rest of the round readable -- the")
        w("changes reported below can be attributed to the operations, not to the measurement.")
    else:
        w("**THE CONTROL FAILED. Something structural moved under value-only updates. That is a")
        w("defect in the implementation or in the statistics, NOT degradation -- fix it before")
        w("reading anything else.**")
    w("")

    # ---- 2. structural ---------------------------------------------------
    w("## 2. Structural metrics")
    w("")
    for k in KINDS:
        w(f"### `{k}` ({KEYS[k]:,} initial keys)")
        w("")
        w("| workload | checkpoint | records | overflow | overflow share | compactions | rebuilds | splits | **fallback regions** | fallback record share | fallback query share (set A) |")
        w("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
        for wl in WORKLOADS:
            for f in (0.0, 0.05, 0.10, 0.25):
                kk = (k, wl, f)
                rec = M(kk, lambda r: r["records"])
                ovf = M(kk, lambda r: r["overflow_records"])
                w(f"| `{wl}` | {f:.0%} | {rec:,.0f} | {ovf:,.0f} | {100 * ovf / rec:.1f}% | "
                  f"{M(kk, lambda r: r['compactions']):,.0f} | {M(kk, lambda r: r['rebuilds']):,.0f} | "
                  f"{M(kk, lambda r: r['splits']):,.0f} | "
                  f"**{M(kk, lambda r: r['fallback']['regions']):,.0f}** | "
                  f"{100 * M(kk, lambda r: r['fallback']['record_share']):.1f}% | "
                  f"{100 * M(kk, lambda r: r['fallback']['query_share_setA']):.1f}% |")
        w("")
    w("**Capacity fallback never happens.** A rebuild is what would replace a region with the")
    w("uniform default, and no rebuild occurs under any of these workloads: the inserts are")
    w("anchored on real keys, so no region's prefix changes and no split is forced. **The")
    w("demand layout is preserved; the channel through which maintenance shows up is OVERFLOW")
    w("GROWTH**, not capacity reverting.")
    w("")
    w("That also means the two coverage metrics come out at zero, and they are reported anyway:")
    w("had the layout reverted, they are what would have said how much data and how much traffic")
    w("was affected.")
    w("")

    # ---- 3. space --------------------------------------------------------
    w("## 3. Space over time (MB)")
    w("")
    for k in KINDS:
        w(f"### `{k}`")
        w("")
        w("| workload | checkpoint | prototype | ART | prototype / ART |")
        w("|---|---|---:|---:|---:|")
        for wl in WORKLOADS:
            for f in (0.0, 0.05, 0.10, 0.25):
                kk = (k, wl, f)
                p = M(kk, lambda r: r["space_prototype"]) / 1e6
                a = M(kk, lambda r: r["space_art_live"], bym) / 1e6
                w(f"| `{wl}` | {f:.0%} | {p:.2f} | {a:.2f} | **{p / a:.2f}x** |")
        w("")
    w("### How long does the `skew` advantage last?")
    w("")
    w("E21 measured the prototype **14% smaller** than ART on `skew` at rest. Over the load:")
    w("")
    w("| workload | 0% | 5% | 10% | 25% |")
    w("|---|---:|---:|---:|---:|")
    for wl in WORKLOADS:
        cells = []
        for f in (0.0, 0.05, 0.10, 0.25):
            kk = ("skew", wl, f)
            p = M(kk, lambda r: r["space_prototype"]) / 1e6
            a = M(kk, lambda r: r["space_art_live"], bym) / 1e6
            cells.append(f"{p / a:.2f}x")
        w(f"| `{wl}` | " + " | ".join(cells) + " |")
    w("")
    w("**Value-only updates: it holds exactly** (nothing moves on either side).")
    w("**Inserts: it survives, and widens** -- both grow, the prototype more slowly.")
    w("**Replacement: it is gone by the 25% checkpoint** -- the prototype grows while ART stays")
    w("flat, because inserted keys land in overflow while erased ones leave their bytes behind.")
    w("")

    # ---- 4. queries ------------------------------------------------------
    w("## 4. Query cost at the checkpoints (ns/query)")
    w("")
    w("Two query sets, reported separately:")
    w("")
    w("* **A: a FIXED key sequence.** The same access demand at every checkpoint; the true")
    w("  answer is recomputed from the live state, so the hit rate falls as keys are replaced.")
    w("  This is the one that shows whether the SAME demand gets slower.")
    w("* **B: regenerated to a fixed 50% hit ratio** from the current live keys and keys verified")
    w("  absent. This keeps the hit rate from masking a layout change.")
    w("")
    w("**Noise floor**: the 0% checkpoint is the same untouched state, measured once per workload,")
    w("so its spread across those measurements is what a difference has to beat.")
    w("")
    floor = []
    for k in KINDS:
        for setname in ["queryA", "queryB"]:
            for meth in ["ns_prototype", "ns_art"]:
                vals = [M((k, wl, 0.0), lambda r: r[setname][meth]) for wl in WORKLOADS]
                floor.append((100 * (max(vals) - min(vals)) / med(vals)))
    w(f"At rest, the same state measured 9 times (3 datasets x 3 workloads x 2 sets x 2 methods)")
    w(f"spreads by a median of **{med(floor):.0f}%** and up to **{max(floor):.0f}%**.")
    w("")
    for k in KINDS:
        w(f"### `{k}`")
        w("")
        w("| workload | checkpoint | A: proto | A: ART | A: ratio | A hit | B: proto | B: ART | B: ratio |")
        w("|---|---|---:|---:|---:|---:|---:|---:|---:|")
        for wl in WORKLOADS:
            for f in (0.0, 0.05, 0.10, 0.25):
                kk = (k, wl, f)
                ap = M(kk, lambda r: r["queryA"]["ns_prototype"])
                aa = M(kk, lambda r: r["queryA"]["ns_art"])
                hit = M(kk, lambda r: r["queryA"]["hits"] / r["queryA"]["n"])
                bp = M(kk, lambda r: r["queryB"]["ns_prototype"])
                ba = M(kk, lambda r: r["queryB"]["ns_art"])
                w(f"| `{wl}` | {f:.0%} | {ap:.0f} | {aa:.0f} | {ap / aa:.2f}x | {100 * hit:.1f}% | "
                  f"{bp:.0f} | {ba:.0f} | {bp / ba:.2f}x |")
        w("")

    # ---- 5. update cost --------------------------------------------------
    w("## 5. Update cost (ms per segment, median across processes)")
    w("")
    w("The timed span contains only index operations. A replace cycle is two operations.")
    w("")
    for k in KINDS:
        w(f"### `{k}`")
        w("")
        w("| workload | checkpoint | cycles | ops | prototype (ms) | ART (ms) |")
        w("|---|---|---:|---:|---:|---:|")
        for wl in WORKLOADS:
            for f in (0.05, 0.10, 0.25):
                kk = (k, wl, f)
                w(f"| `{wl}` | {f:.0%} | {M(kk, lambda r: r['checkpoint']['cycles']):,.0f} | "
                  f"{M(kk, lambda r: r['checkpoint']['ops']):,.0f} | "
                  f"{M(kk, lambda r: r['update_ms']):.1f} | {M(kk, lambda r: r['update_ms_art']):.1f} |")
        w("")

    with open(os.path.join(HERE, "E22_TABLES.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote E22_TABLES.md ({len(out)} lines); control {'OK' if ok else 'FAILED'}")


if __name__ == "__main__":
    main()

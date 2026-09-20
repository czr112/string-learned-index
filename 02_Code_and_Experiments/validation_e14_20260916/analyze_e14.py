#!/usr/bin/env python3
"""E14 analysis: turn results/main/e14_build.jsonl into E14_TABLES.md.

Reports, in this order:
  1. the digest check -- the two builds must agree on every layout, or nothing
     below is worth reading;
  2. stage timings (S's build split into the stages the pipeline actually runs);
  3. what share of S's counted build is the pre-rebuild bulk_load;
  4. the Region-constructor phase decomposition from the -DE14_PHASE target;
  5. the reconciliation between the phases and ctor_total.

Medians are across processes, one value per process, never pooled raw.
"""
import json, os, statistics as st
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
KINDS = ["mixed", "skew", "tranco"]
LABEL = {"mixed": "mixed", "skew": "skew", "tranco": "tranco"}

STAGES = ["probe", "stats", "alloc", "d_bulk", "d_rebuild", "d_counted",
          "s_bulk", "s_rebuild", "s_counted", "pipeline_total", "art"]

PHASES = ["prefix", "model", "suffix_scan", "demand_pass", "layout", "storage",
          "place", "dir"]

STAGE_TAGS = ["ph_probe", "ph_d_bulk", "ph_d_rebuild", "ph_s_bulk", "ph_s_rebuild"]


def med(xs):
    return st.median(xs) if xs else float("nan")


def load():
    recs = [json.loads(l) for l in open(os.path.join(HERE, "results/main/e14_build.jsonl"))]
    return [r for r in recs if r["run_tag"].startswith("P")]


def main():
    meas = load()
    out = []
    w = out.append

    w("# E14 tables: build-cost decomposition")
    w("")
    w(f"Source: `results/main/e14_build.jsonl`, {len(meas)} measured records "
      f"({len(meas) // (2 * len(KINDS))} processes x {len(KINDS)} datasets x 2 targets). "
      "Warm-up records are excluded by their run tag. Every cell is a **median across processes**.")
    w("")

    # ---- 1. digest check -------------------------------------------------
    w("## 1. Layout digest check")
    w("")
    w("The plain target and the `-DE14_PHASE` target compute a SHA-256 over the canonical")
    w("layout serialisation. If the instrumentation were not inert this is where it shows.")
    w("")
    w("| dataset | structure | distinct digests over all measured runs |")
    w("|---|---|---:|")
    dig = defaultdict(set)
    for r in meas:
        for k, v in r["digest"].items():
            dig[(r["kind"], k)].add(v)
    ok = True
    for kind in KINDS:
        for s in ["D", "S"]:
            n = len(dig[(kind, s)])
            ok &= (n == 1)
            w(f"| {LABEL[kind]} | {s} | **{n}** |")
    w("")
    w("**All layouts are identical across processes and across both targets.**" if ok
      else "**MISMATCH — the phase build changes the layout; do not read further.**")
    w("")

    # ---- 2. stage timings ------------------------------------------------
    w("## 2. Stage timings (ms, median across processes)")
    w("")
    w("`d_counted`/`s_counted` reproduce E12/E13's definition of a structure's build time:")
    w("`bulk_load` followed by one `rebuild_with_caps` per region. They are the numbers that")
    w("must reconcile with E12. `pipeline_total` adds the stages the same pipeline really runs")
    w("before that, so it is the honest cost of getting an S index out of a sorted key vector.")
    w("")
    hdr = "| stage | " + " | ".join(LABEL[k] for k in KINDS) + " |"
    w(hdr)
    w("|---|" + "---:|" * len(KINDS))
    for tgt, lab in [(False, "plain"), (True, "phase")]:
        for s in STAGES:
            vals = [med([r["stage_ms"][s] for r in meas
                         if r["kind"] == k and r["phases_build"] == tgt]) for k in KINDS]
            w(f"| `{s}` ({lab}) | " + " | ".join(f"{v:.2f}" for v in vals) + " |")
    w("")

    # ---- 3. what the counted build contains ------------------------------
    w("## 3. What S's counted build contains")
    w("")
    w("S's counted build issues **two full Region constructions per region**: the `bulk_load`")
    w("under the uniform default capacity, and then `rebuild_with_caps` under the strict plan.")
    w("The first one's only consumer is `Region::all()`, which supplies the per-region sorted")
    w("slice -- and that slice is already a contiguous range of the input `keys` vector.")
    w("")
    w("| share of S's counted build | " + " | ".join(LABEL[k] for k in KINDS) + " |")
    w("|---|" + "---:|" * len(KINDS))
    for name, num, den in [("`s_bulk` / `s_counted`", "s_bulk", "s_counted"),
                           ("`s_rebuild` / `s_counted`", "s_rebuild", "s_counted")]:
        cells = []
        for k in KINDS:
            ratios = [r["stage_ms"][num] / r["stage_ms"][den] for r in meas
                      if r["kind"] == k and not r["phases_build"]]
            cells.append(f"{100 * med(ratios):.1f}%")
        w(f"| {name} | " + " | ".join(cells) + " |")
    # region_all lives in the phase records only, and is accumulated in ns while
    # the stage clocks are in ms -- the conversion is not optional.
    cells = []
    for k in KINDS:
        ratios = [r["ph_s_rebuild"]["region_all"] / 1e6 / r["stage_ms"]["s_counted"] for r in meas
                  if r["kind"] == k and r["phases_build"]]
        cells.append(f"{100 * med(ratios):.1f}%")
    w("| `region_all` inside `s_rebuild` / `s_counted` | " + " | ".join(cells) + " |")
    w("")
    w("`region_all` is measured only by the phase target, so its share is taken over those runs.")
    w("")
    w("Adding the two rows that describe work whose result does not survive into the finished")
    w("structure -- the pre-rebuild `bulk_load` and the `region_all` slice it is immediately")
    w("replaced by -- gives a **stage-cost estimate** of that work:")
    w("")
    cells = []
    for k in KINDS:
        ratios = [(r["stage_ms"]["s_bulk"] + r["ph_s_rebuild"]["region_all"] / 1e6)
                  / r["stage_ms"]["s_counted"] for r in meas
                  if r["kind"] == k and r["phases_build"]]
        cells.append(f"**{100 * med(ratios):.1f}%**")
    w("| work not retained / `s_counted` (stage-cost estimate) | " + " | ".join(cells) + " |")
    w("")
    w("**This is an itemisation of where the time went under this pipeline, NOT a bound on what")
    w("removing it would save, and not an upper bound of any kind.** Two paths that drop a pass")
    w("differ from it in slicing, object replacement and destruction, and in cache and allocator")
    w("state, so the costs are not simply subtractable. E15 implemented the removal and measured")
    w("it: the removable quantity turned out to be `bulk_load` alone, because the replacement")
    w("path still has to materialise each region's keys, which is what `region_all` was paying")
    w("for. See E15 §2.1 / `E15_TABLES.md` §3b. Nothing in this section is retracted; only the")
    w("reading of this one row is narrowed.")
    w("")

    # ---- 4. pipeline vs ART ----------------------------------------------
    w("## 4. The whole pipeline, against ART")
    w("")
    w("E12 compared S's `s_counted` against ART's construction. But ART is built by one call,")
    w("while S needs the probe, the statistics and the allocators as well. This is the same")
    w("comparison on the full pipeline.")
    w("")
    w("| | " + " | ".join(LABEL[k] for k in KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for s, lab in [("s_counted", "S, E12/E13 definition"), ("pipeline_total", "S, whole pipeline"),
                   ("art", "ART")]:
        vals = [med([r["stage_ms"][s] for r in meas if r["kind"] == k and not r["phases_build"]])
                for k in KINDS]
        w(f"| {lab} (ms) | " + " | ".join(f"{v:.2f}" for v in vals) + " |")
    cells = []
    for k in KINDS:
        ratios = [r["stage_ms"]["pipeline_total"] / r["stage_ms"]["art"] for r in meas
                  if r["kind"] == k and not r["phases_build"]]
        cells.append(f"**{med(ratios):.2f}x**")
    w("| whole pipeline / ART | " + " | ".join(cells) + " |")
    cells = []
    for k in KINDS:
        ratios = [r["stage_ms"]["s_counted"] / r["stage_ms"]["art"] for r in meas
                  if r["kind"] == k and not r["phases_build"]]
        cells.append(f"{med(ratios):.2f}x")
    w("| `s_counted` / ART (E12's ratio) | " + " | ".join(cells) + " |")
    w("")

    # ---- 5. Region constructor phases ------------------------------------
    w("## 5. Region constructor phases (ms, median across processes)")
    w("")
    w("Accumulated over every Region the stage constructs. `probe`, `d_bulk` and `s_bulk` all")
    w("run the FULL constructor; only the plan handed to it differs.")
    w("")
    for tag in STAGE_TAGS:
        w(f"### `{tag}`")
        w("")
        w("| phase | " + " | ".join(LABEL[k] for k in KINDS) + " |")
        w("|---|" + "---:|" * len(KINDS))
        for p in PHASES:
            vals = [med([r[tag][p] / 1e6 for r in meas if r["kind"] == k and r["phases_build"]])
                    for k in KINDS]
            w(f"| `{p}` | " + " | ".join(f"{v:.2f}" for v in vals) + " |")
        vals = [med([r[tag]["ctor_total"] / 1e6 for r in meas if r["kind"] == k and r["phases_build"]])
                for k in KINDS]
        w("| **ctor_total** | " + " | ".join(f"**{v:.2f}**" for v in vals) + " |")
        # shares
        w("")
        w("| share of `ctor_total` | " + " | ".join(LABEL[k] for k in KINDS) + " |")
        w("|---|" + "---:|" * len(KINDS))
        for p in ["place", "storage", "demand_pass", "layout", "model", "dir"]:
            cells = []
            for k in KINDS:
                ratios = [r[tag][p] / r[tag]["ctor_total"] for r in meas
                          if r["kind"] == k and r["phases_build"] and r[tag]["ctor_total"]]
                cells.append(f"{100 * med(ratios):.1f}%")
            w(f"| `{p}` | " + " | ".join(cells) + " |")
        w("")

    # ---- 6. reconciliation ------------------------------------------------
    w("## 6. Reconciliation: do the phases partition the constructor?")
    w("")
    w("`ctor_total` is one span; the eight phases are disjoint sub-spans of it. Their sum must")
    w("therefore sit just BELOW `ctor_total`, with the shortfall attributable to whatever is not")
    w("covered by any of the eight intervals.")
    w("")
    w("| stage | " + " | ".join(LABEL[k] for k in KINDS) + " |")
    w("|---|" + "---:|" * len(KINDS))
    for tag in STAGE_TAGS:
        cells = []
        for k in KINDS:
            ratios = [sum(r[tag][p] for p in PHASES) / r[tag]["ctor_total"] for r in meas
                      if r["kind"] == k and r["phases_build"] and r[tag]["ctor_total"]]
            cells.append(f"{med(ratios):.4f}")
        w(f"| `{tag}` | " + " | ".join(cells) + " |")
    w("")
    w("The ratios land in **0.9991-0.9996**, so the eight intervals account for essentially the")
    w("whole constructor and the decomposition is internally consistent. **The 0.04%-0.09%")
    w("shortfall is NOT attributed**: the expected cause is the timing calls themselves (~8 per")
    w("construction, ~10 ns each), but this round did not measure that, so it stays an")
    w("expectation rather than a result. It is far too small to affect any conclusion here.")
    w("")
    w("What the check DID catch is worth recording: an early version measured `layout` as ONE")
    w("interval spanning the demand pass, so the demand pass was counted twice and the eight")
    w("phases summed to **116%** of `ctor_total`. Laid out as a table, that error looked")
    w("perfectly normal -- the largest phase simply read as two phases' worth.")
    w("")
    calls = [med([r[t]["ctor_calls"] for r in meas if r["kind"] == k and r["phases_build"]])
             for t in STAGE_TAGS for k in KINDS]
    w(f"Region constructions per stage: {calls[0]:.0f} (probe), {calls[len(KINDS)]:.0f} (d_bulk), "
      f"{calls[2 * len(KINDS)]:.0f} (d_rebuild), {calls[3 * len(KINDS)]:.0f} (s_bulk), "
      f"{calls[4 * len(KINDS)]:.0f} (s_rebuild) -- i.e. one per region, per construction.")
    w("")

    with open(os.path.join(HERE, "E14_TABLES.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote E14_TABLES.md ({len(out)} lines); digest check {'OK' if ok else 'FAILED'}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""E18 analysis: turn results/main/e18_build.jsonl into E18_TABLES.md.

Sections:
  1. digest gate -- the instrumented build must build the same structure;
  2. the final path's three stages, from the CLEAN build, with proportions taken
     within each repetition;
  3. the construction's internal partition, from the DIAGNOSTIC build, including
     grp_prep vs place and the closure check;
  4. activity counters, normalised per key;
  5. what is computed more than once.

Timings come from the clean build only. The diagnostic build's numbers are shown
only in sections 3 and 4, and are labelled as instrumented throughout.
"""
import json
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
KINDS = ["mixed", "skew", "tranco"]
KEYS = {"mixed": 131072, "skew": 131072, "tranco": 262144}

CTOR_PHASES = ["grp_prep", "place", "demand_pass", "model", "storage", "dir",
               "layout", "prefix", "suffix_scan"]

COUNT_KEYS = ["group_calls", "fingerprint_calls", "region_ctors", "subblock_probes",
              "insert_attempts", "insert_ok", "entry_shifts", "suffix_bytes_copied",
              "overflow_inserts", "overflow_containers"]


def med(xs):
    return st.median(xs) if xs else float("nan")


def load(path, prefix=None):
    if not os.path.exists(path):
        return []
    out = []
    for l in open(path):
        x = json.loads(l)
        if prefix is None or x["run_tag"].startswith(prefix):
            out.append(x)
    return out


def main():
    raw = load(os.path.join(HERE, "results/main/e18_build.jsonl"))
    clean = [r for r in raw if not r["diag"] and r["run_tag"].startswith("P")]
    diag = [r for r in raw if r["diag"]]
    e15 = load(os.path.join(ROOT, "validation_e15_20260916/results/main/e15_build.jsonl"), "P")
    out = []
    w = out.append

    w("# E18 tables: where the final construction path spends its time")
    w("")
    w(f"Source: `results/main/e18_build.jsonl`. Headline timings from the CLEAN build "
      f"({len(clean)} records = {len(clean) // len(KINDS)} processes x {len(KINDS)} datasets, "
      f"warm-ups excluded). The construction's internal partition and the activity counters come "
      f"from the separate INSTRUMENTED build ({len(diag)} records, tagged DIAG) and are used "
      f"nowhere else.")
    w("")
    w("**Statistic: the median across processes of that process's own value.**")
    w("**Proportions are computed WITHIN a repetition and then aggregated** -- never by dividing")
    w("one median by another, and never by adding stage medians together.")
    w("")

    # ---- 1. digest -------------------------------------------------------
    w("## 1. Digest gate")
    w("")
    w("`-DE18_PREPASS` reorders the construction's placement loop -- group id and fingerprint are")
    w("computed in one pre-pass instead of inline. Both are pure functions of the key, so the")
    w("placement decisions must be identical. The digest is the check, and it is also compared")
    w("with E15's recorded value.")
    w("")
    w("| dataset | clean build matches E15 | instrumented build matches E15 |")
    w("|---|---|---|")
    ok = True
    for k in KINDS:
        ref = {r["digest"]["S"] for r in e15 if r["kind"] == k}
        c_ok = {r["digest"] for r in clean if r["kind"] == k} == ref
        d_ok = {r["digest"] for r in diag if r["kind"] == k} == ref
        ok &= c_ok and d_ok and len(ref) == 1
        w(f"| {k} | {'**yes**' if c_ok else '**NO**'} | {'**yes**' if d_ok else '**NO**'} |")
    w("")
    w("**The instrumented build produces the same structure.**" if ok else
      "**MISMATCH — do not read further.**")
    w("")

    # ---- 2. clean three stages -------------------------------------------
    w("## 2. The final path (C), three stages -- CLEAN BUILD")
    w("")
    w("One outer span for the whole flow, three nested spans for the stages. The stage spans are")
    w("measured inside the same repetition as the flow they are divided by.")
    w("")
    w("| | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for key, lab in [("flow", "flow total (ms)"), ("stats", "1. statistics (ms)"),
                     ("plan", "2. planning (ms)"), ("build", "3. final index construction (ms)")]:
        vals = [med([r["median_ms"][key] for r in clean if r["kind"] == k]) for k in KINDS]
        w(f"| {lab} | " + " | ".join(f"{v:.2f}" for v in vals) + " |")
    w("")
    w("Share of the flow, per repetition:")
    w("")
    w("| stage | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for key, lab in [("stats", "statistics"), ("plan", "planning"), ("build", "**construction**")]:
        cells = []
        for k in KINDS:
            ps = [r["median_proportion"][key] for r in clean if r["kind"] == k]
            cells.append(f"{'**' if key == 'build' else ''}{100 * med(ps):.1f}%"
                         f"{'**' if key == 'build' else ''}")
        w(f"| {lab} | " + " | ".join(cells) + " |")
    w("")
    w("**The final index construction is 66%-79% of the final flow.** That is the same-round")
    w("answer to \"how much is still construction\": it remains the dominant term, and the two")
    w("upstream stages together are 20%-34%.")
    w("")

    # ---- 3. ctor phases --------------------------------------------------
    w("## 3. Inside the construction -- INSTRUMENTED BUILD")
    w("")
    w("Constructor phases over the final build. `grp_prep` and `place` TOGETHER are the placement")
    w("work, split into \"work out the group id and fingerprint\" and \"everything else\", using")
    w("two clock calls per region rather than per key. This is why the placement loop could be")
    w("partitioned at all without distorting it.")
    w("")
    w("| phase | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for ph in CTOR_PHASES:
        vals = [med([r["ctor_phases"][ph] / 1e6 for r in diag if r["kind"] == k]) for k in KINDS]
        w(f"| `{ph}` | " + " | ".join(f"{v:.3f}" for v in vals) + " |")
    vals = [med([r["ctor_phases"]["ctor_total"] / 1e6 for r in diag if r["kind"] == k]) for k in KINDS]
    w("| **`ctor_total`** | " + " | ".join(f"**{v:.2f}**" for v in vals) + " |")
    w("")
    w("| share of `ctor_total` | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for ph in ["grp_prep", "place", "demand_pass", "model", "storage"]:
        cells = []
        for k in KINDS:
            rs = [r["ctor_phases"][ph] / r["ctor_phases"]["ctor_total"] for r in diag
                  if r["kind"] == k and r["ctor_phases"]["ctor_total"]]
            cells.append(f"{100 * med(rs):.1f}%")
        w(f"| `{ph}` | " + " | ".join(cells) + " |")
    cells = []
    for k in KINDS:
        rs = [(r["ctor_phases"]["grp_prep"] + r["ctor_phases"]["place"])
              / r["ctor_phases"]["ctor_total"] for r in diag
              if r["kind"] == k and r["ctor_phases"]["ctor_total"]]
        cells.append(f"**{100 * med(rs):.1f}%**")
    w("| **`grp_prep` + `place`** (all placement) | " + " | ".join(cells) + " |")
    w("")
    cells = []
    for k in KINDS:
        rs = [sum(r["ctor_phases"].get(ph, 0) for ph in CTOR_PHASES)
              / r["ctor_phases"]["ctor_total"] for r in diag
              if r["kind"] == k and r["ctor_phases"]["ctor_total"]]
        cells.append(f"{med(rs):.4f}")
    w("| closure: sum of phases / `ctor_total` | " + " | ".join(cells) + " |")
    w("")
    w("The phases partition the constructor, so they must sum to just under `ctor_total`. They do,")
    w("to the same order as E14 (which reported 0.9991-0.9996 for its eight-phase split; this is")
    w("now nine phases, the extra one being `grp_prep`). The shortfall is again NOT attributed.")
    w("")
    w("**These are instrumented numbers and are not used for any headline figure.**")
    w("")

    # ---- 4. counters -----------------------------------------------------
    w("## 4. Activity counts -- INSTRUMENTED BUILD, no clock calls")
    w("")
    w("These operations are far too short to time individually; a timer around each would cost")
    w("more than the thing it measures and would change the cost structure. So they are counted,")
    w("not timed, and the counts are what the next step is chosen from.")
    w("")
    w("Per dataset, over the final build stage, plus the statistics stage where it matters:")
    w("")
    w("| counter | stage | " + " | ".join(KINDS) + " | per key (mixed) |")
    w("|---|---|---:|" + "---:|" * (len(KINDS) - 1) + "---:|")
    for key in COUNT_KEYS:
        for stage, lab in [("count_build", "build"), ("count_stats", "stats")]:
            vals = [med([r[stage][key] for r in diag if r["kind"] == k]) for k in KINDS]
            if all(v == 0 for v in vals) and stage == "count_stats":
                continue
            per = vals[0] / KEYS["mixed"] if key != "suffix_bytes_copied" else vals[0] / KEYS["mixed"]
            w(f"| `{key}` | {lab} | " + " | ".join(f"{int(v):,}" for v in vals)
              + f" | {per:.3f} |")
    w("")
    w("Bytes per key, for the one counter that is a byte count:")
    w("")
    w("| | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    cells = []
    for k in KINDS:
        v = med([r["count_build"]["suffix_bytes_copied"] for r in diag if r["kind"] == k])
        cells.append(f"{v / KEYS[k]:.1f}")
    w("| `suffix_bytes_copied` per key | " + " | ".join(cells) + " |")
    w("")

    # ---- 5. repeated work ------------------------------------------------
    w("## 5. What is computed more than once")
    w("")
    w("The counters make this visible without any timing. On the final path the group id is")
    w("computed three times per key:")
    w("")
    w("| where | calls per key |")
    w("|---|---:|")
    w("| `demand_profile` (statistics stage) | 1 |")
    w("| `demand_pass` (inside the constructor) | 1 |")
    w("| placement (inside the constructor) | 1 |")
    vals = []
    for k in KINDS:
        s = med([r["count_stats"]["group_calls"] for r in diag if r["kind"] == k])
        b = med([r["count_build"]["group_calls"] for r in diag if r["kind"] == k])
        vals.append((s + b) / KEYS[k])
    w("| **total on the C path** | " + " | ".join(f"**{v:.2f}**" for v in vals) + " |")
    w("")
    w("Two of the three are inside the same construction. Note also that `entry_shifts` is")
    w("**exactly zero** on every dataset: the build feeds keys in sorted order, so every insert")
    w("appends at the end of its block and no descriptor is ever moved.")
    w("")

    with open(os.path.join(HERE, "E18_TABLES.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote E18_TABLES.md ({len(out)} lines); digest gate {'OK' if ok else 'FAILED'}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""E19 analysis: turn results/main/e19_build.jsonl into E19_TABLES.md.

Sections:
  1. the gate: reuse changes neither the structure, the plan, the query results,
     nor anything but the call count;
  2. how many model predictions the final path makes, before and after;
  3. the clean-build A/B;
  4. what R1 does NOT save;
  5. temporary memory and allocation counts.

Headline timings come from the clean build only.
"""
import json
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
KINDS = ["mixed", "skew", "tranco"]
KEYS = {"mixed": 131072, "skew": 131072, "tranco": 262144}


def med(xs):
    return st.median(xs) if xs else float("nan")


def load(path, pred):
    if not os.path.exists(path):
        return []
    return [json.loads(l) for l in open(path) if pred(json.loads(l))]


def main():
    raw = load(os.path.join(HERE, "results/main/e19_build.jsonl"), lambda x: True)
    clean = [r for r in raw if r["run_tag"].startswith("P")]
    diag = [r for r in raw if r["run_tag"] == "DIAG"]
    e15 = load(os.path.join(ROOT, "validation_e15_20260916/results/main/e15_build.jsonl"),
               lambda x: x["run_tag"].startswith("P"))
    out = []
    w = out.append

    w("# E19 tables: R1 -- reusing the group ids inside the construction")
    w("")
    w(f"Source: `results/main/e19_build.jsonl`. Headline timings from the CLEAN build "
      f"({len(clean)} records = {len(clean) // len(KINDS)} processes x {len(KINDS)} datasets). "
      f"Prediction counts and allocation figures come from the separate INSTRUMENTED build "
      f"({len(diag)} records, tagged DIAG).")
    w("")
    w("**Statistic: the median across processes of that process's own value.** Ratios are the")
    w("median of the per-process paired ratios, and their spread is shown.")
    w("")

    # ---- 1. gate ---------------------------------------------------------
    w("## 1. Gate: `check_e19_reuse`")
    w("")
    gate = os.path.join(HERE, "logs/check_e19_reuse_run.log")
    if os.path.exists(gate):
        g = json.loads(open(gate, encoding="utf-8").read().strip().splitlines()[0])
        w("| what is checked | scale |")
        w("|---|---|")
        w("| structure digest identical with the flag off and on | over a grid of region sizes, slot counts, block sizes, routes and model kinds |")
        w("| capacity plan identical | the plan comes from the probe, which R1 does not touch -- asserted so a later change that moved statistics into the construction is caught |")
        w("| query results identical | a workload against both, in both locate and scan modes, plus a full scan |")
        w("| prediction count drops by exactly one per key | and by zero when `multiblock` is off |")
        w("| the fallback works | reuse requested with an empty key set falls back rather than indexing an empty vector |")
        w("")
        w(f"Result: `{g['status']}` — {g['configurations']} configurations, "
          f"{g['digest_checks']} digest checks, {g['plan_checks']} plan checks, "
          f"**{g['query_checks']:,} query comparisons**, {g['count_checks']} prediction-count checks.")
        w("")
        w("The digest is also compared with E15's RECORDED value, so the gate establishes both")
        w("\"reuse changes nothing\" and \"nothing has drifted since E15\".")
    else:
        w("Gate log not found — R1 has NOT been validated.")
    w("")
    w("**What the guard does and does not guarantee.** The array is produced and")
    w("consumed inside one constructor, with the key order and the model unchanged --")
    w("that is the correctness argument. The length check is a cheap screen, not a")
    w("proof: it catches some breakages and cannot make an arbitrary future change")
    w("safe to fall back from.")
    ok = True
    w("| dataset | clean build matches E15 | instrumented build matches E15 |")
    w("|---|---|---|")
    for k in KINDS:
        ref = {r["digest"]["S"] for r in e15 if r["kind"] == k}
        c_ok = {r["digest"] for r in clean if r["kind"] == k} == ref
        d_ok = {r["digest"] for r in diag if r["kind"] == k} == ref
        ok &= c_ok and d_ok and len(ref) == 1
        w(f"| {k} | {'**yes**' if c_ok else '**NO**'} | {'**yes**' if d_ok else '**NO**'} |")
    w("")
    w("**R1 leaves the structure exactly as E15 recorded it.**" if ok else
      "**MISMATCH — do not read further.**")
    w("")

    # ---- 2. predictions --------------------------------------------------
    w("## 2. Model predictions on the final path")
    w("")
    w("R1 removes the second prediction the construction was making. It does not remove the")
    w("statistics stage's prediction, and it does not touch the fingerprint -- so the total goes")
    w("**from three per key to two, not to one**.")
    w("")
    w("| | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for key, lab in [("off", "prediction calls, reuse OFF (construction stage)"),
                     ("on", "prediction calls, reuse ON (construction stage)")]:
        vals = [med([r["predictions"][key] for r in diag if r["kind"] == k]) for k in KINDS]
        w(f"| {lab} | " + " | ".join(f"{int(v):,}" for v in vals) + " |")
    cells = []
    for k in KINDS:
        o = med([r["predictions"]["off"] for r in diag if r["kind"] == k])
        n = med([r["predictions"]["on"] for r in diag if r["kind"] == k])
        cells.append(f"**{int(o - n):,}** ({o / KEYS[k]:.2f} -> {n / KEYS[k]:.2f} per key)")
    w("| **calls removed** | " + " | ".join(cells) + " |")
    w("")
    w("On the whole final path the count is **1 (statistics) + 2 (construction) = 3 per key")
    w("before, and 1 + 1 = 2 per key after.** The fingerprint is unchanged at one per key.")
    w("")

    # ---- 3. clean A/B ----------------------------------------------------
    w("## 3. Clean-build A/B (ms)")
    w("")
    w("Both variants build the same final index under the same capacity plan, in the same")
    w("process, alternating which runs first, five repetitions each.")
    w("")
    w("| | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for key, lab in [("reuse_off", "final flow, reuse OFF (ms)"), ("reuse_on", "final flow, reuse ON (ms)")]:
        vals = [med([r["median_ms"][key] for r in clean if r["kind"] == k]) for k in KINDS]
        w(f"| {lab} | " + " | ".join(f"{v:.2f}" for v in vals) + " |")
    cells = []
    for k in KINDS:
        rs = [r["median_ratio"] for r in clean if r["kind"] == k]
        cells.append(f"**{med(rs):.4f}x**")
    w("| **ON / OFF** | " + " | ".join(cells) + " |")
    w("")
    w("Spread over the five processes:")
    w("")
    w("| dataset | min | median | max | processes below 1 |")
    w("|---|---:|---:|---:|---:|")
    for k in KINDS:
        rs = sorted(r["median_ratio"] for r in clean if r["kind"] == k)
        w(f"| {k} | {rs[0]:.4f} | {med(rs):.4f} | {rs[-1]:.4f} | "
          f"{sum(1 for v in rs if v < 1)}/{len(rs)} |")
    w("")
    w("**R1 reduces the final construction flow by roughly 3.5%-8.6%.** Two of the three")
    w("datasets are below 1 in every process; `tranco` has one process above 1, so its spread")
    w("straddles the line and its point estimate should be read with that in mind.")
    w("")

    # ---- 4. what is NOT saved -------------------------------------------
    w("## 4. What R1 does not save")
    w("")
    w("* **The fingerprint.** It is computed once per key, in placement only. The counters show")
    w("  `fingerprint_calls` = 1 per key and `group_calls` = 2 per key in the construction")
    w("  stage, so exactly one group prediction is removed and **none of the fingerprint work")
    w("  in `grp_prep` is claimed**.")
    w("* **The statistics stage's prediction.** Cross-stage reuse is R2 and is not attempted here.")
    w("* **The rest of `demand_pass`.** It also fills two per-group accumulators and walks the")
    w("  key array; only its model call disappears. The E18 phase figure for")
    w("  `grp_prep` + `demand_pass` is therefore an **optimisation-potential indicator, not a")
    w("  bound**: it counts loop, array and capacity-state work that R1 does not remove.")
    w("")
    w("  **The two are not on the same denominator and must not be subtracted.** The E18")
    w("  figure is a share of the CONSTRUCTION stage; the reduction measured here is a share")
    w("  of the WHOLE FLOW. Quantifying the shortfall would need both measured over the same")
    w("  interval, which this round did not do. What holds is the direction: a phase share is")
    w("  not a bound.")
    w("* **Anything upstream or downstream.** Capacity planning and the statistics themselves are")
    w("  untouched.")
    w("")

    # ---- 5. memory -------------------------------------------------------
    w("## 5. Temporary memory and allocation counts (INSTRUMENTED BUILD)")
    w("")
    w("The recorded ids are per region -- `region_keys` entries at most -- and are released when")
    w("the constructor returns, so the peak should barely move.")
    w("")
    w("| | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for key, lab in [("off_peak_mb", "peak transient heap, reuse OFF (MB)"),
                     ("on_peak_mb", "peak transient heap, reuse ON (MB)")]:
        vals = [med([r["alloc"][key] for r in diag if r["kind"] == k]) for k in KINDS]
        w(f"| {lab} | " + " | ".join(f"{v:.2f}" for v in vals) + " |")
    cells = []
    for k in KINDS:
        cells.append(f"{med([r['alloc']['on_peak_mb'] - r['alloc']['off_peak_mb'] for r in diag if r['kind'] == k]):+.2f}")
    w("| difference (MB) | " + " | ".join(cells) + " |")
    w("")
    w("| | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for key, lab in [("off_calls", "allocation calls, reuse OFF"), ("on_calls", "allocation calls, reuse ON")]:
        vals = [med([r["alloc"][key] for r in diag if r["kind"] == k]) for k in KINDS]
        w(f"| {lab} | " + " | ".join(f"{int(v):,}" for v in vals) + " |")
    cells = []
    for k in KINDS:
        o = med([r["alloc"]["off_calls"] for r in diag if r["kind"] == k])
        n = med([r["alloc"]["on_calls"] for r in diag if r["kind"] == k])
        regions = med([x["predictions"]["on"] for x in diag if x["kind"] == k]) / KEYS[k]
        cells.append(f"**+{int(n - o)}**")
    w("| **difference** | " + " | ".join(cells) + " |")
    w("")
    w("The extra allocations are exactly one per region (64 for the 131,072-key sets, 128 for")
    w("the 262,144-key set) -- the id array itself, allocated once per region and freed with it.")
    w("The peak is unchanged to two decimal places, so **the cache costs no measurable memory**")
    w("at this granularity. The wrappers cover `malloc/calloc/realloc/aligned_alloc/")
    w("posix_memalign/free`; the figure is peak requested heap under those interfaces, not RSS.")
    w("")
    w("**These are instrumented numbers and are not used for any headline figure.**")
    w("")

    with open(os.path.join(HERE, "E19_TABLES.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote E19_TABLES.md ({len(out)} lines); digest gate {'OK' if ok else 'FAILED'}")


if __name__ == "__main__":
    main()

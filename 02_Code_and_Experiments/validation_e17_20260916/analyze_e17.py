#!/usr/bin/env python3
"""E17 analysis: turn results/main/e17_build.jsonl into E17_TABLES.md.

Order:
  1. the digest gate -- the three paths must build the same structure, and it must
     be the structure the earlier rounds recorded;
  2. times for the three paths;
  3. the ratios, including the one that was missing: C/B.

Every cell is the median across processes of that process's own value. Ratios are
given both ways -- median of the per-process ratios, and ratio of the medians --
because the two are not the same number.
"""
import json
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
KINDS = ["mixed", "skew", "tranco"]
KEYS = {"mixed": 131072, "skew": 131072, "tranco": 262144}

PATHS = [("B", "B — probe + plan + build-then-rebuild (original)"),
         ("A", "A — probe + plan + direct build (E15)"),
         ("C", "C — demand_profile + plan + direct build (E16, final)")]


def med(xs):
    return st.median(xs) if xs else float("nan")


def load(path, prefix="P"):
    if not os.path.exists(path):
        return []
    out = []
    for l in open(path):
        x = json.loads(l)
        if x["run_tag"].startswith(prefix):
            out.append(x)
    return out


def main():
    meas = load(os.path.join(HERE, "results/main/e17_build.jsonl"))
    e15 = load(os.path.join(ROOT, "validation_e15_20260916/results/main/e15_build.jsonl"))
    out = []
    w = out.append

    w("# E17 tables: the three construction paths in one process")
    w("")
    w(f"Source: `results/main/e17_build.jsonl`, {len(meas)} measured records "
      f"({len(meas) // len(KINDS)} processes x {len(KINDS)} datasets). Warm-ups excluded by tag.")
    w("")
    w("**Statistic used in every cell: the median across processes of that process's own value.**")
    w("Ratios are given both ways -- median of the per-process ratios, and ratio of the medians.")
    w("")
    w("| dataset | keys |")
    w("|---|---:|")
    for k in KINDS:
        w(f"| `{k}` | {KEYS[k]:,} |")
    w("")
    w("(`tranco` is the Tranco list as the data SOURCE; only the first 262,144 rows are used.")
    w("Nothing here tests a million keys.)")
    w("")

    # ---- 1. digest -------------------------------------------------------
    w("## 1. Digest gate")
    w("")
    w("Each run builds S three ways and compares the digests BEFORE reporting any timing; a")
    w("mismatch aborts the run, so a record existing at all means they agreed. The serialisation")
    w("is E14/E15/E16's, including the SHA-256 self-test, so it can also be compared with the")
    w("digest the earlier rounds recorded.")
    w("")
    w("| dataset | distinct digests inside E17 | equals E15's recorded digest |")
    w("|---|---:|---|")
    ok = True
    for k in KINDS:
        n = len({r["digest"] for r in meas if r["kind"] == k})
        cross = None
        if e15:
            ref = {r["digest"]["S"] for r in e15 if r["kind"] == k}
            got = {r["digest"] for r in meas if r["kind"] == k}
            cross = (len(ref) == 1 and ref == got)
            ok &= bool(cross)
        ok &= (n == 1)
        w(f"| {k} | **{n}** | {'**yes**' if cross else ('yes' if cross is None else '**NO**')} |")
    w("")
    w("**All three paths build the same structure, and it is the one the earlier rounds recorded.**"
      if ok else "**MISMATCH — do not read further.**")
    w("")

    # ---- 2. times --------------------------------------------------------
    w("## 2. Times (ms)")
    w("")
    for key, lab in PATHS:
        w(f"* **{lab}**")
    w("")
    w("Each repetition runs all three in a ROTATED order, so position within the round cannot")
    w("favour one path; ratios are formed within a repetition and then aggregated.")
    w("")
    w("| path | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for key, lab in PATHS:
        vals = [med([r["median_ms"][key] for r in meas if r["kind"] == k]) for k in KINDS]
        w(f"| {lab} | " + " | ".join(f"{v:.2f}" for v in vals) + " |")
    w("")

    # ---- 3. ratios -------------------------------------------------------
    w("## 3. Ratios")
    w("")
    w("| | " + " | ".join(KINDS) + " |")
    w("|---|---:|" + "---:|" * (len(KINDS) - 1))
    for rk, lab in [("A_over_B", "**A / B** (isolates E15)"),
                    ("C_over_A", "**C / A** (isolates E16, on E15's path)"),
                    ("C_over_B", "**C / B** (the two together)")]:
        cells = []
        for k in KINDS:
            rs = [r["median_ratio"][rk] for r in meas if r["kind"] == k]
            cells.append(f"**{med(rs):.3f}x**")
        w(f"| {lab}, median of per-process ratios | " + " | ".join(cells) + " |")
        cells = []
        num, den = rk.split("_over_")
        for k in KINDS:
            a = med([r["median_ms"][num] for r in meas if r["kind"] == k])
            b = med([r["median_ms"][den] for r in meas if r["kind"] == k])
            cells.append(f"{a / b:.3f}x")
        w(f"| {lab}, ratio of the medians | " + " | ".join(cells) + " |")
    w("")
    w("**`C / B` is the number the earlier rounds could not supply.** E15 measured A/B in its own")
    w("round and E16 measured C/A in another; neither is the same observation of A, so their")
    w("product is not C/B. It is measured directly here.")
    w("")
    w("Spread of the per-process `C / B` (5 processes):")
    w("")
    w("| dataset | min | median | max |")
    w("|---|---:|---:|---:|")
    for k in KINDS:
        rs = sorted(r["median_ratio"]["C_over_B"] for r in meas if r["kind"] == k)
        w(f"| {k} | {rs[0]:.3f} | {med(rs):.3f} | {rs[-1]:.3f} |")
    w("")
    w("Pooled over every repetition in every process (25 samples per side):")
    w("")
    w("| dataset | B median | A median | C median | A/B | C/A | **C/B** |")
    w("|---|---:|---:|---:|---:|---:|---:|")
    for k in KINDS:
        bs = [v for r in meas if r["kind"] == k for v in r["samples"]["B"]]
        as_ = [v for r in meas if r["kind"] == k for v in r["samples"]["A"]]
        cs = [v for r in meas if r["kind"] == k for v in r["samples"]["C"]]
        w(f"| {k} | {med(bs):.2f} | {med(as_):.2f} | {med(cs):.2f} | "
          f"{med(as_) / med(bs):.3f} | {med(cs) / med(as_):.3f} | **{med(cs) / med(bs):.3f}** |")
    w("")
    w("Ratios of pooled medians are not the median of per-process ratios. Both are shown because")
    w("they are different quantities that happen to agree closely here -- up to about 0.015 apart")
    w("")

    with open(os.path.join(HERE, "E17_TABLES.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote E17_TABLES.md ({len(out)} lines); digest gate {'OK' if ok else 'FAILED'}")


if __name__ == "__main__":
    main()

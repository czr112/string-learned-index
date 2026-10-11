# -*- coding: utf-8 -*-
"""Hole-reuse entry analysis: is there a population a hole table could serve?

WHAT MAKES THIS ROUND'S NUMBERS DIFFERENT FROM A GLOBAL SIZE COMPARISON.

The event is a BYTE refusal on ONE sub-block. A gap in another sub-block could not have
held that suffix -- placement is per-block and invariant M1 keeps the blocks' key ranges
disjoint -- so a whole-index comparison of the two size distributions would pair holes with
requests that could never reach them and overstate the match. Everything here is computed at
the refusal, on the block that refused.

AND A BLOCK-LEVEL REFUSAL IS NOT AN OVERFLOWING INSERT. put_primary walks several blocks, so
one overflowing insert produces several refusals. The ratio between the two counts is
reported rather than assumed, because a "match rate" over refusals and a "share of insertions
saved" over overflowing inserts are different claims.

TWO TESTS PER EVENT, and the comparison between them is the candidate's whole case:
    fits_hole    some dead range in this block >= the suffix        copies NOTHING
    fits_repack  contiguous free + this block's dead bytes >= it    what repack_subblock
                                                                    does today, copying every
                                                                    live suffix in the block

THE CONFIGURATION FACT, stated wherever it matters: subblock_reclaim and reclaim_widest are
both false in the shipped configuration, so fits_repack is a COUNTERFACTUAL -- what the
existing mechanism would do if enabled -- and subblock_reclaims == 0 is a zero caused by
configuration, not by absence of opportunity.

NO TIMING. A match rate is a count; counts locate candidates, they do not price them.

Usage:  python3 analyze_holes.py
"""

import collections
import io
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

KINDS = ["mixed", "skew", "tranco"]


def load_summary(kind):
    path = os.path.join(MAIN, "holes_%s.summary.jsonl" % kind)
    rows = []

    with io.open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()

            if line:
                rows.append(json.loads(line))

    return rows


def pct(x):
    return "%.2f%%" % (100.0 * x)


def rate(a, b):
    return float(a) / float(b) if b else 0.0


def main():
    print("=" * 116)
    print("HOLE-REUSE ENTRY DIAGNOSIS")
    print("  fits_hole    some dead range in THIS block >= the suffix      copies nothing")
    print("  fits_repack  contiguous free + this block's dead bytes >= it  what repack_subblock")
    print("                                                                does today, copying all")
    print("  The shipped config has subblock_reclaim = false, so fits_repack is COUNTERFACTUAL.")
    print("=" * 116)

    all_rows = {}

    for k in KINDS:
        all_rows[k] = [r for r in load_summary(k)
                       if r["workload"] in ("replace", "insert", "permuted")
                       and r["frac"] in (0.25, 0.5, 1.0, 2.0)]

    # ------------------------------------------------------------------ per workload
    for workload, title, note in [
        ("replace", "W1  LONG REPLACEMENT -- erase cuts dead ranges, insert wants them back",
         "The candidate's own regime."),
        ("insert", "W2  PURE INSERT -- nothing is erased, so almost no dead range is ever cut",
         "The control: it separates a property of the workload from a property of the layout."),
        ("permuted", "W3  REPLACEMENT WITH A PERMUTED ERASE ORDER",
         "W1 removes the lowest key every time, which is the lowest-offset record of whatever "
         "block it lands in. erase_at never writes h.bottom, so that order is what makes the "
         "dead region one run. This arm permutes the victims and asks whether the structure "
         "survives -- the control W1 needed and did not have."),
    ]:
        print("\n%s\n    %s\n" % (title, note))
        print("    %-8s %5s %10s %10s %10s %10s %11s %9s %9s"
              % ("kind", "frac", "refusals", "slot ref", "hole fits", "repack",
                 "guarded", "mean len", "mean copy"))
        print("    " + "-" * 104)

        for k in KINDS:
            rows = [r for r in all_rows[k] if r["workload"] == workload]
            rows.sort(key=lambda r: r["frac"])

            for r in rows:
                fb = r["fail_bytes"] or 1

                print("    %-8s %5.2f %10d %10s %10s %10s %11s %9.1f %9.1f"
                      % (k, r["frac"], r["fail_bytes"], "%d" % r["fail_slots"],
                         pct(r["hole_match_rate"]), pct(r["repack_match_rate"]),
                         pct(float(r["repack_guarded"]) / fb),
                         r["mean_len"], r["mean_live_to_copy"]))

    # ------------------------------------------------------------------ the two counts
    print("\n" + "=" * 116)
    print("A BLOCK-LEVEL REFUSAL IS NOT AN OVERFLOWING INSERT")
    print("=" * 116 + "\n")
    print("    Refusals are counted per (insert, block) pair; put_primary walks several blocks,")
    print("    so one overflowing insert produces several. Reading the match rate as a share of")
    print("    insertions would be wrong, so both denominators are printed.\n")
    print("    %-8s %-8s %12s %12s %10s %9s   %s"
          % ("kind", "workload", "refusals", "overflow", "ref/ovf", "slot ref", "reading"))
    print("    " + "-" * 104)

    for k in KINDS:
        for workload in ("replace", "insert"):
            rows = [r for r in all_rows[k] if r["workload"] == workload]
            rows.sort(key=lambda r: r["frac"])

            if not rows:
                continue

            r = rows[-1]
            ovf = r["overflow_inserts"]
            per = float(r["fail_bytes"]) / float(ovf) if ovf else 0.0

            # The ratio can be BELOW one, and the first version of this line glossed that as
            # "one-to-one". It is the opposite: fewer than one byte refusal per overflowing
            # insert means many overflowing inserts scored no byte refusal at all and failed
            # on SLOTS -- which is the case hole reuse cannot touch.
            if per >= 1.0:
                gloss = "%.1f byte refusals per overflowing insert" % per
            else:
                gloss = ("MANY overflowing inserts scored NO byte refusal at all -- they "
                         "failed on slots")

            print("    %-8s %-8s %12d %12d %10.2f %9d   %s"
                  % (k, workload, r["fail_bytes"], ovf, per, r["fail_slots"], gloss))

    # ------------------------------------------------------------------ what runs today
    print("\n" + "=" * 116)
    print("WHAT THE SHIPPED PATH ACTUALLY RUNS (so the counterfactual above is not mistaken for a measurement)")
    print("=" * 116 + "\n")
    print("    %-8s %-8s %13s %13s %9s %13s %14s"
          % ("kind", "workload", "compact enter", "plan rejected", "helped",
             "help rate", "in-block reclaim"))
    print("    " + "-" * 104)

    for k in KINDS:
        for workload in ("replace", "insert"):
            rows = [r for r in all_rows[k] if r["workload"] == workload]
            rows.sort(key=lambda r: r["frac"])

            if not rows:
                continue

            r = rows[-1]

            print("    %-8s %-8s %13d %13d %9d %13s %14s"
                  % (k, workload, r["compact_entered"], r["compact_plan_rejected"],
                     r["compact_helped"],
                     pct(rate(r["compact_helped"], r["compact_entered"])),
                     "%d  (disabled)" % r["subblock_reclaims"]))

    # ------------------------------------------------------------------ structure
    print("\n" + "=" * 116)
    print("HOW MANY DEAD RANGES DOES A FAILING BLOCK HAVE -- and is 'one' a layout property?")
    print("=" * 116 + "\n")
    print("    Counted over EVERY refusal, not over the retained rows: the per-event log is")
    print("    capped, so a histogram built from it is a censored subsample of the earliest")
    print("    refusals. These are counters and cover the population exactly.\n")
    print("    %-8s %-9s %10s %12s %12s %12s %10s %10s"
          % ("kind", "workload", "refusals", "nholes==0", "nholes==1", "nholes>=2",
             "count<2", "slots_free"))
    print("    " + "-" * 100)

    for k in KINDS:
        for workload in ("replace", "permuted", "insert"):
            rows = [r for r in all_rows[k] if r["workload"] == workload]
            rows.sort(key=lambda r: r["frac"])

            if not rows:
                continue

            r = rows[-1]
            fb = r["fail_bytes"] or 1

            print("    %-8s %-9s %10d %11s %12s %12s %10s %10s"
                  % (k, workload, r["fail_bytes"],
                     pct(float(r["nholes_0"]) / fb),
                     pct(float(r["nholes_1"]) / fb),
                     pct(float(r["nholes_2plus"]) / fb),
                     pct(float(r["count_lt2"]) / fb),
                     "%d" % r["slots_not_free"]))

    print("")
    print("    read: for W1 the dead region is one run almost always; for W3 it is NOT. The")
    print("    difference is the ERASE ORDER, not the layout: erase_at drops a descriptor and")
    print("    never writes h.bottom, and h.bottom is a low-water mark, so a gap left below it")
    print("    is indistinguishable from one above. W1 erases the lowest key every time, which")
    print("    is the lowest-offset record of its block; permuting the victims splits the run.")
    print("    So a hole table needs a real free list, and W1's 'one pointer per block' was an")
    print("    artifact of the driver -- which this round published as a property of the layout.")
    print("    count<2 is the share where repack_subblock no-ops because it returns for n < 2.")

    # ------------------------------------------------------------------ decision
    print("\n" + "=" * 116)
    print("DECISION")
    print("=" * 116 + "\n")

    print("    The entry condition has TWO halves and BOTH must hold:")
    print("      (1) 局部整理有效            -- local reclaim works")
    print("      (2) 重复复制活跃后缀占主导   -- the repeated copying of live suffixes dominates\n")
    print("    %-8s %-9s %11s %11s %11s %13s   %s"
          % ("kind", "workload", "hole", "repack", "guarded", "mean copy", "reading"))
    print("    " + "-" * 108)

    for k in KINDS:
        for workload in ("replace", "permuted", "insert"):
            rows = [r for r in all_rows[k] if r["workload"] == workload]
            rows.sort(key=lambda r: r["frac"])

            if not rows:
                continue

            r = rows[-1]
            fb = r["fail_bytes"] or 1
            g = float(r["repack_guarded"]) / fb

            if r["hole_match_rate"] < 0.05:
                reading = "(1) unreachable"
            elif g < 0.5 * r["hole_match_rate"]:
                reading = "(1) only if the n<2 guard goes"
            else:
                reading = "(2) -- the copy size decides"

            print("    %-8s %-9s %11s %11s %11s %13s   %s"
                  % (k, workload, pct(r["hole_match_rate"]), pct(r["repack_match_rate"]),
                     pct(g), "%.1f bytes" % r["mean_live_to_copy"], reading))

    print("\n    READING. Three corrections the adversarial review forced onto an earlier draft:\n")
    print("    1. THE CANDIDATE IS NOT COVERED BY repack ON THE LATE CHECKPOINTS. `guarded` applies")
    print("       repack_subblock's own n<2 early return; the earlier draft's test did not, so it")
    print("       credited repack on blocks holding 0 or 1 live records, where repack does nothing.")
    print("       With the guard the 200% rate collapses while hole stays high.")
    print("    2. THE COPY IS TINY. mean_live_to_copy is 39-119 bytes at 200%, and on most of those")
    print("       events there is at most one live record to move -- there is no repeated copying")
    print("       for the candidate to eliminate. The entry condition's SECOND HALF fails, and it")
    print("       fails independently of point 1.")
    print("    3. THE STRUCTURE IS THE DRIVER'S. W3 permutes the erase order and the dead region")
    print("       splits, so 'one run per block' was an artifact of W1 always erasing the lowest")
    print("       key. A hole table needs a real free list. The first draft published the opposite")
    print("       as a property of the layout, and the metadata-cost registration rode on it.\n")
    print("    DISPOSITION: do not enter hole reuse as specified. What the round DOES establish is")
    print("    narrower and cheaper to test: repack_subblock's 'if (n < 2) return;' is a one-line")
    print("    condition, not a mechanism, and on the late checkpoints it is what turns the")
    print("    refusals away. Checking that costs one guard change. The n == 0 case needs care --")
    print("    it would set h.bottom to bb_ without moving a record. THAT is the next test, not a")
    print("    hole table.\n")
    print("    The whole-group compact help rate below is reported with its denominator caveat")
    print("    attached; the gap to a per-block repack is NOT explained by compact_plan_rejected.")
    print("    (a) compact_entered is incremented BEFORE worth_compacting() is asked, so it counts")
    print("        every entry to the branch, including the ones the gate then refused. It is NOT")
    print("        'compactions that ran'. The denominators are entered / gated-in / helped, and")
    print("        this round separated only the first and the third.")
    print("    (b) On the PURE-INSERT arm the help rate is exactly 0 and that one IS explained:")
    print("        worth_compacting requires the group's garbage to reach the record's length, and")
    print("        nothing was erased, so the gate never opens.")
    print()
    print("    What would settle the residual gap: a completed-compaction count and the retry's")
    print("    failure reason (slot vs bytes) after the compaction commits. Not measured here.\n")
    print("    See RESULTS_HOLES_ENTRY.md for the full reading and the registration.\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

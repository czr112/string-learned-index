# -*- coding: utf-8 -*-
"""A3 entry diagnosis analysis: is the container still >20% of maintenance, mostly moves?

WHAT IS AND IS NOT COMPUTED HERE.
  * The share is `(ov_insert + ov_erase) / maintenance`. `maintenance` is the timed erase
    span plus the timed insert span, which is the same denominator E32 used, so the two
    rounds are comparable IN CALIBER even though they are not comparable in value.
  * The breakdown is an additive partition of the container's own time. The fourth
    term is labelled `insert*` and NOT `shift`: with the shift distance this
    workload actually produces (~0.01 records per insert) it is a RESIDUAL --
    insert_at plus the counters plus whatever the clocks failed to account for.
        lookup  = ov_lookup_ms                 (minus one clock pair)
        alloc   = ov_alloc_ms                  (minus one clock pair)
        insert* = ov_insert_ms - lookup - alloc - one clock pair   (a RESIDUAL)
        erase   = ov_erase_ms                  (minus its own clock pair)
    It is stated as a partition because a report that lists four numbers without saying
    whether they sum is how a reader ends up double-counting one of them.
  * PHASES ARE KEPT APART. `short_term` is everything up to and including one full pass over
    N; `past_one_full` is what comes after, where the victims are records this run itself
    inserted rather than original ones.
  * E34's O1 is cited ONLY as context. It is a non-default arm and its 12x movement
    reduction is not credited to the shipped path anywhere in this output.

NO SPEEDUP IS ESTIMATED. The share is a ceiling on what ANY container change could save --
it is not a prediction of what a particular one would.

Usage:  python3 analyze_ov_a3.py [--tag A3]
"""

import argparse
import collections
import io
import os
import statistics

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

KINDS = ["mixed", "skew", "tranco"]
WORKLOADS = ["replace", "insert"]
PHASES = ["short_term", "past_one_full"]
CP = [0.25, 0.50, 1.00, 2.00]


def load(kind):
    path = os.path.join(MAIN, "ov_cost_a3_%s.jsonl" % kind)
    rows = []

    with io.open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                import json
                rows.append(json.loads(line))

    return rows


def med(rows, key):
    vals = [r[key] for r in rows if key in r]
    return statistics.median(vals) if vals else 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", default="A3")
    opts = parser.parse_args()

    out = []
    w = out.append

    w("A3 entry diagnosis   run_tag=%s" % opts.tag)
    w("container share of maintenance, on the SHIPPED configuration; E34's O1 arm is NOT credited")
    w("=" * 104)

    verdict_rows = []

    for workload in WORKLOADS:
        w("")
        w("#" * 104)
        w("## %s" % ("long replacement to 200%" if workload == "replace" else "pure insert"))
        w("#" * 104)

        for kind in KINDS:
            rows = [r for r in load(kind) if r.get("workload") == workload]
            if not rows:
                continue

            w("")
            w("  %s" % kind)
            w("  all columns are %% of maintenance, and the instrument's own clock overhead")
            w("  has been SUBTRACTED from each of them; the last column is what was subtracted.")
            w("  %-11s %5s %9s %8s %8s %8s %7s %7s %7s %7s"
              % ("phase", "frac", "maint_ms", "ov_net", "share", "lookup", "alloc",
                 "insert*", "erase", "instr"))

            for frac in CP:
                cell = [r for r in rows if abs(r["frac"] - frac) < 1e-9]
                if not cell:
                    continue

                maint = med(cell, "maintenance_ms")
                ovi = med(cell, "ov_insert_ms")
                ove = med(cell, "ov_erase_ms")
                lk = med(cell, "ov_lookup_ms")
                al = med(cell, "ov_alloc_ms")

                # SUBTRACT THE INSTRUMENT. Each clock pair costs two steady_clock reads and
                # this round put three pairs inside put_overflow where E32 had one, so the
                # residual carries one pair's worth of overhead that is NOT insert work.
                # Subtracting it is what makes the residual attributable; leaving it in
                # would credit the instrument to the container.
                oh_ns = med(cell, "timer_overhead_ns")
                icalls = med(cell, "ov_insert_calls")
                ecalls = med(cell, "ov_erase_calls")
                oh_i_ms = oh_ns * icalls / 1e6
                oh_e_ms = oh_ns * ecalls / 1e6

                lk_c = lk - oh_i_ms
                al_c = al - oh_i_ms
                sh_c = ovi - lk - al - oh_i_ms
                ove_c = ove - oh_e_ms

                ov = ovi + ove
                ov_net = lk_c + al_c + sh_c + ove_c
                instruments = (ovi + ove) - ov_net

                phase = cell[0]["phase"]
                share = (ov / maint) if maint else 0.0
                share_net = (ov_net / maint) if maint else 0.0

                w("  %-11s %5d %9.1f %8.1f %7.1f%% %8.1f%% %7.1f%% %7.1f%% %7.1f%% %7.1f%%"
                  % (phase, int(frac * 100), maint, ov_net, 100 * share_net,
                     100 * lk_c / maint if maint else 0,
                     100 * al_c / maint if maint else 0,
                     100 * sh_c / maint if maint else 0,
                     100 * ove_c / maint if maint else 0,
                     100 * instruments / maint if maint else 0))

                verdict_rows.append((workload, kind, phase, int(frac * 100), share_net,
                                     ov_net, maint))

            # Movement, in the units the code moves.
            w("")
            w("  %-11s %5s %14s %16s %14s %16s"
              % ("phase", "frac", "ins_records", "ins_bytes", "ers_records", "ers_bytes"))
            for frac in CP:
                cell = [r for r in rows if abs(r["frac"] - frac) < 1e-9]
                if not cell:
                    continue
                w("  %-11s %5d %14.0f %16.0f %14.0f %16.0f"
                  % (cell[0]["phase"], int(frac * 100),
                     med(cell, "ov_shifted_records_insert"), med(cell, "ov_shifted_bytes_insert"),
                     med(cell, "ov_shifted_records_erase"), med(cell, "ov_shifted_bytes_erase")))

    w("")
    w("#" * 104)
    w("## VERDICT against the plan's rule: container > 20% of maintenance")
    w("#" * 104)
    w("")
    w("  %-9s %-8s %-15s %6s %10s %s"
      % ("workload", "kind", "phase", "frac", "share", "verdict"))

    over = 0

    for workload, kind, phase, frac, share, ov, maint in verdict_rows:
        if frac != 200:
            continue
        v = "ABOVE 20%" if share > 0.20 else "below 20%"
        if share > 0.20:
            over += 1
        w("  %-9s %-8s %-15s %5d%% %9.1f%%  %s" % (workload, kind, phase, frac, 100 * share, v))

    w("")
    w("  cells above 20%%: %d of %d (at the 200%% checkpoint)"
      % (over, sum(1 for r in verdict_rows if r[3] == 200)))
    w("")
    w("  NOTE: this share is a CEILING on what any container change could save, not a")
    w("  prediction. It is not a speedup and no speedup is estimated from it.")

    text = "\n".join(out) + "\n"

    with io.open(os.path.join(MAIN, "ov_cost_a3_analysis_%s.txt" % opts.tag), "w",
                 encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

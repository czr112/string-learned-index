#!/usr/bin/env python3
"""G3/A1' sweep: S0 / E31 / WIDE / ART, four arms.

clean  bench_widest   5 processes x 3 corpora x 2 modes x 4 reps  -- the TIMINGS

"SWEEP" HERE MEANS ITERATING THE EXPERIMENT CONFIGURATION, not a range scan over the index.
The workload is long-replacement to 200% of N with the live count held constant -- the same
shape E26/E28/E31/E37 used, so this round's numbers can be read against theirs.

REPETITIONS COME FROM ONE PLACE. `--reps` is UNSET by default and the phase table supplies
the value; passing it explicitly overrides it and the effective number is printed either way.

FOUR REPETITIONS, BECAUSE THERE ARE FOUR ARMS. The rotation puts every arm in every position
exactly once, which removes the systematic advantage of going first. It does NOT remove every
carry-over between arms, and it does not make the repetitions independent samples -- the
driver prints the order it produced rather than implying more than was done. An explicit
repetition count that is not a multiple of four is REFUSED before anything runs.

EVERY RATIO IS TAKEN WITHIN ONE REPETITION. A paired ratio across processes, or across rounds,
is not a measurement of this round; ART in particular is rebuilt here rather than carried over.

THE ARMS ARE NOT A CHAIN. S0 -> E31 is `subblock_reclaim`; E31 -> WIDE is `reclaim_widest`.
The report gives both ratios side by side and never multiplies them: E31/S0 x WIDE/E31 is not
WIDE/S0 as a measurement, only as arithmetic on medians of ratios.

RUN IDENTITY. Every invocation writes its records under a `run_id`, and each phase's output is
ARCHIVED as it is produced, so an interruption preserves what was collected. Nothing from a
previous run is deleted.

Usage:  python3 run_widest.py [--processes 5] [--reps N] [--modes same,grow] [--run-id TAG]
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

DATASETS = [
    ("mixed", "131072", ""),
    ("skew", "131072", ""),
    ("tranco", "262144", "../validation_e5_20260913/data/tranco_XN6XN.csv"),
]

MODES = ["same", "grow"]

# phase -> (target, output prefix, processes, repetitions)
PHASES = {
    "clean": ("bench_widest", "long_widest", 5, 4),
    # G5: the 2x2 cross-backend row. Five arms {S0, WIDE, S0L, WIDEL, ART}, so the
    # repetition count must be a multiple of five for the rotation to be balanced.
    "x2":    ("bench_lits2x2", "long_lits2x2", 5, 5),
    # G5: the external-baseline row. Four arms {S0, WIDE, ART, BTREE}.
    "bt":    ("bench_btree", "long_btree", 5, 4),
    # I10 closing round: old vs safe compaction path, paired. Three arms {OLD, NEW, ART},
    # so the repetition count must be a multiple of three.
    "i10":   ("bench_i10", "long_i10", 5, 6),
}

# Phase -> arm count. The balance check below is only meaningful if it uses the arm
# count of the phase it is checking.
ARM_COUNT = {"clean": 4, "x2": 5, "bt": 4, "i10": 3}


def archive(work, dest):
    """Move whatever the child appended into the run's own file. Returns the line count.

    Append-only, so nothing written earlier is ever rewritten. The CALLER must invoke this
    after every child rather than once per phase: the guarantee is only as good as its call
    site.
    """
    if not os.path.exists(work):
        return 0

    with open(work, "rb") as f:
        data = f.read()

    with open(work, "wb"):
        pass

    if not data:
        return 0

    with open(dest, "ab") as f:
        f.write(data)

    return data.count(b"\n")


def invoke(target, kind, n, reps, csv, tag, mode, log):
    args = [sys.executable, "tools_build.py", "run", target, kind, n, str(reps), csv, tag, mode]

    started = time.time()
    result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    elapsed = time.time() - started

    line = "%s %s %s reps=%d tag=%s rc=%d wall=%.0fs" % (target, kind, mode, reps, tag,
                                                        result.returncode, elapsed)
    print("  " + line, flush=True)
    log.write("  " + line + "\n")
    log.flush()

    return result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--processes", type=int, default=5)
    parser.add_argument("--reps", type=int, default=None)
    parser.add_argument("--modes", default="same,grow")
    parser.add_argument("--phases", default="clean")
    parser.add_argument("--run-id", default=time.strftime("%Y%m%d_%H%M%S"))
    opts = parser.parse_args()

    modes = [m.strip() for m in opts.modes.split(",") if m.strip()]
    phases = [p.strip() for p in opts.phases.split(",") if p.strip()]

    effective = {}

    for phase in phases:
        reps = opts.reps if opts.reps is not None else PHASES[phase][3]

        if reps % ARM_COUNT[phase] != 0:
            raise SystemExit(
                "REFUSED: the clean phase needs a repetition count that is a multiple of the "
                "arm count (%d), so every arm occupies every position exactly once. Got %d "
                "(%s)." % (ARM_COUNT[phase], reps,
                           "from --reps" if opts.reps is not None else "from the phase table"))

        effective[phase] = reps

    log_path = os.path.join(HERE, "logs", "run_widest_%s.log" % opts.run_id)

    with open(log_path, "w", encoding="utf-8") as log:
        log.write("widest sweep run_id=%s  %s\n" % (opts.run_id, time.strftime("%Y-%m-%d %H:%M:%S")))
        print("run_id %s" % opts.run_id, flush=True)

        for phase in phases:
            target, prefix, _, _ = PHASES[phase]
            procs = opts.processes
            reps = effective[phase]

            source = "--reps" if opts.reps is not None else "phase table"
            line = ("phase %s: target=%s reps=%d (%s) processes=%d"
                    % (phase, target, reps, source, procs))
            print(line, flush=True)
            log.write(line + "\n")

            arms = ARM_COUNT[phase]

            # The order that will actually run, computed the way the benchmark computes it.
            for r in range(reps):
                order = [(r + k) % arms for k in range(arms)]
                log.write("    rep %d arm order: %s\n" % (r, " ".join(str(a) for a in order)))

            # Position balance, asserted rather than assumed.
            placed = [[0] * arms for _ in range(arms)]

            for r in range(reps):
                for pos, arm in enumerate((r + k) % arms for k in range(arms)):
                    placed[arm][pos] += 1

            for arm in range(arms):
                if len(set(placed[arm])) != 1:
                    raise SystemExit("REFUSED: arm %d is not position-balanced: %s"
                                     % (arm, placed[arm]))

            log.write("    position balance verified: every arm occupies every position %d time(s)\n"
                      % (reps // arms))

        log.flush()

        for phase in phases:
            target, prefix, _, _ = PHASES[phase]
            procs = opts.processes
            reps = effective[phase]

            for mode in modes:
                work = os.path.join(HERE, "results", "main", "%s_%s.jsonl" % (prefix, mode))
                dest = os.path.join(HERE, "results", "main",
                                    "%s_%s_%s.jsonl" % (prefix, opts.run_id, mode))

                # Only THIS run's working file is cleared.
                if os.path.exists(work):
                    os.remove(work)

                header = "--- phase %s mode %s -> %s ---" % (phase, mode, os.path.basename(dest))
                print(header, flush=True)
                log.write(header + "\n")

                total = 0

                for p in range(1, procs + 1):
                    tag = "P%d" % p

                    for kind, n, csv in DATASETS:
                        invoke(target, kind, n, reps, csv, tag, mode, log)
                        total += archive(work, dest)

                line = "    archived %d row(s) to %s" % (total, os.path.basename(dest))
                print(line, flush=True)
                log.write(line + "\n")
                log.flush()

        log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

    print("WIDEST SWEEP COMPLETE run_id=%s" % opts.run_id)


if __name__ == "__main__":
    main()

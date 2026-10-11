#!/usr/bin/env python3
"""E38/P3 sweep: M0 / LITS / ART, three arms.

clean  bench_lits_adapter   5 processes x 3 datasets x 2 modes x 3 reps -- the TIMINGS
mech   diag_lits_adapter    1 process  x 3 datasets x 2 modes           -- the COUNTERS

"SWEEP" HERE MEANS ITERATING THE EXPERIMENT CONFIGURATION, not a range scan over the index.
This round measures the full build, the static mixed point query and resident space. The
`same` / `grow` labels are the long-replacement workload's, carried over so the two modes stay
the ones earlier rounds reported; neither is a long-range replacement run in this round.

REPETITIONS COME FROM ONE PLACE. `--reps` is UNSET by default and the phase table supplies the
value; passing `--reps` explicitly overrides it and the effective number is printed either way.
The earlier version had the phase table claim 3 while the option default of 4 was what actually
ran, so the file and the run disagreed.

THREE REPETITIONS, BECAUSE THERE ARE THREE ARMS. The rotation puts every arm in every position
exactly once, which removes the systematic advantage of going first. It does NOT remove every
carry-over between arms, and it does not make the repetitions independent samples -- the driver
prints the order it produced rather than implying more than was done. An explicit repetition
count that is not a multiple of three is REFUSED before anything runs.

EVERY RATIO IS TAKEN WITHIN ONE REPETITION. A paired ratio across processes, or across rounds,
is not a measurement of this round; LITS/ART in particular is rebuilt here rather than carried
over from E21 or E25.

RUN IDENTITY. Every invocation writes its records under a `run_id`, and each phase's output is
ARCHIVED as it is produced, so an interruption preserves what was collected instead of leaving
it in a working file that the next run would truncate. Nothing from a previous run is deleted.

Usage:  python3 run_lits.py [--processes 5] [--reps N] [--modes same,grow] [--phases clean,mech]
                           [--run-id TAG]
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

# phase -> (target, output prefix, processes, repetitions, tag prefix)
PHASES = {
    "clean": ("bench_lits_adapter", "lits_adapter", 5, 3, "P"),
    "mech": ("diag_lits_adapter", "lits_adapter_diag", 1, 1, "MECH"),
}

ARMS = 3


def archive(work, dest):
    """Move whatever the child appended into the run's own file. Returns the line count.

    Append-only, so nothing written earlier is ever rewritten. The CALLER must invoke this
    after every child rather than once per phase: the guarantee is only as good as its call
    site, and the first version of this file described the right thing and did the other.
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
    args = [sys.executable, "tools_build.py", "run_current", target, kind, n, str(reps), csv, tag, mode]

    started = time.time()
    result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    elapsed = time.time() - started

    line = "%s %s %s reps=%d tag=%s rc=%d wall=%.0fs" % (target, kind, mode, reps, tag, result.returncode, elapsed)
    print("  " + line, flush=True)
    log.write("  " + line + "\n")
    log.flush()

    return result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--processes", type=int, default=5)
    # UNSET means "use the phase table". A default of 4 here is what made the table's 3 a lie.
    parser.add_argument("--reps", type=int, default=None)
    parser.add_argument("--modes", default="same,grow")
    parser.add_argument("--phases", default="clean,mech")
    parser.add_argument("--run-id", default=time.strftime("%Y%m%d_%H%M%S"))
    opts = parser.parse_args()

    modes = [m.strip() for m in opts.modes.split(",") if m.strip()]
    phases = [p.strip() for p in opts.phases.split(",") if p.strip()]

    # ---- the effective repetition count, resolved and CHECKED before anything runs ---------
    effective = {}
    for phase in phases:
        reps = opts.reps if opts.reps is not None else PHASES[phase][3]

        if phase == "clean" and reps % ARMS != 0:
            raise SystemExit(
                "REFUSED: the clean phase needs a repetition count that is a multiple of the "
                "arm count (%d), so every arm occupies every position exactly once. Got %d "
                "(%s)." % (ARMS, reps, "from --reps" if opts.reps is not None else "from the phase table"))

        effective[phase] = reps

    log_path = os.path.join(HERE, "logs", "run_lits_%s.log" % opts.run_id)

    with open(log_path, "w", encoding="utf-8") as log:
        log.write("lits sweep run_id=%s  %s\n" % (opts.run_id, time.strftime("%Y-%m-%d %H:%M:%S")))
        print("run_id %s" % opts.run_id, flush=True)

        for phase in phases:
            target, prefix, procs_cfg, _, tag_prefix = PHASES[phase]
            procs = opts.processes if phase == "clean" else 1
            reps = effective[phase]

            source = "--reps" if opts.reps is not None else "phase table"
            line = ("phase %s: target=%s reps=%d (%s) processes=%d"
                    % (phase, target, reps, source, procs))
            print(line, flush=True)
            log.write(line + "\n")

            # THE ORDER THAT WILL ACTUALLY RUN, computed the way the benchmark computes it.
            # Printed rather than described, so the record says what happened.
            for r in range(reps):
                order = [(r + k) % ARMS for k in range(ARMS)]
                log.write("    rep %d arm order: %s\n" % (r, " ".join(str(a) for a in order)))

            # Position balance, asserted rather than assumed: with reps a multiple of ARMS this
            # holds by construction, and it is checked because "by construction" is exactly how
            # the earlier repetition mismatch survived.
            #
            # THE CHECK APPLIES TO THE TIMING PHASE ONLY, and the first version applied it to
            # every phase -- which made the diagnostic phase, one repetition over three arms,
            # refuse to start with `arm 0 is not position-balanced: [1, 0, 0]`. One repetition
            # cannot balance three arms, and it does not need to: that phase produces COUNTERS,
            # not comparisons, so no arm is being compared against another by position. A guard
            # that fires on a legitimate configuration is the same class of mistake as a
            # non-vacuity assertion that depends on the corpus.
            placed = [[0] * ARMS for _ in range(ARMS)]
            for r in range(reps):
                for pos, arm in enumerate((r + k) % ARMS for k in range(ARMS)):
                    placed[arm][pos] += 1

            if phase == "clean":
                for arm in range(ARMS):
                    if len(set(placed[arm])) != 1:
                        raise SystemExit("REFUSED: arm %d is not position-balanced: %s"
                                         % (arm, placed[arm]))

                log.write("    position balance verified: every arm occupies every position %d time(s)\n"
                          % (reps // ARMS))
            else:
                log.write("    position balance NOT applicable to phase %s (%d repetition(s), "
                          "counters only, no arm-to-arm comparison)\n" % (phase, reps))

        log.flush()

        for phase in phases:
            target, prefix, _, _, tag_prefix = PHASES[phase]
            procs = opts.processes if phase == "clean" else 1
            reps = effective[phase]

            for mode in modes:
                work = os.path.join(HERE, "results", "main", "%s_%s.jsonl" % (prefix, mode))
                dest = os.path.join(HERE, "results", "main", "%s_%s_%s.jsonl" % (prefix, opts.run_id, mode))

                # Only THIS run's working file is cleared. The archived files of earlier runs
                # are never touched -- an interrupted run's rows are not a later run's collateral.
                if os.path.exists(work):
                    os.remove(work)

                header = "--- phase %s mode %s -> %s ---" % (phase, mode, os.path.basename(dest))
                print(header, flush=True)
                log.write(header + "\n")

                total = 0

                for p in range(1, procs + 1):
                    tag = tag_prefix if procs == 1 else "%s%d" % (tag_prefix, p)

                    for kind, n, csv in DATASETS:
                        invoke(target, kind, n, reps, csv, tag, mode, log)

                        # ARCHIVED AFTER EVERY INVOCATION, which is what makes an interruption
                        # survivable. Doing it once at the end of the mode leaves the rows in a
                        # working file that only the run_id in the log identifies -- and that is
                        # the state this file's docstring described while its code did not.
                        total += archive(work, dest)

                line = "    archived %d row(s) to %s" % (total, os.path.basename(dest))
                print(line, flush=True)
                log.write(line + "\n")
                log.flush()

        log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

    print("LITS SWEEP COMPLETE run_id=%s" % opts.run_id)


if __name__ == "__main__":
    main()

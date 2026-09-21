#!/usr/bin/env python3
"""E35 prefix diagnostic: the two sweeps.

clean  bench_prefix   5 processes x 3 datasets x 2 modes -- the per-queue TIMINGS, which
                       is the only build whose absolute times mean anything
mech   diag_prefix    1 process  x 3 datasets x 2 modes -- the per-queue COUNTERS. These
                      are deterministic functions of the workload, not measurements with
                      noise, so one process is enough; and the counting cost sits inside
                      the comparison loop, so this build supplies no timing at all.

Both go through run_current, which refuses to run a binary whose sources have moved on.

Usage:  python3 run_prefix.py [--processes 5] [--reps 3] [--modes same,grow] [--phases clean,mech]
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

# phase -> (target, output prefix, processes, reps, tag prefix)
PHASES = {
    "clean": ("bench_prefix", "prefix", 5, 3, "P"),
    "mech": ("diag_prefix", "prefix_diag", 1, 1, "MECH"),
}


def invoke(target, kind, n, reps, csv, tag, mode, log):
    args = [sys.executable, "tools_build.py", "run_current", target, kind, n, str(reps), csv, tag, mode]

    started = time.time()
    result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    elapsed = time.time() - started

    line = "%s %s %s reps=%d tag=%s rc=%d wall=%.0fs" % (target, kind, mode, reps, tag, result.returncode, elapsed)

    print(line, flush=True)
    log.write(line + "\n")

    if result.returncode != 0:
        log.write("  stderr: " + result.stderr.strip()[-2500:] + "\n")
        log.flush()
        raise SystemExit("FAILED: " + line)

    log.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--processes", type=int, default=5)
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--modes", default="same,grow")
    parser.add_argument("--phases", default="clean,mech")
    opts = parser.parse_args()

    modes = [m.strip() for m in opts.modes.split(",") if m.strip()]
    phases = [p.strip() for p in opts.phases.split(",") if p.strip()]

    # The benchmark appends, because several processes share the file. Removing the files
    # first is what keeps a re-run from mixing two sweeps.
    for phase in phases:
        for mode in modes:
            out = os.path.join(HERE, "results", "main", "%s_%s.jsonl" % (PHASES[phase][1], mode))
            if os.path.exists(out):
                os.remove(out)

    with open(os.path.join(HERE, "logs", "run_prefix.log"), "w", encoding="utf-8") as log:
        log.write("prefix sweep %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

        for phase in phases:
            target, _, default_procs, default_reps, tag_prefix = PHASES[phase]
            procs = opts.processes if phase == "clean" else 1
            reps = opts.reps if phase == "clean" else 1

            for mode in modes:
                log.write("--- phase %s mode %s ---\n" % (phase, mode))
                print("--- %s / %s ---" % (phase, mode), flush=True)

                for p in range(1, procs + 1):
                    tag = tag_prefix if procs == 1 else "%s%d" % (tag_prefix, p)

                    for kind, n, csv in DATASETS:
                        invoke(target, kind, n, reps, csv, tag, mode, log)

        log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

    print("PREFIX SWEEP COMPLETE")


if __name__ == "__main__":
    main()

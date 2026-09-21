#!/usr/bin/env python3
"""E36 sweep: B / O / T, four arms, five independent processes.

clean  bench_tag   5 processes x 3 datasets x 2 modes x 4 reps -- the TIMINGS
mech   diag_tag    1 process  x 3 datasets x 2 modes           -- the COUNTERS

FOUR repetitions, not three: with four arms a four-repetition rotation puts each arm in
each position exactly once, so no arm is systematically first (coldest) or last (warmest).
Three repetitions over four arms would leave the rotation unbalanced.

Both phases go through run_current, which refuses to run a binary whose sources have moved.

Usage:  python3 run_tag.py [--processes 5] [--reps 4] [--modes same,grow] [--phases clean,mech]
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

# phase -> (target, output prefix, processes, reps, tag)
PHASES = {
    "clean": ("bench_tag", "tag", 5, 4, "P"),
    "mech": ("diag_tag", "tag_diag", 1, 1, "MECH"),
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
    parser.add_argument("--reps", type=int, default=4)
    parser.add_argument("--modes", default="same,grow")
    parser.add_argument("--phases", default="clean,mech")
    opts = parser.parse_args()

    modes = [m.strip() for m in opts.modes.split(",") if m.strip()]
    phases = [p.strip() for p in opts.phases.split(",") if p.strip()]

    for phase in phases:
        for mode in modes:
            out = os.path.join(HERE, "results", "main", "%s_%s.jsonl" % (PHASES[phase][1], mode))

            if os.path.exists(out):
                os.remove(out)

    with open(os.path.join(HERE, "logs", "run_tag.log"), "w", encoding="utf-8") as log:
        log.write("tag sweep %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

        for phase in phases:
            target, _, _, _, tag_prefix = PHASES[phase]
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

    print("TAG SWEEP COMPLETE")


if __name__ == "__main__":
    main()

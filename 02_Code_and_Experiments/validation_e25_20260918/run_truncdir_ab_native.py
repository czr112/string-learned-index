#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Native (MinGW) driver for the TRUNCDIR A/B, for the case where WSL is unavailable.

WHY THIS EXISTS. The round's device is `bench_depth_dir` plus `run_depth_dir_ab.py`, and its
timing protocol is unchanged: five independent processes, four repetitions each, the two arms
rotating position inside every repetition, ratios formed WITHIN one repetition. That driver
builds and runs through `tools_build.py` under WSL; this one runs an ALREADY-BUILT native
executable with the identical command shape and the identical output files, and then the same
analyzer (`run_depth_dir_ab.py --analyze`) reads them. It is a toolchain substitute, not a
protocol change, and every figure it produces must be reported as a native (MinGW) figure.

Usage:  python3 run_truncdir_ab_native.py --exe scratch_truncdir/bench_depth_dir.exe \
            --arms C16,DDirT --stem trunc_c16 [--procs 5] [--reps 4]
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")
TR = "../validation_e5_20260913/data/tranco_XN6XN.csv"
CORPORA = [("mixed", 131072, "x"), ("skew", 131072, "x"), ("tranco", 262144, TR)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--arms", default="C16,DDirT")
    ap.add_argument("--stem", required=True)
    ap.add_argument("--procs", type=int, default=5)
    ap.add_argument("--reps", type=int, default=4)
    a = ap.parse_args()

    for kind, _n, _csv in CORPORA:
        path = os.path.join(MAIN, "%s_%s.jsonl" % (a.stem, kind))
        if os.path.exists(path):
            print("REFUSED: %s already exists" % path, file=sys.stderr)
            return 2

    for kind, n, csv in CORPORA:
        for p in range(a.procs):
            tag = "p%d" % p
            cmd = [a.exe, kind, str(n), str(a.reps), csv, tag, "%s_%s" % (a.stem, kind), a.arms]
            print("### " + " ".join(cmd), flush=True)
            rc = subprocess.run(cmd, cwd=HERE).returncode
            if rc != 0:
                print("FAILED rc=%d for %s %s" % (rc, kind, tag), file=sys.stderr)
                return rc
    return 0


if __name__ == "__main__":
    sys.exit(main())

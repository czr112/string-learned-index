#!/usr/bin/env python3
"""E15 sweep driver.

Same shape as E14's: 5 independent processes, each one warms both binaries on a
small dataset (tagged so those records can be excluded -- the first execution of a
fresh binary pays its own page-in cost) and then runs every measured dataset with
both targets. The target order flips on even processes.

The A/B that matters -- two-pass vs one-pass S -- happens INSIDE a single process
and is repeated 3 times there with the order alternated, so it does not depend on
this driver's ordering at all. This driver only supplies repetitions across
processes.

Usage:  python3 run_e15.py [--processes 5]
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))

DATASETS = [
    ("mixed", "131072", ""),
    ("skew", "131072", ""),
    ("tranco", "262144", "../validation_e5_20260913/data/tranco_XN6XN.csv"),
]

WARMUP = ("mixed", "1024", "")

TARGETS = ["bench_e15", "bench_e15_phase"]


def invoke(target, kind, n, csv, tag, log):
    # The CSV slot is always passed EXPLICITLY, even when empty: omitting it
    # shifts the run tag into the CSV argument and the tag is silently lost.
    args = [sys.executable, "tools_build.py", "run", target, kind, n, csv, tag]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = f"{target} {kind} {n} tag={tag} rc={r.returncode} wall={dt:.1f}s"
    print(line, flush=True)
    log.write(line + "\n")
    if r.stdout.strip():
        log.write("  stdout: " + r.stdout.strip() + "\n")
    if r.stderr.strip():
        log.write("  stderr: " + r.stderr.strip().replace("\n", " | ")[-1500:] + "\n")
    if r.returncode != 0:
        log.flush()
        raise SystemExit(f"FAILED: {line}")
    log.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--processes", type=int, default=5)
    a = ap.parse_args()

    with open(os.path.join(HERE, "logs", "run_e15.log"), "w", encoding="utf-8") as log:
        log.write(f"sweep start {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        for p in range(1, a.processes + 1):
            order = TARGETS if p % 2 == 1 else list(reversed(TARGETS))
            log.write(f"--- process P{p}, target order {order} ---\n")
            log.flush()
            for t in order:
                invoke(t, WARMUP[0], WARMUP[1], WARMUP[2], f"warmup-P{p}", log)
            for kind, n, csv in DATASETS:
                for t in order:
                    invoke(t, kind, n, csv, f"P{p}", log)
        log.write(f"sweep end {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()

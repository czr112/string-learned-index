#!/usr/bin/env python3
"""E14 sweep driver.

Three independent processes (P1..P3). Each one:

  1. warms up both binaries on a small dataset, tagged "warmup". The first
     execution of a freshly built binary pays its own page-in cost, which is not
     part of any build being measured; the tag makes those records excludable
     instead of having to guess which ones they were.
  2. runs every measured dataset with BOTH targets back to back.

The order of the two targets is flipped on P2 so that "which binary ran first"
is not perfectly confounded with the target itself.

Every invocation is recorded with the process label, so the analysis can group by
process and report per-process numbers plus medians, exactly as E12/E13 did.

Usage:  python3 run_e14.py [--processes 3]
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))

# Dataset sizes are E13's, unchanged, so this round's stage timings describe the
# same three builds E12 and E13 reported on.
DATASETS = [
    ("mixed", "131072", ""),
    ("skew", "131072", ""),
    ("tranco", "262144", "../validation_e5_20260913/data/tranco_XN6XN.csv"),
]

WARMUP = ("mixed", "1024", "")

TARGETS = ["bench_e14", "bench_e14_phase"]   # plain first, unless flipped


def invoke(target, kind, n, csv, tag, log):
    # The CSV slot is passed EXPLICITLY even when empty. Omitting it shifts the
    # run tag into the CSV argument, which the bench then records as its input
    # file and which silently drops the tag -- and for a non-tranco kind nothing
    # complains, because that kind ignores the file.
    args = [sys.executable, "tools_build.py", "run", target, kind, n, csv, tag]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = f"{target} {kind} {n} tag={tag} rc={r.returncode} wall={dt:.1f}s"
    print(line, flush=True)
    log.write(line + "\n")
    if r.stdout.strip():
        log.write("  stdout: " + r.stdout.strip() + "\n")
    if r.returncode != 0:
        log.write("  stderr: " + r.stderr.strip()[-2000:] + "\n")
        log.flush()
        raise SystemExit(f"FAILED: {line}")
    log.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--processes", type=int, default=3)
    a = ap.parse_args()

    with open(os.path.join(HERE, "logs", "run_e14.log"), "w", encoding="utf-8") as log:
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

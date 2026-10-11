#!/usr/bin/env python3
"""G4/E1: the PGO/LTO comparison.

FOUR BUILDS OF ONE SOURCE. `bench_widest.cpp` is compiled four ways, so the algorithm, the ISA
target, the data structures and the shipped configuration are identical and only the
compilation differs:

    base      -O3 -march=x86-64-v2 -static          <- the shipped build, this is `bench_widest`
    lto       base + -flto
    pgo       base + -fprofile-use
    pgo_lto   base + -flto -fprofile-use

THE PROFILE COMES FROM A HELD-OUT TRAINING WORKLOAD. Training runs the INSTRUMENTED build
(`bench_pgo_gen`) on smaller corpora and on the OTHER mode -- 65,536 keys, `grow` -- while
evaluation uses 131,072/262,144 keys and `same`. The evaluation seeds and sizes are therefore
not the ones the profile was collected from, and the profile is never re-fitted after seeing a
result.

VERIFYING THAT PGO ACTUALLY APPLIED. "It linked" is not evidence. The `-Wmissing-profile` flag
is on for the two PGO builds, so g++ says so if it could not find the data; the driver reads
back the compiler's own stderr from the build record and REFUSES to continue if a
profile-related diagnostic appears. It also records the .gcda inventory and the executable
hashes, so a PGO build that came out byte-identical to its non-PGO twin is visible.

ROTATION. Four separate executables cannot rotate inside one process, so a "repetition" here is
one invocation of each build, run back to back in a rotated order. Each record's run_tag
encodes process, repetition and build, so analysis can pair strictly within a repetition.

Usage:  python3 run_pgo.py [--processes 5] [--reps 4] [--run-id TAG]
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# build -> (target, tag suffix)
BUILDS = [
    ("base", "bench_widest", "B"),
    ("lto", "bench_lto", "L"),
    ("pgo", "bench_pgo", "P"),
    ("pgo_lto", "bench_pgo_lto", "PL"),
]

TRAIN = [("mixed", "65536", ""), ("skew", "65536", "")]
# The training MODE. The first E1 run trained on `grow` and evaluated on `same` -- a held-out
# but distributionally DIFFERENT profile, which is the honest default because a profile trained
# on the evaluation distribution is a profile trained on the evaluation data (make_data is
# deterministic, so the smaller corpora are PREFIXES of the larger ones). A second run trains on
# the evaluation MODE at a quarter of the size as a robustness check, and the report states the
# partial overlap rather than hiding it.
TRAIN_MODE = "grow"

EVAL = [
    ("mixed", "131072", ""),
    ("skew", "131072", ""),
    ("tranco", "262144", "../validation_e5_20260913/data/tranco_XN6XN.csv"),
]
EVAL_MODE = "same"

PROFILE_DIR = "build/pgo_profile"
WORK = "results/main/long_widest_%s.jsonl"


def run(args, log, label, timeout=3600):
    started = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True, timeout=timeout)
    elapsed = time.time() - started
    line = "%-46s rc=%d wall=%.0fs" % (label, r.returncode, elapsed)
    print("  " + line, flush=True)
    log.write("  " + line + "\n")
    log.flush()

    if r.returncode != 0:
        log.write(r.stdout[-2000:] + "\n" + r.stderr[-4000:] + "\n")
        raise SystemExit("REFUSED: %s failed (rc=%d). Nothing was timed." % (label, r.returncode))

    return r


def build(target, log, label):
    return run([sys.executable, "tools_build.py", target], log, "build " + label)


def archive(work, dest):
    """Move whatever the children appended into the run's own file. Returns the line count.

    Append-only, so nothing written earlier is ever rewritten. Called after EVERY invocation:
    the guarantee is only as good as its call site.
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


def record_of(target):
    path = os.path.join(HERE, "logs", "build_%s.json" % target)

    with open(path, encoding="utf-8") as f:
        return json.load(f)


def assert_no_profile_complaint(target, log):
    """The compiler's own stderr is the evidence that PGO applied (or did not)."""
    rec = record_of(target)
    tail = rec.get("stderr_tail") or ""
    lower = tail.lower()

    bad = [w for w in ("missing profile", "profile data may be out of date",
                       "profile count data file not found", "coverage mismatch")
           if w in lower]

    if bad:
        log.write("  !! %s: the compiler complained: %s\n" % (target, bad))
        raise SystemExit(
            "REFUSED: %s reports %s -- the profile was NOT applied, so timing it would "
            "measure the wrong thing." % (target, bad))

    log.write("  %-28s no profile diagnostic; exe %s\n"
              % (target, rec.get("exe_sha256", "")[:16]))

    return rec.get("exe_sha256")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--processes", type=int, default=5)
    parser.add_argument("--reps", type=int, default=4)
    parser.add_argument("--run-id", default=time.strftime("%Y%m%d_%H%M%S"))
    parser.add_argument("--build-only", action="store_true",
                        help="train and build, then stop before the evaluation sweep")
    parser.add_argument("--train-mode", default=TRAIN_MODE,
                        choices=["same", "grow"],
                        help="the workload mode the profile is collected from")
    opts = parser.parse_args()

    if opts.reps % len(BUILDS) != 0:
        raise SystemExit("REFUSED: reps must be a multiple of the build count (%d) so every "
                         "build occupies every position exactly once" % len(BUILDS))

    log_path = os.path.join(HERE, "logs", "run_pgo_%s.log" % opts.run_id)

    with open(log_path, "w", encoding="utf-8") as log:
        log.write("PGO/LTO run_id=%s  %s\n" % (opts.run_id, time.strftime("%Y-%m-%d %H:%M:%S")))

        # ---- 1. training ---------------------------------------------------------------
        prof = os.path.join(HERE, PROFILE_DIR)
        for old in glob.glob(os.path.join(prof, "**", "*.gcda"), recursive=True):
            os.remove(old)

        print("phase train", flush=True)
        log.write("\nphase train\n")
        build("bench_pgo_gen", log, "bench_pgo_gen (instrumented)")

        for kind, n, csv in TRAIN:
            run([sys.executable, "tools_build.py", "run", "bench_pgo_gen", kind, n, "1",
                 csv, "TRAIN", opts.train_mode], log,
                "train %s %s %s" % (kind, n, opts.train_mode))

        # GCC mirrors the object's ABSOLUTE directory under the profile dir, so the data files
        # are several levels down, not at the top. The non-recursive glob here silently found
        # nothing on the first run and the driver refused -- which is the right outcome for a
        # wrong check, and a reminder that "no files" and "files I did not look for" look alike.
        gcda = sorted(glob.glob(os.path.join(prof, "**", "*.gcda"), recursive=True))
        total = sum(os.path.getsize(p) for p in gcda)
        print("  profile: %d .gcda file(s), %d bytes" % (len(gcda), total), flush=True)
        log.write("  profile: %d .gcda file(s), %d bytes\n" % (len(gcda), total))

        for p in gcda:
            log.write("    %s (%d bytes)\n" % (p, os.path.getsize(p)))

        if not gcda or total == 0:
            raise SystemExit("REFUSED: training produced no profile data; PGO cannot be tested")

        # ---- 2. the four builds --------------------------------------------------------
        print("phase build", flush=True)
        log.write("\nphase build\n")
        hashes = {}
        build("bench_widest", log, "bench_widest (base)")
        hashes["base"] = record_of("bench_widest").get("exe_sha256")
        build("bench_lto", log, "bench_lto")
        hashes["lto"] = record_of("bench_lto").get("exe_sha256")
        build("bench_pgo", log, "bench_pgo")
        hashes["pgo"] = assert_no_profile_complaint("bench_pgo", log)
        build("bench_pgo_lto", log, "bench_pgo_lto")
        hashes["pgo_lto"] = assert_no_profile_complaint("bench_pgo_lto", log)

        log.write("\nexecutable hashes:\n")
        for k, v in hashes.items():
            log.write("  %-10s %s\n" % (k, v))
        print("  hashes: " + "  ".join("%s=%s" % (k, v[:8]) for k, v in hashes.items()),
              flush=True)

        if len(set(hashes.values())) != len(hashes):
            log.write("  !! two builds produced the SAME executable -- the comparison is not "
                      "measuring four compilations\n")
            raise SystemExit("REFUSED: at least two builds produced identical executables")

        if opts.build_only:
            print("BUILD-ONLY: stopping before the evaluation sweep")
            return

        # ---- 3. the evaluation sweep ---------------------------------------------------
        print("phase eval", flush=True)
        log.write("\nphase eval\n")

        for mode in [EVAL_MODE]:
            work = os.path.join(HERE, WORK % mode)
            dest = os.path.join(HERE, "results", "main",
                                "long_widest_%s_%s.jsonl" % (opts.run_id, mode))

            if os.path.exists(work):
                os.remove(work)

            total_rows = 0

            for p in range(1, opts.processes + 1):
                for r in range(opts.reps):
                    # Rotate which build goes first, exactly as the in-process benchmark rotates
                    # its arms: every build occupies every position once across the repetitions.
                    order = [(r + j) % len(BUILDS) for j in range(len(BUILDS))]

                    for pos, idx in enumerate(order):
                        name, target, tagv = BUILDS[idx]
                        tag = "P%dR%d%s" % (p, r, tagv)

                        for kind, n, csv in EVAL:
                            run([sys.executable, "tools_build.py", "run", target, kind, n, "1",
                                 csv, tag, mode], log,
                                "%s %s %s pos=%d" % (name, kind, mode, pos))

                            # ARCHIVED AFTER EVERY INVOCATION, not once per mode. The first
                            # version of this file archived only after the whole mode finished,
                            # which left roughly an hour of an interrupted sweep nowhere but in
                            # a working file the next run would truncate -- the exact state the
                            # E25/E38 drivers were fixed to stop producing.
                            total_rows += archive(work, dest)

            line = "    archived %d row(s) to %s" % (total_rows, os.path.basename(dest))
            print(line, flush=True)
            log.write(line + "\n")

        log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

    print("PGO/LTO SWEEP COMPLETE run_id=%s" % opts.run_id)


if __name__ == "__main__":
    main()

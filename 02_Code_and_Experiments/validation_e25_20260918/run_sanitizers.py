#!/usr/bin/env python3
"""ITEM B: the ASan/UBSan memory-safety coverage run. NOT a timing run.

THE CLAIM, IN THE REQUEST'S OWN WORDS
-------------------------------------
The user asked for one explicit memory-safety acceptance pass, and stated its bound:

    在指定配置与测试覆盖下，ASan/UBSan 未报告问题。

That means: under the listed configuration and test coverage, ASan/UBSan reported no
problem. That is the WHOLE claim. It does NOT prove every path is safe, and it does NOT
replace the scan-contract acceptance (ITEM A). The sentence is printed verbatim at the end
of every run, next to the list of what was covered and what was not.

EVERY NUMBER THESE RUNS PRINT IS MEANINGLESS AS A MEASUREMENT
-------------------------------------------------------------
The targets are built at -O1 with ASan's instrumentation and ASan's allocator, so they are
far slower than the shipped -O3 builds. These runs exist to find memory errors. Nothing
they print may be reported as performance, and anything they append to a results file is
discarded again (see PROTECTED_OUTPUTS).

HOW IT IS PUT TOGETHER
----------------------
  * tools_build.py owns the build. Its SAN_TARGETS set names targets that map, through its
    SRC table, onto EXISTING sources; no shipped target's flags, record or .exe is touched.
  * Runs go through `tools_build.py run`, which refuses a stale or tampered binary. The
    child's stdout and stderr are captured here, so a sanitizer report cannot scroll past.
  * POSITIVE CONTROLS run first: two deliberately broken programs, one that only ASan can
    see and one that only UBSan can see, must BOTH be reported. A coverage run whose
    detector never fired proves nothing -- the E31 lesson ("a gate that passes without
    executing the branch under test certifies nothing"), applied to the detector itself.
    If the controls do not fire, no claim is made and nothing is run.

OUTPUTS
-------
    logs/run_sanitizers.log                the transcript
    results/main/sanitizer_runs.jsonl      one JSON line per run

Exit status is non-zero if any build failed, any run failed, or any run reported a
sanitizer error.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, HERE)

import tools_build  # noqa: E402  -- HERE has to be on sys.path first

LOG = os.path.join(HERE, "logs", "run_sanitizers.log")

RECORDS = os.path.join(HERE, "results", "main", "sanitizer_runs.jsonl")

# halt_on_error: the first problem ends the process, so a non-zero exit status means
# something. detect_leaks=0: the earlier round (validation_20260906) found LeakSanitizer's
# end-of-process scan does not work in every environment, and it wrote that finding down
# instead of claiming a leak check it had not run. The same boundary is kept here -- this
# run claims AddressSanitizer and UndefinedBehaviorSanitizer, and says nothing about leaks.
SAN_ENV = {
    "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
    "UBSAN_OPTIONS": "halt_on_error=1",
}

# What the runtimes themselves print when they find something. The verdict does not rest
# on the exit status alone.
SAN_MARKERS = (
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "LeakSanitizer",
    "runtime error:",
)

# What a toolchain prints when it cannot build a sanitizer target at all. Reported apart
# from a finding, because "the detector could not be built" must never be read as
# "the detector found nothing".
NO_RUNTIME_MARKERS = (
    "cannot find -lasan",
    "cannot find -lubsan",
    "unrecognized command line option",
    "unrecognized argument",
)

# A benchmark APPENDS its rows into results/main/*.jsonl -- the same files the real timing
# sweeps write. Rows produced by an -O1 ASan build are not measurements, so they must not
# be left where a later reader could take them for one. The driver records each file's
# length before the run and truncates it back afterwards (and removes the file if it did
# not exist before), then says how much it discarded.
PROTECTED_OUTPUTS = {
    "san_bench_long_hyst": ["results/main/long_hyst_same.jsonl"],
}

# The positive controls. Each is one bug, and each can be caught by exactly one of the two
# runtimes, so a pass proves both of them are live rather than just one.
#
#   ASan control: a heap overrun through a pointer whose block size the compiler cannot
#   see (it comes back from a noinline function), so -fsanitize=undefined's object-size
#   check stays silent and a report can only come from ASan's allocator redzones.
PROBE_ASAN = """#include <cstdio>
#include <cstdlib>

__attribute__((noinline)) int* make(int n) {
    return static_cast<int*>(malloc(sizeof(int) * (unsigned)n));
}

int main() {
    int* a = make(64);
    a[63] = 7;
    volatile int x = a[64];
    printf("%d\\n", x);
    free(a);
    return 0;
}
"""

#   UBSan control: a signed overflow. ASan has nothing to say about it; only the UB checks
#   can report it.
PROBE_UBSAN = """#include <cstdio>

int main() {
    volatile int x = 2147483647;
    x = x + 1;
    printf("%d\\n", x);
    return 0;
}
"""

# The coverage set. `covers` is copied into the record and printed in the summary, so the
# claim in the log can be read without this file open next to it.
RUNS = [
    {
        "target": "san_check_e31",
        "args": ["mixed", "131072", "", "", "same"],
        "dataset": "mixed 131072 same",
        "covers": "E31's in-place sub-block repack, the insert that follows it, deletes of "
                  "the first and last live records, emptied middle sub-blocks, and the "
                  "boundary directory re-checked against its own sub-blocks on BOTH arms",
    },
    {
        "target": "san_check_e31",
        "args": ["mixed", "131072", "", "", "grow"],
        "dataset": "mixed 131072 grow",
        "covers": "the same repack and placement paths run with a growing key length, so "
                  "suffixes that change size move through them",
    },
    {
        "target": "san_check_e9_multiblock",
        "args": [],
        "dataset": "self-contained pool, 2 trials",
        "covers": "structural invariants: rebuild, the sub-block sweep, the boundary "
                  "directory, and empty middle sub-blocks",
    },
    {
        "target": "san_check_e11_locate",
        "args": [],
        "dataset": "self-contained pool, 3 trials",
        "covers": "locate against the boundary directory across multi-block groups, "
                  "including groups whose overflow holds records below every primary key",
    },
    {
        "target": "san_check_freeze",
        "args": ["mixed", "131072", "", ""],
        "dataset": "mixed 131072",
        "covers": "rebuild paths: three whole indexes built from one plan at the shipped "
                  "defaults, including the exact-capacity sub-block cases the allocator "
                  "produces",
    },
    {
        "target": "san_bench_long_hyst",
        "args": ["mixed", "131072", "1", "", "", "same"],
        "dataset": "mixed 131072 reps=1 same",
        "covers": "one overflow-heavy workload: overflow growth, overflow deletion and "
                  "saved positions going stale under long churn, at ONE repetition on the "
                  "smallest dataset -- coverage, not a timing run",
    },
]

# The other half of the bounded claim. The user was explicit that a clean sanitizer run
# cannot replace the scan-contract acceptance, and that it cannot prove all paths safe.
NOT_COVERED = [
    "LeakSanitizer, and leaks in general: leak detection is switched off "
    "(ASAN_OPTIONS=detect_leaks=0), so this run says nothing about leaks.",
    "the tranco dataset (262144 keys) and every dataset/mode/repetition combination the "
    "timing sweeps use; only the runs listed above were executed.",
    "code reached only under -DE25_DIAG in the programs that were NOT built with it here "
    "(only san_check_e31 carries the counters).",
    "the split path as a claim of its own: whatever the listed programs execute is "
    "covered, but no run here asserts on a split in its own right.",
    "any toolchain or platform other than the one this driver probes; the positive "
    "controls prove the detector is live ON THIS MACHINE only.",
    "memory errors that neither ASan nor UBSan can see.",
    "the scan-contract acceptance (ITEM A): no sanitizer result says anything about scan "
    "order, and the two must not be run together into one conclusion.",
]


def log_line(log, text=""):
    print(text, flush=True)
    log.write(text + "\n")
    log.flush()


def hits_in(text, markers):
    return [m for m in markers if m in text]


def positive_controls(log):
    """Build and run the two deliberately broken programs. Both must be reported.

    The point is not to test the sanitizers. It is to prove that on this machine the
    detector is LIVE, so that a quiet sweep means "nothing was found" rather than "nothing
    was looking".
    """
    tmp = tempfile.mkdtemp(prefix="e25_san_probe_")
    suffix = ".exe" if os.name == "nt" else ""
    ok = True

    try:
        for name, source, accepted in (("asan", PROBE_ASAN, ("AddressSanitizer",)),
                                       ("ubsan", PROBE_UBSAN,
                                        ("UndefinedBehaviorSanitizer", "runtime error:"))):
            src = os.path.join(tmp, "probe_%s.cpp" % name)
            exe = os.path.join(tmp, "probe_%s%s" % (name, suffix))

            with open(src, "w", encoding="utf-8", newline="\n") as f:
                f.write(source)

            bc = subprocess.run(tools_build.CXX_SAN + [src, "-o", exe], cwd=tmp,
                                capture_output=True, text=True, errors="replace")

            if bc.returncode != 0 or not os.path.exists(exe):
                log_line(log, "CONTROL %s: could not build" % name)
                log_line(log, "  " + (bc.stderr.strip()[-1200:] or "(no stderr)"))
                ok = False
                continue

            env = os.environ.copy()
            env.update(SAN_ENV)

            rc = subprocess.run([exe], cwd=tmp, capture_output=True, text=True,
                                errors="replace", env=env)

            seen = rc.stdout + rc.stderr
            hit = next((m for m in accepted if m in seen), None)

            if rc.returncode != 0 and hit:
                log_line(log, "CONTROL %s: fired (%s, rc=%d)" % (name, hit, rc.returncode))
            else:
                log_line(log, "CONTROL %s: DID NOT FIRE (rc=%d, %d bytes of output)"
                              % (name, rc.returncode, len(seen)))
                ok = False
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    return ok


def build(target, log):
    r = subprocess.run([sys.executable, "tools_build.py", target], cwd=HERE,
                       capture_output=True, text=True, errors="replace")
    out = r.stdout + r.stderr

    if r.returncode == 0:
        log_line(log, "BUILD %-22s ok" % target)
    else:
        log_line(log, "BUILD %-22s FAILED (rc=%d)" % (target, r.returncode))
        log_line(log, "  " + (r.stderr.strip()[-1500:] or "(no stderr)"))

    return r.returncode == 0, out


def protect_start(target):
    """Remember how long each results file was before the run."""
    saved = {}

    for rel in PROTECTED_OUTPUTS.get(target, []):
        full = os.path.join(HERE, rel)
        saved[rel] = os.path.getsize(full) if os.path.exists(full) else None

    return saved


def protect_end(target, saved, log):
    """Undo whatever the run appended to a results file, and say how much it was."""
    discarded = 0

    for rel, before in saved.items():
        full = os.path.join(HERE, rel)

        if before is None:
            if os.path.exists(full):
                discarded += os.path.getsize(full)
                os.remove(full)
            continue

        if not os.path.exists(full):
            continue

        now = os.path.getsize(full)

        if now > before:
            discarded += now - before
            with open(full, "r+b") as f:
                f.truncate(before)

    if discarded:
        log_line(log, "  DISCARDED %d byte(s) this sanitizer run appended to %s -- rows from an"
                      " -O1 instrumented build are not measurements and are not kept"
                      % (discarded, ", ".join(sorted(saved))))

    return discarded


def build_failed_record(item, out):
    missing = hits_in(out, NO_RUNTIME_MARKERS)
    status = "BUILD_FAILED_NO_RUNTIME" if missing else "BUILD_FAILED"

    return {
        "target": item["target"],
        "dataset": item["dataset"],
        "args": item["args"],
        "build_ok": False,
        "exit_code": None,
        "sanitizer_report": False,
        "markers": [],
        "status": status,
        "wall_s": 0.0,
        "discarded_result_bytes": 0,
        "covers": item["covers"],
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
    }


def run_one(item, log):
    target = item["target"]
    saved = protect_start(target)

    env = os.environ.copy()
    env.update(SAN_ENV)

    t0 = time.time()
    r = subprocess.run([sys.executable, "tools_build.py", "run", target] + item["args"],
                       cwd=HERE, capture_output=True, text=True, errors="replace", env=env)
    wall = time.time() - t0

    discarded = protect_end(target, saved, log)

    out = r.stdout + r.stderr
    hits = hits_in(out, SAN_MARKERS)

    if hits:
        status = "SANITIZER_REPORT"
    elif r.returncode != 0:
        status = "RUN_FAILED"
    else:
        status = "OK"

    log_line(log, "RUN   %-22s %-26s rc=%-3d sanitizer=%s (%.0fs)"
                  % (target, item["dataset"], r.returncode,
                     ("YES: " + ", ".join(hits)) if hits else "no", wall))

    if status == "OK":
        for line in out.strip().splitlines()[-3:]:
            log_line(log, "  " + line)
    else:
        log_line(log, "  ---- output tail ----")
        for line in out.strip().splitlines()[-40:]:
            log_line(log, "  " + line)

    return {
        "target": target,
        "dataset": item["dataset"],
        "args": item["args"],
        "build_ok": True,
        "exit_code": r.returncode,
        "sanitizer_report": bool(hits),
        "markers": hits,
        "status": status,
        "wall_s": round(wall, 1),
        "discarded_result_bytes": discarded,
        "covers": item["covers"],
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
    }


def write_records(records):
    os.makedirs(os.path.dirname(RECORDS), exist_ok=True)

    with open(RECORDS, "w", encoding="utf-8") as f:
        for rec in records:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")


def summary(log, records, verdict, extra=()):
    log_line(log, "")
    log_line(log, "=" * 78)
    log_line(log, "ITEM B -- THE BOUNDED CLAIM")
    log_line(log, "=" * 78)
    log_line(log, "CLAIM: 在指定配置与测试覆盖下，ASan/UBSan 未报告问题。")
    log_line(log, "       Under the listed configuration and test coverage, ASan/UBSan")
    log_line(log, "       reported no problem. That does NOT prove every path is safe, and")
    log_line(log, "       it does NOT replace the scan-contract acceptance (ITEM A).")
    log_line(log, "")
    log_line(log, verdict)

    for line in extra:
        log_line(log, line)

    log_line(log, "")
    log_line(log, "COVERED -- runs that completed with NO sanitizer report:")
    live = [r for r in records if r.get("status") == "OK"]

    if not live:
        log_line(log, "  (none)")

    for r in live:
        log_line(log, "  %-22s %-26s" % (r["target"], r["dataset"]))
        log_line(log, "  %-22s %s" % ("", r["covers"]))

    log_line(log, "")
    log_line(log, "NOT COVERED -- this run says NOTHING about:")
    for n in NOT_COVERED:
        log_line(log, "  * " + n)

    log_line(log, "")
    log_line(log, "records: results/main/sanitizer_runs.jsonl")
    log_line(log, "log:     logs/run_sanitizers.log")
    log_line(log, "=" * 78)


def main():
    os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)

    with open(LOG, "w", encoding="utf-8") as log:
        log_line(log, "=" * 78)
        log_line(log, "ITEM B: ASan/UBSan memory-safety coverage -- %s"
                      % time.strftime("%Y-%m-%d %H:%M:%S"))
        log_line(log, "These runs are for MEMORY-SAFETY COVERAGE ONLY. Their timings are")
        log_line(log, "meaningless: the targets are -O1 and instrumented. Never report them")
        log_line(log, "as performance, and never mix them with a timing sweep's numbers.")
        log_line(log, "=" * 78)

        compiler = getattr(tools_build, "CXX_SAN", None)
        targets = getattr(tools_build, "SAN_TARGETS", None)

        if not compiler or not targets:
            log_line(log, "ABORT: tools_build.py has no CXX_SAN / SAN_TARGETS. Apply the build")
            log_line(log, "half of the patch first. Nothing was built and nothing was run.")
            return 3

        log_line(log, "compiler line: %s" % " ".join(compiler))
        log_line(log, "")

        wanted = [item["target"] for item in RUNS]
        unknown = sorted(set(wanted) - set(targets))

        if unknown:
            log_line(log, "ABORT: not in tools_build.SAN_TARGETS: %s. Nothing was run."
                          % ", ".join(unknown))
            return 3

        log_line(log, "positive controls -- a detector that never fires proves nothing:")
        live = positive_controls(log)
        log_line(log, "")

        if not live:
            log_line(log, "ABORT: the sanitizer detector did not fire on a program with a known")
            log_line(log, "bug, or the sanitizer build could not be produced at all. A quiet")
            log_line(log, "sweep from this toolchain would mean NOTHING, so nothing was run and")
            log_line(log, "no claim is made.")
            write_records([])
            summary(log, [], "RESULT: ABORTED -- no working detector, so no coverage and no claim.")
            return 1

        builds = {}

        for target in sorted(set(wanted)):
            builds[target] = build(target, log)

        log_line(log, "")

        records = []

        for item in RUNS:
            built_ok, build_out = builds[item["target"]]

            if not built_ok:
                records.append(build_failed_record(item, build_out))
                continue

            records.append(run_one(item, log))

        write_records(records)

        reports = [r for r in records if r.get("status") == "SANITIZER_REPORT"]
        failed = [r for r in records if r.get("status") == "RUN_FAILED"]
        unbuilt = [r for r in records if r.get("status") in ("BUILD_FAILED",
                                                             "BUILD_FAILED_NO_RUNTIME")]

        extra = []

        if any(r["status"] == "BUILD_FAILED_NO_RUNTIME" for r in unbuilt):
            extra.append("")
            extra.append("NOTE: this toolchain has no sanitizer runtime (the link failed with")
            extra.append("      'cannot find -lasan' / 'cannot find -lubsan'). That is why")
            extra.append("      nothing could be covered. Point the build at a toolchain that")
            extra.append("      has the runtimes and re-run, for example:")
            extra.append("        set E25_SAN_CXX=clang++ -std=c++17 -O1 -g "
                         "-fsanitize=address,undefined -fno-omit-frame-pointer")
            extra.append("      Nothing above is evidence of memory safety.")

        if reports:
            verdict = ("RESULT: FAIL -- %d run(s) reported a sanitizer problem. See the output"
                       " tails above." % len(reports))
            code = 1
        elif failed:
            verdict = ("RESULT: FAIL -- %d run(s) exited non-zero without a sanitizer report."
                       " That is a failure to investigate, not a clean result." % len(failed))
            code = 1
        elif unbuilt:
            verdict = ("RESULT: FAIL -- %d target(s) could not be built with the sanitizer"
                       " flags." % len(unbuilt))
            code = 1
        else:
            verdict = ("RESULT: PASS -- %d run(s) completed and none reported a sanitizer"
                       " problem. The claim is bounded by the coverage below."
                       % len([r for r in records if r.get("status") == "OK"]))
            code = 0

        summary(log, records, verdict, extra)

    return code


if __name__ == "__main__":
    sys.exit(main())

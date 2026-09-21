#!/usr/bin/env python3
"""E25 build/run closure (extended from the E8..E24 one).

Carries over the two guarantees the earlier rounds relied on, and adds what this
round needs:

build:  compile to a UNIQUE temporary output; only on success atomically rename to
        build/<target>.exe and write build/<target>.built.json with source hashes,
        the compile command, the executable hash and a build id. A failed compile
        leaves no runnable artifact behind.
run:    python tools_build.py run <target> [args...]  — refuses to run unless the
        build record exists and the executable hash matches it. ALSO refuses when any
        source recorded in the build has changed since: without that check, editing a
        source and forgetting to rebuild silently runs the OLD binary, which is exactly
        what happened once in E34 (the only symptom was a missing JSON field).
run_current:   same as `run`. The explicit name exists so new experiments can say which
        guarantee they are relying on.
run_recorded:  python tools_build.py run_recorded <target> [args...] — runs the binary
        the record describes WITHOUT re-verifying the sources, printing the drift and
        stating that the results do not describe the current source. For deliberately
        re-running a historical arm.

E25 additions:
  * V0/V1/V2 are ONE binary, three Config settings -- not three builds. The default
    (both flags off) reproduces E24 exactly, which is what lets the inherited gates
    pin V0 down.
  * check_e25_write: the differential gate. Three indexes from one plan, one
    operation sequence, compared ELEMENT BY ELEMENT after every batch. A missing
    comparison is not caught by a checksum, so it is not used.
  * the heavy counter build (E25_DIAG) and the clean timing build are separate
    targets. Algorithm-necessary maintenance must never live behind the diagnostic
    macro -- see the round's LOG.
  * no libart yet: the ART arm of the comparison is not built in this step.
  * the gates carry over unchanged.
"""
import hashlib, json, os, subprocess, sys, time

CXX = ["g++", "-std=c++17", "-O3", "-march=x86-64-v2", "-static"]
CC = ["gcc", "-std=c99", "-O3", "-march=x86-64-v2"]

# ITEM B: the sanitizer compiler line, used ONLY for the targets in SAN_TARGETS below.
# Two deliberate differences from CXX, and both matter:
#   * -static is DROPPED. The sanitizer runtime is a separate library that is not
#     compatible with a fully static link: the link fails, or the runtime never
#     initialises. This is the reason the sanitizer builds need their own line.
#   * -O1 -g instead of -O3. ASan's own instrumentation and allocator dominate the
#     runtime anyway, and -g is what makes a report name a source line. The goal of
#     these builds is COVERAGE, not speed -- their timings are meaningless and are
#     never reported as performance.
# The compiler itself is overridable, because a host can have a g++ with no
# sanitizer runtime at all (this machine's MinGW toolchain ships no libasan and no
# libubsan, so a sanitizer build there fails at link time):
#     set E25_SAN_CXX=clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
CXX_SAN = os.environ.get("E25_SAN_CXX", "").split() or [
    "g++", "-std=c++17", "-O1", "-g", "-march=x86-64-v2",
    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
]

# ITEM B: the targets built with CXX_SAN instead of CXX. Every one is a NEW target
# name over an EXISTING source (see SRC below), so no shipped target's flags, build
# record or executable is touched -- the same pattern already used for diag_long,
# which is bench_long.cpp under a second name. run_sanitizers.py runs exactly this set.
SAN_TARGETS = {
    "san_check_e31",
    "san_check_e9_multiblock",
    "san_check_e11_locate",
    "san_check_freeze",
    "san_bench_long_hyst",
}

# Per-target flags.
EXTRA = {
    "check_e11_locate": ["-DE12_COUNT"],   # this gate reads candidate_blocks
    "check_e19_reuse": ["-DE18_COUNT"],
    "check_e20_r2": ["-DE18_COUNT"],
    # The E25 gate asserts on the branch counters, so it is built WITH them. A gate
    # is not a timing build, so the counters cost nothing that is measured.
    "check_e25_write": ["-DE25_DIAG"],
    "diag_e25_calls": ["-DE25_DIAG"],      # the call matrix IS a counter build
    "diag_e26": ["-DE25_DIAG"],            # compaction diagnostics + tag-reuse check
    "diag_scan": ["-DE25_DIAG"],           # scan work breakdown
    "diag_long": ["-DE25_DIAG"],           # long-run structural counters
    "diag_transfer": ["-DE25_DIAG"],       # long-run: does a replacement change group?
    "diag_overflow": ["-DE25_DIAG"],       # long-run: why does an insert overflow?
    # The E31 gate asserts that the reclaim branch actually ran (subblock_reclaims > 0),
    # which needs the counters. A gate is not a timing build, so the counters cost
    # nothing that is measured. Source has an #error if this flag is missing.
    "check_e31": ["-DE25_DIAG"],
    # The scan-contract gate injects a deliberately NON-monotone group mapping through
    # the E25_DIAG-only hook on Region (Region::sar_rotate_groups), and asserts that the
    # injection really makes the scan misbehave -- with the hook off it also asserts that
    # a second index built the same way is identical to the first. The hook is not a
    # Config switch, so no switches.hpp table changes and the frozen configuration is
    # untouched. Source has an #error if this flag is missing. A gate is not a timing
    # build, so the counters cost nothing that is measured.
    "check_scan_contract": ["-DE25_DIAG"],
    # The O1 gate asserts that the stable-store arm actually used the new
    # representation (its record-shift counter stays 0 while its handle counter rises)
    # and that the erases really reached the overflow container. Both are read from the
    # E25 counters, so this target is built WITH them -- as check_e31 is. Source has an
    # #error if the flag is missing, so a wrong build is a hard stop rather than a report
    # full of zeros that looks like the container was never touched. A gate is not a
    # timing build, so the counters cost nothing that is measured.
    "check_overflow_store": ["-DE25_DIAG"],
    # The hysteresis counters are read by bench_long_hyst's own non-vacuous gate,
    # so the diagnostic build of the SAME source is a second target (as diag_long is
    # for bench_long). The clean timing build stays counter-free.
    "diag_hyst": ["-DE25_DIAG"],
    # The O1 arm asserts on its own movement counters, so the diagnostic build of the
    # same source is a second target, as diag_hyst is for bench_long_hyst.
    "diag_o1": ["-DE25_DIAG"],
    # The E35 prefix diagnostic: same source, diagnostic counters in.
    "diag_prefix": ["-DE25_DIAG"],
    # The E36 composition diagnostic needs the shift counters, so it is diagnostic-only.
    "diag_compose": ["-DE25_DIAG"],
    # E36: the gate reads the tag counters for its non-vacuity assertions, and the
    # benchmark has a diagnostic target for the same counters.
    "check_tag_store": ["-DE25_DIAG"], "diag_tag": ["-DE25_DIAG"],
    # E37: the migration feasibility scan is diagnostic-only by construction.
    "diag_migrate": ["-DE25_DIAG"],
    "check_migrate": ["-DE25_DIAG"],
    # E37: the R0/R1 benchmark's diagnostic twin. Named _b because diag_migrate is
    # already the feasibility program.
    "diag_migrate_b": ["-DE25_DIAG"],
    # E37 step 2b: repairing an already-degraded snapshot.
    "diag_repair": ["-DE25_DIAG"],
    "bench_w_t": ["-DE30_TIME"],           # same source as bench_w, clock ON
    # Needs BOTH: e30_probe is declared inside the E25_DIAG member block because it
    # calls diag_group(), so -DE30_TIME alone leaves it uncompiled.
    "bench_repack": ["-DE30_TIME", "-DE25_DIAG"],
    # E31 item 3, and it needs both for the same reason plus one more: the overflow
    # clocks are E30_T0/E30_ADD (so -DE30_TIME), and the movement counters and the
    # container-size accessor are declared under -DE25_DIAG. The source carries an
    # #error if either is missing, so a wrong build is a hard stop rather than a report
    # full of zeros that looks like "the overflow container is free".
    "diag_ov_cost": ["-DE30_TIME", "-DE25_DIAG"],
    # ITEM B: the sanitizer targets take the SAME flags as the shipped target built
    # from the same source, for the same reasons -- check_e31 asserts that the reclaim
    # branch ran (its source carries an #error without the counters) and
    # check_e11_locate reads candidate_blocks. A sanitizer build is not a timing
    # build either, so the counters cost nothing that is measured anywhere.
    "san_check_e11_locate": ["-DE12_COUNT"],
    "san_check_e31": ["-DE25_DIAG"],
}
LIBS = {"bench_e25": ["build/art.o"], "bench_e26": ["build/art.o"],
        "bench_long_art": ["build/art.o"], "bench_long_hyst": ["build/art.o"],
        "diag_hyst": ["build/art.o"], "san_bench_long_hyst": ["build/art.o"],
        "bench_long_o1": ["build/art.o"], "diag_o1": ["build/art.o"],
        "bench_prefix": ["build/art.o"], "diag_prefix": ["build/art.o"],
        "diag_compose": ["build/art.o"],
        "bench_tag": ["build/art.o"], "diag_tag": ["build/art.o"],
        "diag_migrate": ["build/art.o"],
        "bench_migrate": ["build/art.o"], "diag_migrate_b": ["build/art.o"],
        "diag_repair": ["build/art.o"]}
C_SOURCES = {"bench_e25": ["third_party/vendor/libart/src/art.c"],
             "bench_e26": ["third_party/vendor/libart/src/art.c"],
             "bench_long_art": ["third_party/vendor/libart/src/art.c"],
             "bench_long_hyst": ["third_party/vendor/libart/src/art.c"],
             "diag_hyst": ["third_party/vendor/libart/src/art.c"],
             "san_bench_long_hyst": ["third_party/vendor/libart/src/art.c"],
             "bench_long_o1": ["third_party/vendor/libart/src/art.c"],
             "diag_o1": ["third_party/vendor/libart/src/art.c"],
             "bench_prefix": ["third_party/vendor/libart/src/art.c"],
             "diag_prefix": ["third_party/vendor/libart/src/art.c"],
             "diag_compose": ["third_party/vendor/libart/src/art.c"],
             "bench_tag": ["third_party/vendor/libart/src/art.c"],
             "diag_tag": ["third_party/vendor/libart/src/art.c"],
             "diag_migrate": ["third_party/vendor/libart/src/art.c"],
             "bench_migrate": ["third_party/vendor/libart/src/art.c"],
             "diag_migrate_b": ["third_party/vendor/libart/src/art.c"],
             "diag_repair": ["third_party/vendor/libart/src/art.c"]}
# diag_long is the SAME source as bench_long, built with the counters in. One source,
# two targets, so the two builds cannot drift apart.
SRC = {"diag_long": "bench_long.cpp", "bench_w_t": "bench_w.cpp",
       "diag_hyst": "bench_long_hyst.cpp", "diag_o1": "bench_long_o1.cpp",
       "bench_prefix": "bench_prefix.cpp", "diag_prefix": "bench_prefix.cpp",
       "diag_compose": "diag_compose.cpp", "diag_tag": "bench_tag.cpp",
       "diag_migrate": "diag_migrate.cpp", "diag_migrate_b": "bench_migrate.cpp",
       "diag_repair": "diag_repair.cpp",
       # ITEM B: the sanitizer targets are new NAMES over these same sources. One
       # source, two targets -- as diag_long is for bench_long -- so the sanitizer
       # build and the shipped build of the same program cannot drift apart.
       "san_check_e31": "check_e31.cpp",
       "san_check_e9_multiblock": "check_e9_multiblock.cpp",
       "san_check_e11_locate": "check_e11_locate.cpp",
       "san_check_freeze": "check_freeze.cpp",
       "san_bench_long_hyst": "bench_long_hyst.cpp"}

DIR = os.path.dirname(os.path.abspath(__file__))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compile_c(target):
    """Compile the C dependencies of a target into build/<name>.o."""
    objs = []
    for rel in C_SOURCES.get(target, []):
        obj = os.path.join(DIR, "build", os.path.splitext(os.path.basename(rel))[0] + ".o")
        cmd = CC + ["-c", os.path.join(DIR, rel), "-o", obj]
        r = subprocess.run(cmd, cwd=DIR, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"C BUILD FAILED: {rel}\n{r.stderr[-2000:]}", file=sys.stderr)
            sys.exit(1)
        objs.append(obj)
    return objs


def build(target):
    src = os.path.join(DIR, SRC.get(target, target + ".cpp"))
    exe = os.path.join(DIR, "build", target + ".exe")
    tmp = os.path.join(DIR, "build", f"{target}.{os.getpid()}.{time.time_ns()}.tmp.exe")

    objs = compile_c(target)
    link = [os.path.join(DIR, p) for p in LIBS.get(target, [])]

    # ITEM B: a sanitizer target takes CXX_SAN instead of CXX (no -static; see the
    # constant at the top). This is the ONLY change to this function. Everything
    # else on this line and around it is unchanged: the unique temporary output, the
    # atomic rename on success, the source hashes, the executable hash, and the
    # removal of the stale artifact after a failed compile.
    cxx = CXX_SAN if target in SAN_TARGETS else CXX
    cmd = cxx + EXTRA.get(target, []) + [src] + link + ["-o", tmp]
    rec = {"target": target, "build_id": f"{time.time_ns()}", "command": " ".join(cmd), "sources": {}}
    rec["sources"][os.path.basename(src)] = sha256(src)
    for f in sorted(os.listdir(DIR)):
        if f.endswith(".hpp"):
            rec["sources"][f] = sha256(os.path.join(DIR, f))
    for rel in C_SOURCES.get(target, []):
        rec["sources"][rel] = sha256(os.path.join(DIR, rel))
    rec["extra_flags"] = EXTRA.get(target, [])
    rec["c_objects"] = [os.path.relpath(o, DIR).replace(os.sep, "/") for o in objs]

    r = subprocess.run(cmd, cwd=DIR, capture_output=True, text=True)
    rec["returncode"] = r.returncode
    rec["stderr_tail"] = r.stderr[-4000:]

    if r.returncode != 0 or not os.path.exists(tmp):
        rec["status"] = "COMPILE_FAILED"
        rec["exe_sha256"] = None
        with open(os.path.join(DIR, "logs", f"build_{target}.json"), "w", encoding="utf-8") as f:
            json.dump(rec, f, ensure_ascii=False, indent=1)
        if os.path.exists(tmp):
            os.remove(tmp)
        # A failed compile must not leave the PREVIOUS artifact runnable. `run` reads
        # build/<target>.built.json, and that file used to survive a failure -- so a
        # gate whose source no longer compiled still executed its last good binary and
        # reported a pass. That happened once in this round. Remove both, so a failure
        # is a hard stop rather than a silent fallback to a stale build.
        for stale in (os.path.join(DIR, "build", f"{target}.built.json"),
                      os.path.join(DIR, "build", target + ".exe")):
            if os.path.exists(stale):
                os.remove(stale)
        print(f"BUILD FAILED: {target} — no runnable artifact produced "
              f"(stale build record and executable removed)", file=sys.stderr)
        sys.exit(1)

    os.replace(tmp, exe)          # atomic: the artifact exists only on success
    rec["exe_sha256"] = sha256(exe)
    rec["status"] = "OK"
    with open(os.path.join(DIR, "logs", f"build_{target}.json"), "w", encoding="utf-8") as f:
        json.dump(rec, f, ensure_ascii=False, indent=1)
    with open(os.path.join(DIR, "build", f"{target}.built.json"), "w", encoding="utf-8") as f:
        json.dump(rec, f, ensure_ascii=False, indent=1)
    print(f"OK {target} build_id={rec['build_id']} exe={rec['exe_sha256'][:16]}…")


def load_record(target):
    """The build record, or a refusal. Shared by every run entry."""
    built = os.path.join(DIR, "build", f"{target}.built.json")
    if not os.path.exists(built):
        print(f"RUN REFUSED: no successful build record for {target} (run tools_build.py first)", file=sys.stderr)
        sys.exit(2)

    rec = json.load(open(built, encoding="utf-8"))
    exe = os.path.join(DIR, "build", target + ".exe")

    if rec.get("status") != "OK" or not os.path.exists(exe) or sha256(exe) != rec.get("exe_sha256"):
        print(f"RUN REFUSED: executable hash does not match build record for {target} "
              f"(stale or tampered binary)", file=sys.stderr)
        sys.exit(2)

    return rec, exe


def source_drift(rec):
    """Every source in the record whose current content differs.

    Returns a list of (name, status) where status is 'changed' or 'missing'. An empty
    list means the binary in build/ was produced from the sources now on disk.
    """
    drifted = []

    for name, recorded in (rec.get("sources") or {}).items():
        full = os.path.join(DIR, name.replace("/", os.sep))

        if not os.path.exists(full):
            full = os.path.join(DIR, os.path.basename(name))

        if not os.path.exists(full):
            drifted.append((name, "missing"))
        elif sha256(full) != recorded:
            drifted.append((name, "changed"))

    return drifted


def report_drift(target, rec, drifted, pad):
    """One block, used by the strict refusal and the recorded-run banner."""
    print("%sbuild_id %s was made from %d source(s); %d differ from the ones on disk now:"
          % (pad, rec.get("build_id"), len(rec.get("sources") or {}), len(drifted)), file=sys.stderr)

    for name, status in drifted[:20]:
        print("%s  %-10s %s" % (pad, status, name), file=sys.stderr)

    if len(drifted) > 20:
        print("%s  ... and %d more" % (pad, len(drifted) - 20), file=sys.stderr)


def run_current(target, args):
    """The default entry: the binary must match BOTH its executable hash and its sources."""
    rec, exe = load_record(target)
    drifted = source_drift(rec)

    if drifted:
        print(f"RUN REFUSED: {target} was built from sources that have since changed "
              f"(refusing to run a binary that does not match the tree)", file=sys.stderr)
        report_drift(target, rec, drifted, "  ")
        print("  Rebuild the target (python3 tools_build.py %s), or use run_recorded to "
              "re-run the recorded binary deliberately." % target, file=sys.stderr)
        sys.exit(2)

    sys.exit(subprocess.run([exe] + args, cwd=DIR).returncode)


def run_recorded(target, args):
    """Run the recorded binary even if the sources moved on, and say so."""
    rec, exe = load_record(target)
    drifted = source_drift(rec)

    print("RUN RECORDED: %s, build_id %s" % (target, rec.get("build_id")), file=sys.stderr)

    if drifted:
        print("  WARNING: the sources have changed since this binary was built, so these "
              "results DESCRIBE THE RECORDED BUILD, NOT THE CURRENT SOURCE.", file=sys.stderr)
        report_drift(target, rec, drifted, "  ")
    else:
        print("  (sources are unchanged: this binary does match the current tree)", file=sys.stderr)

    sys.exit(subprocess.run([exe] + args, cwd=DIR).returncode)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    if sys.argv[1] == "run_current":
        run_current(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == "run_recorded":
        run_recorded(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == "run":
        # `run` is the strict entry. Fail-closed by default: the E34 accident (edit a
        # source, forget to rebuild, silently run the old binary) must not be reachable
        # by omission. Historical re-runs go through run_recorded, which says what it is.
        run_current(sys.argv[2], sys.argv[3:])
    else:
        for t in sys.argv[1:]:
            build(t)

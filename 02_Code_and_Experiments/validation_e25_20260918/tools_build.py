#!/usr/bin/env python3
"""E25 build/run closure (extended from the E8..E24 one).

Carries over the two guarantees the earlier rounds relied on, and adds what this
round needs:

build:  compile to a UNIQUE temporary output; only on success atomically rename to
        build/<target>.exe and write build/<target>.built.json with source hashes,
        the compile command, the executable hash and a build id. A failed compile
        leaves no runnable artifact behind.
run:    python tools_build.py run <target> [args...]  — refuses to run unless the
        build record exists and the executable hash matches it.

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
    # The hysteresis counters are read by bench_long_hyst's own non-vacuous gate,
    # so the diagnostic build of the SAME source is a second target (as diag_long is
    # for bench_long). The clean timing build stays counter-free.
    "diag_hyst": ["-DE25_DIAG"],
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
        "diag_hyst": ["build/art.o"], "san_bench_long_hyst": ["build/art.o"]}
C_SOURCES = {"bench_e25": ["third_party/vendor/libart/src/art.c"],
             "bench_e26": ["third_party/vendor/libart/src/art.c"],
             "bench_long_art": ["third_party/vendor/libart/src/art.c"],
             "bench_long_hyst": ["third_party/vendor/libart/src/art.c"],
             "diag_hyst": ["third_party/vendor/libart/src/art.c"],
             "san_bench_long_hyst": ["third_party/vendor/libart/src/art.c"]}
# diag_long is the SAME source as bench_long, built with the counters in. One source,
# two targets, so the two builds cannot drift apart.
SRC = {"diag_long": "bench_long.cpp", "bench_w_t": "bench_w.cpp",
       "diag_hyst": "bench_long_hyst.cpp",
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


def run(target, args):
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
    sys.exit(subprocess.run([exe] + args, cwd=DIR).returncode)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    if sys.argv[1] == "run":
        run(sys.argv[2], sys.argv[3:])
    else:
        for t in sys.argv[1:]:
            build(t)

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
    "bench_w_t": ["-DE30_TIME"],           # same source as bench_w, clock ON
    # Needs BOTH: e30_probe is declared inside the E25_DIAG member block because it
    # calls diag_group(), so -DE30_TIME alone leaves it uncompiled.
    "bench_repack": ["-DE30_TIME", "-DE25_DIAG"],
}
LIBS = {"bench_e25": ["build/art.o"], "bench_e26": ["build/art.o"],
        "bench_long_art": ["build/art.o"]}
C_SOURCES = {"bench_e25": ["third_party/vendor/libart/src/art.c"],
             "bench_e26": ["third_party/vendor/libart/src/art.c"],
             "bench_long_art": ["third_party/vendor/libart/src/art.c"]}
# diag_long is the SAME source as bench_long, built with the counters in. One source,
# two targets, so the two builds cannot drift apart.
SRC = {"diag_long": "bench_long.cpp", "bench_w_t": "bench_w.cpp"}

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

    cmd = CXX + EXTRA.get(target, []) + [src] + link + ["-o", tmp]
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

#!/usr/bin/env python3
"""E19 build/run closure (extended from the E8..E18 one).

Carries over the two guarantees the earlier rounds relied on, and adds what this
round needs:

build:  compile to a UNIQUE temporary output; only on success atomically rename to
        build/<target>.exe and write build/<target>.built.json with source hashes,
        the compile command, the executable hash and a build id. A failed compile
        leaves no runnable artifact behind.
run:    python tools_build.py run <target> [args...]  — refuses to run unless the
        build record exists and the executable hash matches it.

E19 additions:
  * bench_e19 (clean, headline) and bench_e19_diag (instrumented) are the SAME
    SOURCE built two ways. The diagnostic build gets -DE18_COUNT (group-prediction
    counters, reused from E18) and -DE19_MEM (allocator accounting), plus the wrap
    flags it needs. No headline number comes from it.
  * the gates carry over unchanged and are built with the same flags.
"""
import hashlib, json, os, subprocess, sys, time

CXX = ["g++", "-std=c++17", "-O3", "-march=x86-64-v2", "-static"]
CC = ["gcc", "-std=c99", "-O3", "-march=x86-64-v2"]

# Per-target flags.
WRAP_ALL = ["-Wl,--wrap=malloc", "-Wl,--wrap=calloc", "-Wl,--wrap=realloc",
            "-Wl,--wrap=aligned_alloc", "-Wl,--wrap=posix_memalign", "-Wl,--wrap=free"]
EXTRA = {
    "bench_e19_diag": WRAP_ALL + ["-DE18_COUNT", "-DE19_MEM"],
    "check_e11_locate": ["-DE12_COUNT"],   # this gate reads candidate_blocks
    "check_e19_reuse": ["-DE18_COUNT"],    # this gate checks the prediction count
}
LIBS = {}
C_SOURCES = {}

# bench_e19_diag is the same translation unit as bench_e19, built with extra
# defines. Recording the shared source under its own name keeps both build records
# pointing at a file that exists.
SRC = {"bench_e19_diag": "bench_e19.cpp"}

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
        print(f"BUILD FAILED: {target} — no runnable artifact produced", file=sys.stderr)
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

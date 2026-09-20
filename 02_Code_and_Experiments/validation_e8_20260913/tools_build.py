#!/usr/bin/env python3
"""E8 build/run closure (E8-10, review 2026-09-13).

build:  compile to a UNIQUE temporary output; only on success atomically rename
        to build/<target>.exe and write build/<target>.built.json containing the
        source hashes, compile command, executable hash and build id. A failed
        compile leaves no runnable artifact behind.
run:    python tools_build.py run <target> [args...]  — refuses to run unless
        build/<target>.built.json exists and the executable hash matches it.
        This makes running a stale binary impossible."""
import hashlib, json, os, subprocess, sys, time

CXX = ["g++", "-std=c++17", "-O3", "-march=x86-64-v2", "-static"]
DIR = os.path.dirname(os.path.abspath(__file__))

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def build(target):
    src = os.path.join(DIR, target + ".cpp")
    exe = os.path.join(DIR, "build", target + ".exe")
    tmp = os.path.join(DIR, "build", f"{target}.{os.getpid()}.{time.time_ns()}.tmp.exe")
    cmd = CXX + [src, "-o", tmp]
    rec = {"target": target, "build_id": f"{time.time_ns()}", "command": " ".join(cmd), "sources": {}}
    rec["sources"][target + ".cpp"] = sha256(src)
    for f in sorted(os.listdir(DIR)):
        if f.endswith(".hpp"):
            rec["sources"][f] = sha256(os.path.join(DIR, f))
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
    os.replace(tmp, exe)                      # atomic: artifact exists only on success
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

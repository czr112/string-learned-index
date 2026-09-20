#!/usr/bin/env python3
"""E6/E7 build tool (E6-10): every official build records the source list with
SHA-256, the exact compile command, and the executable hash; a build that does
not compile successfully is never left looking like a usable binary.
Usage: python tools_build.py <target> [more targets...]   (e.g. bench_e6 check)
Writes logs/build_<target>.json and refuses to continue on failure."""
import hashlib, json, os, subprocess, sys

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
    cmd = CXX + [src, "-o", exe]
    rec = {"target": target, "command": " ".join(cmd), "sources": {}}
    for extra in [target + ".cpp"]:
        rec["sources"][extra] = sha256(os.path.join(DIR, extra))
    # record every local header reachable via the include chain of the target dir
    for f in sorted(os.listdir(DIR)):
        if f.endswith(".hpp"):
            rec["sources"][f] = sha256(os.path.join(DIR, f))
    os.makedirs(os.path.join(DIR, "build"), exist_ok=True)
    r = subprocess.run(cmd, cwd=DIR, capture_output=True, text=True)
    rec["returncode"] = r.returncode
    rec["stderr_tail"] = r.stderr[-4000:]
    if r.returncode != 0 or not os.path.exists(exe):
        rec["exe_sha256"] = None
        rec["status"] = "COMPILE_FAILED"
        with open(os.path.join(DIR, "logs", f"build_{target}.json"), "w", encoding="utf-8") as f:
            json.dump(rec, f, ensure_ascii=False, indent=1)
        print(f"BUILD FAILED: {target} (see logs/build_{target}.json)", file=sys.stderr)
        sys.exit(1)
    rec["exe_sha256"] = sha256(exe)
    rec["status"] = "OK"
    with open(os.path.join(DIR, "logs", f"build_{target}.json"), "w", encoding="utf-8") as f:
        json.dump(rec, f, ensure_ascii=False, indent=1)
    print(f"OK {target}: exe {rec['exe_sha256'][:16]}…")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    for t in sys.argv[1:]:
        build(t)

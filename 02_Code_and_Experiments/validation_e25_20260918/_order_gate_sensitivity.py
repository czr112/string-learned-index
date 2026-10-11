# -*- coding: utf-8 -*-
"""Sensitivity check: compile the new order gate against a SOURCE WITH THE SIGNED COMPARISON
restored, run it, and put the fix back. The restore happens in a finally block, and the file's
hash is verified before and after, so this cannot leave the tree in the reverted state."""
import hashlib, io, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "model", "equal_depth_dir.hpp")
GATE = os.path.join(HERE, "check_depth_dir_order.cpp")
EXE = os.path.join(HERE, "scratch_blockaudit", "order_gate_signed_control.exe")

fixed = io.open(MODEL, encoding="utf-8", newline="").read()
signed_needle = ("            const bool sep_le_q =\n"
                 "                (i == n) ? (sep.size() <= q.size())\n"
                 "                         : (static_cast<unsigned char>(sep[i]) <\n"
                 "                            static_cast<unsigned char>(q[i]));\n")
signed_form = ("            const bool sep_le_q =\n"
               "                (i == n) ? (sep.size() <= q.size()) : (sep[i] < q[i]);\n")
if fixed.count(signed_needle) != 1:
    raise SystemExit("FATAL: the fixed comparison is not in the model")
broken = fixed.replace(signed_needle, signed_form)
sha_fixed = hashlib.sha256(fixed.encode()).hexdigest()

try:
    io.open(MODEL, "w", encoding="utf-8", newline="\n").write(broken)
    rc = subprocess.run(["g++", "-std=c++17", "-O2", "-march=x86-64-v2", GATE, "-o", EXE],
                        cwd=HERE).returncode
    print("signed-control compile rc=%d" % rc)
    if rc != 0:
        raise SystemExit("compile failed")
    run = subprocess.run([EXE, "mixed", "4096", "x"], cwd=HERE, capture_output=True, text=True)
    tail = [l for l in run.stdout.strip().splitlines() if l.startswith("FAIL") or l.startswith("checks=")]
    print("signed-control run rc=%d" % run.returncode)
    for l in tail[:8]:
        print("   " + l)
    print("   ... failures total: %d" % len([l for l in run.stdout.splitlines() if l.startswith("FAIL")]))
finally:
    io.open(MODEL, "w", encoding="utf-8", newline="\n").write(fixed)
    now = hashlib.sha256(io.open(MODEL, encoding="utf-8", newline="").read().encode()).hexdigest()
    print("fix restored, sha256 matches:", now == sha_fixed)
    if now != sha_fixed:
        sys.exit("FATAL: the model file was not restored")

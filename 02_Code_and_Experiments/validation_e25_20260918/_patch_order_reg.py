# -*- coding: utf-8 -*-
"""Register the byte-order semantics gate and its sanitizer twin."""
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(HERE, "tools_build.py")
t = io.open(p, encoding="utf-8", newline="").read()

def sub(old, new, what):
    global t
    if t.count(old) != 1:
        raise SystemExit("FATAL %s: found %d" % (what, t.count(old)))
    t = t.replace(old, new)

sub("    \"san_check_depth_dir_trunc\",\n",
    "    \"san_check_depth_dir_trunc\",\n"
    "    # Byte-order gate (2026-10-11): it fits directories over NUL / 0x7f / 0x80 / 0xff keys\n"
    "    # and drives a full index through scans and churn, so it runs under the sanitizer like\n"
    "    # its siblings.\n"
    "    \"san_check_depth_dir_order\",\n",
    "SAN_TARGETS")

sub("       \"check_depth_dir_trunc\": \"check_depth_dir_trunc.cpp\",\n"
    "       \"san_check_depth_dir_trunc\": \"check_depth_dir_trunc.cpp\",\n",
    "       \"check_depth_dir_trunc\": \"check_depth_dir_trunc.cpp\",\n"
    "       \"san_check_depth_dir_trunc\": \"check_depth_dir_trunc.cpp\",\n"
    "       # Byte-order gate: one source, two targets -- the gate and its sanitizer twin. It needs\n"
    "       # no diagnostic counters: every count it uses comes from its own reference.\n"
    "       \"check_depth_dir_order\": \"check_depth_dir_order.cpp\",\n"
    "       \"san_check_depth_dir_order\": \"check_depth_dir_order.cpp\",\n",
    "SRC")

io.open(p, "w", encoding="utf-8", newline="\n").write(t)
print("patched", p)

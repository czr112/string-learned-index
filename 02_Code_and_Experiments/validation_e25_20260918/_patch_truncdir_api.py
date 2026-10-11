# -*- coding: utf-8 -*-
"""Small follow-up: expose the directory's stored form to gates (read-only)."""
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(HERE, "index.hpp")
t = io.open(p, encoding="utf-8", newline="").read()
old = ("    std::size_t depth_dir_thresholds() const { return depth_dir_.threshold_count(); }\n")
new = ("    std::size_t depth_dir_thresholds() const { return depth_dir_.threshold_count(); }\n"
       "    // TRUNCDIR: which stored form this region's directory uses, and how many arena bytes\n"
       "    // that form cost. Read-only, like the neighbours above: a gate must be able to assert\n"
       "    // that the LIVE regions are the truncated form instead of inferring it from the config.\n"
       "    bool depth_dir_truncated() const { return depth_dir_.truncated(); }\n"
       "    std::size_t depth_dir_separator_bytes() const { return depth_dir_.separator_bytes(); }\n")
if t.count(old) != 1:
    raise SystemExit("FATAL: accessor anchor not found exactly once")
t = t.replace(old, new)
io.open(p, "w", encoding="utf-8", newline="\n").write(t)
print("patched", p)

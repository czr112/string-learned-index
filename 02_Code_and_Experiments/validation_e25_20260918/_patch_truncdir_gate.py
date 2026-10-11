# -*- coding: utf-8 -*-
"""Tighten the new gate: stable probe ownership, stronger negative controls, no
non-guaranteed corpus assertion; and fix one comment in the model header."""
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))

def edit(path, pairs):
    t = io.open(path, encoding="utf-8", newline="").read()
    for old, new, what in pairs:
        if t.count(old) != 1:
            raise SystemExit("FATAL %s: %s not found exactly once" % (path, what))
        t = t.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="\n").write(t)
    print("patched", path)

g = os.path.join(HERE, "check_depth_dir_trunc.cpp")
edit(g, [
("    ProbeSet ps;\n    const std::size_t n = sorted.size();\n    if (!n) return ps;\n",
 "    ProbeSet ps;\n"
 "    // The mutated probes are owned here, and `owned` is RESERVED before the loop so the views\n"
 "    // taken from its elements cannot be invalidated by a reallocation later in the same loop.\n"
 "    ps.owned.reserve(8);\n"
 "    const std::size_t n = sorted.size();\n    if (!n) return ps;\n",
 "probe ownership"),
("        const Violations v1 = check_separator_invariants(sorted, G, prefix_len, broken);\n"
 "        require(v1.total() > 0, where + \": shortened threshold is caught by the checker\");\n",
 "        const Violations v1 = check_separator_invariants(sorted, G, prefix_len, broken);\n"
 "        require(v1.total() > 0, where + \": shortened threshold is caught by the checker\");\n"
 "        require(v1.left > 0, where + \": ... and caught by the rank-1 check specifically\");\n",
 "control-a strength"),
("        const Violations v2 = check_separator_invariants(sorted, G, prefix_len, swapped);\n"
 "        require(v2.total() > 0, where + \": swapped thresholds are caught by the checker\");\n",
 "        const Violations v2 = check_separator_invariants(sorted, G, prefix_len, swapped);\n"
 "        require(v2.total() > 0, where + \": swapped thresholds are caught by the checker\");\n"
 "        require(v2.order > 0, where + \": ... and caught by the monotonicity check specifically\");\n",
 "control-b strength"),
("    require(bracket_violations == 0, \"no range-invariant violation on any probe\");\n"
 "    require(fallbacks > 0, \"the corpus exercises the unshortenable fallback\");\n",
 "    require(bracket_violations == 0, \"no range-invariant violation on any probe\");\n"
 "    // `fallbacks` is REPORTED, not asserted: corpus keys are distinct, so whether a\n"
 "    // threshold has to fall back depends on the corpus. The duplicate-key suite above is\n"
 "    // where the fallback branch is guaranteed to run and is asserted there.\n",
 "corpus fallback assert"),
])

m = os.path.join(HERE, "model", "equal_depth_dir.hpp")
edit(m, [
("        // no shorter prefix can win, so s_j is kept as-is and the case is counted rather than\n"
 "        // special-cased away.\n",
 "        // no shorter prefix can win, so s_j is kept as-is; such cases are reported by the\n"
 "        // gate's own re-derivation rather than hidden here.\n",
 "model comment"),
])

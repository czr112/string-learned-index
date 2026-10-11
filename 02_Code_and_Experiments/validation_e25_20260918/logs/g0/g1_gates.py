# -*- coding: utf-8 -*-
"""G0/G1 evidence: the eight inherited gates, E24's recorded output vs the CURRENT source.

RESULTS_FREEZE.md section 4 claims "8/11 byte-identical to E24; 3 differ, for a stated
reason". Those claims were made by comparing two binaries built on 2026-09-20. Both of those
records have since drifted (index.hpp changed again at 2026-09-22 12:51), so the claim itself
had no evidence tied to the source now on disk. This re-establishes it: same gates, rebuilt
from the current tree, compared field by field against E24's own run logs.
"""

import io
import json
import os

ROOT = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments"
E24 = os.path.join(ROOT, "validation_e24_20260918")
CUR = os.path.join(ROOT, "validation_e25_20260918")

GATES = ["check", "check_e8_hetero", "check_e15_plan", "check_e16_probe",
         "check_e19_reuse", "check_e20_r2", "check_e9_multiblock", "check_e11_locate"]


def read_json_line(path):
    if not os.path.exists(path):
        return None
    with io.open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("{"):
                try:
                    return json.loads(line)
                except Exception:
                    return None
    return None


def main():
    out = []
    w = out.append

    w("G0/G1 evidence: the eight inherited gates -- E24's recorded run vs the CURRENT source")
    w("current runs: logs/g1_rerun/<gate>.out, rebuilt 2026-09-23 from the tree on disk")
    w("=" * 96)

    for g in GATES:
        old = read_json_line(os.path.join(E24, "logs", g + "_run.log"))
        new = read_json_line(os.path.join(CUR, "logs", "g1_rerun", g + ".out"))

        w("")
        w("### " + g)

        if old is None or new is None:
            w("  MISSING: e24=%s current=%s" % (old is not None, new is not None))
            continue

        keys = sorted(set(old) | set(new))
        diffs = []

        for k in keys:
            a, b = old.get(k, "<absent>"), new.get(k, "<absent>")
            if a != b:
                diffs.append((k, a, b))

        if not diffs:
            w("  IDENTICAL on all %d fields (status=%s)" % (len(keys), new.get("status")))
        else:
            w("  %d field(s) differ:" % len(diffs))
            for k, a, b in diffs:
                w("    %-28s E24=%-12s current=%s" % (k, a, b))

            same = len(keys) - len(diffs)
            w("  (%d of %d fields identical; status E24=%s current=%s)"
              % (same, len(keys), old.get("status"), new.get("status")))

    text = "\n".join(out) + "\n"
    dest = os.path.join(CUR, "results", "main", "g0_inherited_gates_rerun.txt")
    with io.open(dest, "w", encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

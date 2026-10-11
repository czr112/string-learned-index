# -*- coding: utf-8 -*-
"""G1 evidence: the switch-specific gates, historical log vs the CURRENT source, field by field.

The invariant these gates assert is about CONTENT (membership, key order, values, and that
the arms agree with a reference table). Internal event counts MAY legitimately move when the
shipped configuration changes -- the project's rule is that such a move must be explainable,
not that it must not happen. So this reports every differing field rather than a pass/fail,
and distinguishes the two kinds of difference:

  * `*_memory` / `*_blocks`: the byte accounting. E38 wired a second model backend into
    Region/Index, so a small constant offset is expected and is NOT a regression.
  * everything else: a structural or event count. A difference here needs a reason.
"""

import io
import json
import os

ROUND = (r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments"
         r"\validation_e25_20260918")
LOGS = os.path.join(ROUND, "logs")
NEW = os.path.join(LOGS, "g1_rerun")

CASES = [
    ("check_e25_write", "mixed", "check_e25_write_mixed_32768_run.log", "check_e25_write_mixed.out"),
    ("check_e25_write", "skew", "check_e25_write_skew_32768_run.log", "check_e25_write_skew.out"),
    ("check_e25_write", "tranco", "check_e25_write_tranco_32768_run.log", "check_e25_write_tranco.out"),
    ("check_c3", "mixed", "check_c3_mixed.log", "check_c3_mixed_same.out"),
    ("check_c3", "skew", "check_c3_skew.log", "check_c3_skew_same.out"),
    ("check_c3", "tranco", "check_c3_tranco.log", "check_c3_tranco_same.out"),
    ("check_e31", "mixed", "check_e31_mixed_same.log", "check_e31_mixed_same.out"),
    ("check_e31", "skew", "check_e31_skew_same.log", "check_e31_skew_same.out"),
    ("check_e31", "tranco", "check_e31_tranco_same.log", "check_e31_tranco_same.out"),
]

# No historical run log survives for these; only the current run can be reported.
NO_HISTORY = ["check_e26_c2", "check_scan", "check_migrate"]

MONEY = ("memory", "blocks", "bytes")


def records(path):
    """Every {...} line in the file, parsed."""
    if not os.path.exists(path):
        return []
    out = []
    with io.open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("{") and line.endswith("}"):
                try:
                    out.append(json.loads(line))
                except Exception:
                    pass
    return out


def last(path):
    r = records(path)
    return r[-1] if r else None


def main():
    out = []
    w = out.append

    w("G1 evidence: switch-specific gates, historical log vs CURRENT source (field by field)")
    w("current runs: logs/g1_rerun/, built 2026-09-23 from the tree on disk")
    w("=" * 100)

    for gate, kind, hist, new in CASES:
        h = last(os.path.join(LOGS, hist))
        n = last(os.path.join(NEW, new))

        w("")
        w("### %s / %s" % (gate, kind))

        if h is None or n is None:
            w("  MISSING: historical=%s current=%s" % (h is not None, n is not None))
            continue

        shared = [k for k in h if k in n]
        diffs = [(k, h[k], n[k]) for k in shared if h[k] != n[k]]
        only_h = [k for k in h if k not in n]
        only_n = [k for k in n if k not in h]

        w("  %d shared field(s), %d differ" % (len(shared), len(diffs)))

        for k, a, b in diffs:
            tag = "  [byte accounting, expected]" if any(m in k for m in MONEY) else "  [NEEDS A REASON]"
            w("    %-26s historical=%-12s current=%-12s delta=%s%s"
              % (k, a, b, (b - a) if isinstance(a, int) and isinstance(b, int) else "?", tag))

        if only_h:
            w("    only in historical: %s" % ", ".join(sorted(only_h)))
        if only_n:
            w("    only in current: %s" % ", ".join(sorted(only_n)[:12])
              + (" ..." if len(only_n) > 12 else ""))

    w("")
    w("### gates with no historical run log on disk (current run only)")
    w("")
    for gate in NO_HISTORY:
        for kind in ("mixed", "skew", "tranco"):
            n = last(os.path.join(NEW, "%s_%s.out" % (gate, kind)))
            if n is None:
                w("  %-16s %-8s ABSENT" % (gate, kind))
                continue
            w("  %-16s %-8s status=%s" % (gate, kind, n.get("status") or n.get("result") or "?"))

    text = "\n".join(out) + "\n"
    dest = os.path.join(ROUND, "results", "main", "g1_switch_gates_rerun.txt")
    with io.open(dest, "w", encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

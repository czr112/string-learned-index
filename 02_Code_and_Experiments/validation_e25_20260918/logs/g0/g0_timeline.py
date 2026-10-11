# -*- coding: utf-8 -*-
"""G0 evidence: an evidence timeline.

For each build record, when was it built? Compare against when the shared headers last
changed. A gate that "passed" against a source that has since changed is not a defect by
itself -- but the audit has to know which of its evidence is still tied to the code that is
on disk, because `run_current` will refuse the rest (that refusal is the strict entry doing
its job, not a failure to investigate).

build_id is a filesystem timestamp in nanoseconds, so it decodes to a wall-clock build time.
"""

import datetime
import io
import json
import os

HERE = (r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments"
        r"\validation_e25_20260918")
LOGS = os.path.join(HERE, "logs")


def stamp(ns):
    try:
        return datetime.datetime.utcfromtimestamp(int(ns) / 1e9).strftime("%Y-%m-%d %H:%M:%S")
    except Exception:
        return "?"


def mtime(path):
    if not os.path.exists(path):
        return "?"
    return datetime.datetime.utcfromtimestamp(os.path.getmtime(path)).strftime("%Y-%m-%d %H:%M:%S")


def main():
    out = []
    w = out.append

    headers = ["index.hpp", "switches.hpp", "model/lits_hpt_adapter.hpp",
               "vendor/lits_model_safe/lits_model.hpp", "tools_build.py", "data.hpp"]

    w("G0 evidence timeline (UTC). Build times decoded from each record's build_id.")
    w("=" * 92)
    w("")
    w("### shared headers, last modified on disk")
    w("")
    for h in headers:
        w("  %-42s %s" % (h, mtime(os.path.join(HERE, h))))
    w("")
    w("### build records, newest first")
    w("")
    w("  %-34s %-20s %s" % ("target", "built (UTC)", "sources"))
    w("  " + "-" * 88)

    rows = []
    for name in os.listdir(LOGS):
        if not (name.startswith("build_") and name.endswith(".json")):
            continue
        with io.open(os.path.join(LOGS, name), encoding="utf-8") as f:
            rec = json.load(f)
        rows.append((int(rec.get("build_id", 0)), name[6:-5], rec))

    for _, target, rec in sorted(rows, reverse=True):
        w("  %-34s %-20s %d" % (target, stamp(rec.get("build_id")), len(rec.get("sources") or {})))

    text = "\n".join(out) + "\n"
    dest = os.path.join(HERE, "results", "main", "g0_evidence_timeline.txt")
    with io.open(dest, "w", encoding="utf-8") as f:
        f.write(text)

    print("wrote %s" % dest)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

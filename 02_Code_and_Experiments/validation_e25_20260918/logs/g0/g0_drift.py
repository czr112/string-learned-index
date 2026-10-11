# -*- coding: utf-8 -*-
"""G0 evidence: for every build record, do its recorded source hashes still match disk?

A build record that no longer matches is not a defect -- it is the expected consequence of a
shared header changing. What matters is knowing WHICH records are still live, because
`run_current` refuses to run against a drifted record (its strict entry). A record whose
sources all still match can be re-run today and reproduces its own source identity.

Output is written as UTF-8 for the Read tool (this host's console is GBK).
"""

import hashlib
import io
import json
import os

HERE = (r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments"
        r"\validation_e25_20260918")
LOGS = os.path.join(HERE, "logs")


def disk_hash(name):
    """The record stores bare file names; they all live beside the round's sources."""
    path = os.path.join(HERE, name)
    if not os.path.exists(path):
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    rows = []

    for name in sorted(os.listdir(LOGS)):
        if not (name.startswith("build_") and name.endswith(".json")):
            continue

        target = name[len("build_"):-len(".json")]

        with io.open(os.path.join(LOGS, name), encoding="utf-8") as f:
            rec = json.load(f)

        if rec.get("status") != "OK":
            rows.append((target, "NOT-OK", rec.get("status"), "", 0, 0))
            continue

        src = rec.get("sources") or {}
        match = miss = absent = 0
        drift = []

        for fname, recorded in src.items():
            got = disk_hash(fname)
            if got is None:
                absent += 1
                drift.append(fname + "(MISSING)")
            elif got == recorded:
                match += 1
            else:
                miss += 1
                drift.append(fname)

        # The exe identity: if the recorded binary is gone, the record is not replayable
        # even when every source matches.
        exe = rec.get("exe") or rec.get("exe_hash") or ""
        state = "LIVE" if miss == 0 and absent == 0 else "DRIFT"

        rows.append((target, state, "%d/%d" % (match, len(src)),
                     ",".join(sorted(drift)), miss, len(src)))

    out = []
    w = out.append
    w("G0 evidence: build-record drift against the CURRENT on-disk sources")
    w("LIVE  = every recorded source hash still matches disk")
    w("DRIFT = at least one source changed since the build")
    w("=" * 100)

    live = [r for r in rows if r[1] == "LIVE"]
    drift = [r for r in rows if r[1] == "DRIFT"]
    other = [r for r in rows if r[1] not in ("LIVE", "DRIFT")]

    w("")
    w("total records %d   LIVE %d   DRIFT %d   other %d"
      % (len(rows), len(live), len(drift), len(other)))
    w("")
    w("### LIVE")
    w("")
    for t, s, m, d, _, n in live:
        w("  %-34s %s" % (t, m))
    w("")
    w("### DRIFT")
    w("")
    for t, s, m, d, _, n in drift:
        w("  %-34s %s   changed: %s" % (t, m, d))
    if other:
        w("")
        w("### other")
        w("")
        for t, s, m, d, _, n in other:
            w("  %-34s %s" % (t, s))

    text = "\n".join(out) + "\n"
    dest = os.path.join(HERE, "results", "main", "g0_build_drift.txt")
    with io.open(dest, "w", encoding="utf-8") as f:
        f.write(text)

    print("wrote %s" % dest)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

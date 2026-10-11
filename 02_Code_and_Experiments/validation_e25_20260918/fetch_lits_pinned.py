# -*- coding: utf-8 -*-
"""Fetch the pinned LITS model sources into this round's vendor directory.

E38/P0 established that E4's `vendor/` was a build scratch area and is no longer on disk,
while E4's `sources.json` still records the pinned commit and the pre-patch SHA-256 of every
file. This script restores the subset the LITS-HPT adapter needs, and REFUSES any file whose
bytes do not match the recorded hash -- so the adapter is built against the recorded source
or not at all.

WHY A NEW SCRIPT RATHER THAN E4's fetch_sources.py. That one writes into E4's own tree, and
its manifest covers the whole dependency set including HOT, PMSS and libart. The task book
puts the vendored original under `vendor/lits_model_original/` in the NEW round and excludes
HOT/PMSS from this round's dependency set, so the subset is fetched here and the E4 tree is
left exactly as it was recorded.

WHAT IS NOT PATCHED HERE. The fetched files are the pristine upstream bytes, verified against
the hashes in sources.json. The safety patch is a separate, separately-hashed step, so a
reader can always tell the two apart.
"""

import hashlib
import json
import os
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
E4 = os.path.join(HERE, "..", "validation_e4_20260911", "sources.json")
DEST = os.path.join(HERE, "vendor", "lits_model_original")

# The subset this round takes from LITS, and what each is for. The task book's §2.1 table
# names the first four; the last three are needed to compile the four (constants, the entry
# type, and small helpers) and LICENSE/README carry the provenance.
WANTED = [
    "LITS/lits/lits_model.hpp",     # HPT training, CDF, prediction, early stop
    "LITS/lits/lits_base.hpp",      # character range and base constants
    "LITS/lits/lits_gpkl.hpp",      # string length and common-prefix helpers
    "LITS/lits/lits_node.hpp",      # local coefficients, valid position range
    "LITS/lits/lits_utils.hpp",
    "LITS/lits/lits_entry.hpp",
    "LITS/lits/lits_kv.hpp",
    "README.md",
]

# NOT FETCHED, AND WHY IT MATTERS. E4's manifest records no LICENSE file for the LITS
# subtree, so there is no recorded hash to verify one against and this script will not
# invent a URL. The task book's §12 requires the code licence to be confirmed before any
# extracted source is redistributed, and explicitly forbids treating the paper's document
# licence as the code licence. This round only READS the pinned source and does not
# redistribute it, so the gap is recorded rather than closed here -- but it is a real open
# item, not an oversight.


def main():
    with open(E4, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    by_path = {item["path"]: item for item in manifest["files"]}
    commit = manifest["lits_commit"]

    os.makedirs(DEST, exist_ok=True)

    report = []
    for rel in WANTED:
        key = "vendor/lits/" + rel
        item = by_path.get(key)

        if item is None:
            raise SystemExit("ABORT: %s is not in E4's manifest" % key)

        url = item["source"]
        if commit not in url:
            raise SystemExit("ABORT: %s is not pinned to %s" % (key, commit))

        out = os.path.join(DEST, os.path.basename(rel))

        if os.path.exists(out):
            got = hashlib.sha256(open(out, "rb").read()).hexdigest()
            if got == item["sha256"]:
                report.append((rel, item["sha256"], item["bytes"], "already present"))
                continue

        request = urllib.request.Request(url, headers={"User-Agent": "lits-hpt-adapter-round"})
        with urllib.request.urlopen(request, timeout=60) as r:
            payload = r.read()

        got = hashlib.sha256(payload).hexdigest()
        if len(payload) != item["bytes"] or got != item["sha256"]:
            raise SystemExit(
                "ABORT: %s hash mismatch\n  want %s (%d bytes)\n  got  %s (%d bytes)"
                % (rel, item["sha256"], item["bytes"], got, len(payload))
            )

        with open(out, "wb") as f:
            f.write(payload)

        report.append((rel, got, len(payload), "fetched"))

    print("commit " + commit)
    for rel, sha, nbytes, how in report:
        print("  %-32s %s  %6d B  %s" % (rel, sha[:16], nbytes, how))

    print("verified %d/%d pinned files" % (len(report), len(WANTED)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

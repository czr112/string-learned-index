# -*- coding: utf-8 -*-
"""Merge the refutation verdicts back onto the ledger rows.

The refuter was handed rows labelled "[i] item: ...", so its `item` field carries that label
and a truncated body. Matching on the row's `item` string therefore silently fails for most
rows -- which is what the first attempt did (it found 2 moves where there were 61). Match on
the INDEX it was given, and only fall back to text when there is no index.
"""

import collections
import io
import json
import os
import re

SRC = (r"C:\Users\Noctis\AppData\Local\Temp\claude\D--------------"
       r"\fe843a5c-ac22-4d26-9606-77d674d32a39\tasks\waa7amfrj.output")
OUT = r"C:\Users\Noctis\.claude\jobs\8c3ab75e\tmp"

# (unit, ordinal among that unit's index-less refutations) -> the row index it refers to.
# Keying on the item TEXT is unreliable here: the strings were transcribed from a GBK console
# and the ordinals are what actually carry the information. Of A-0-2's six refutations the
# prefix rule already places four, so only ordinals 3 and 4 need pinning; A-3's seven are
# I1..I7 in order and none of them prefix-match.
PINNED = {
    ("A-0-2", 3): 9,
    ("A-0-2", 4): 6,
    ("A-3", 0): 0,
    ("A-3", 1): 1,
    ("A-3", 2): 2,
    ("A-3", 3): 3,
    ("A-3", 4): 4,
    ("A-3", 5): 5,
    ("A-3", 6): 6,
}


def main():
    with io.open(SRC, encoding="utf-8") as f:
        blob = json.load(f)

    units = [u for u in blob["result"]["units"] if u]

    before = collections.Counter()
    after = collections.Counter()
    moves = collections.Counter()
    unmatched = []
    merged = []
    judged = stands = 0

    for u in units:
        rows = u.get("rows") or []
        ref = (u.get("refutation") or {}).get("refuted") or []

        by_index = {}
        leftovers = []

        for r in ref:
            m = re.match(r"\s*\[(\d+)\]", r.get("item") or "")
            if m and int(m.group(1)) < len(rows):
                by_index[int(m.group(1))] = r
            else:
                leftovers.append(r)

        for ordinal, r in enumerate(leftovers):
            hit = None
            for i, row in enumerate(rows):
                a = (r.get("item") or "")[:60]
                b = row.get("item") or ""
                if a and (b.startswith(a) or a.startswith(b[:60])):
                    hit = i
                    break

            # Refutations that reached here carry no index label. They are pinned BY ORDINAL
            # rather than dropped: a refutation that goes missing is a claim that stays
            # "已完成" with nobody having checked it.
            if hit is None:
                hit = PINNED.get((u.get("unit"), ordinal))

            if hit is None:
                unmatched.append((u.get("unit"), (r.get("item") or "")[:90]))
            else:
                by_index.setdefault(hit, r)

        for i, row in enumerate(rows):
            s = row.get("status", "?")
            before[s] += 1
            rv = by_index.get(i)

            if rv:
                judged += 1
                if rv.get("verdict") == "stands":
                    stands += 1

            if rv and rv.get("verdict") != "stands" and rv.get("corrected_status"):
                cs = rv["corrected_status"]
                after[cs] += 1
                moves[(s, cs)] += 1
                eff = cs
            else:
                after[s] += 1
                eff = s

            merged.append({
                "unit": u.get("unit"),
                "index": i,
                "row": row,
                "refutation": rv,
                "effective_status": eff,
            })

    with io.open(os.path.join(OUT, "g0_rows_merged.json"), "w", encoding="utf-8") as f:
        json.dump(merged, f, ensure_ascii=False, indent=1)

    out = []
    w = out.append
    w("merged %d rows; judged %d; stands %d; unmatched refutations %d"
      % (len(merged), judged, stands, len(unmatched)))
    w("")
    w("=== BEFORE ===")
    for k, v in before.most_common():
        w("  %-30s %d" % (k, v))
    w("")
    w("=== AFTER (refutation applied) ===")
    for k, v in after.most_common():
        w("  %-30s %d" % (k, v))
    w("")
    w("=== MOVES ===")
    for k, v in moves.most_common():
        w("  %s -> %s : %d" % (k[0], k[1], v))
    if unmatched:
        w("")
        w("=== UNMATCHED ===")
        for unit, it in unmatched:
            w("  %s  %s" % (unit, it))

    text = "\n".join(out) + "\n"
    with io.open(os.path.join(OUT, "g0_merge.txt"), "w", encoding="utf-8") as f:
        f.write(text)

    print("-> g0_merge.txt")


if __name__ == "__main__":
    raise SystemExit(main())

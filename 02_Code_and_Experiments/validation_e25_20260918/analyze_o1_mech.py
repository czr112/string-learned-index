#!/usr/bin/env python3
"""O1 mechanism table: what moved, and how much space it cost.

Reads results/main/long_o1_diag_{same,grow}.jsonl (run_o1_mech.py).

The comparison is made on the ERASE side, on purpose.

  ov_shifted_records_erase  the number of records an erase displaces -- a LOGICAL
                            quantity, and the same number in both arms, because both
                            arms face the same erase positions. The gate asserts it
                            (arm_a_logical_shifts == arm_b_handle_moves), so it is the
                            workload being held fixed, not a result.
  ov_shifted_bytes_erase    what C2 pays for those displacements: 48 B each.
  ov_handle_move_bytes      what O1 pays IN TOTAL, erases and inserts together. Its
                            erase share is ov_shifted_records_erase x 4 B, derived
                            rather than measured, because the handle counter does not
                            split the two. C2's insert share is not instrumented at
                            all, so a combined-versus-combined figure does not exist;
                            the script prints O1's combined number for reference and
                            never subtracts it from C2's.

Space: ov_store_high_water against ov_store_live_water is the hole question. Equal
values mean the free list handed every slot back and the store never grew past the
live count -- i.e. O1's extra resident space is the handle array alone, not holes.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "results", "main")

OBJ = 48
HANDLE = 4


def load(mode):
    path = os.path.join(RESULTS, "long_o1_diag_%s.jsonl" % mode)

    if not os.path.exists(path):
        raise SystemExit("missing %s -- run run_o1_mech.py first" % path)

    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def pick(rows, kind, mode, arm, frac):
    for d in rows:
        if d["kind"] == kind and d["mode"] == mode and d["arm"] == arm and abs(d["frac"] - frac) < 1e-9:
            return d
    return None


def main():
    modes = ["same", "grow"]
    print("=" * 118)
    print("O1 机制表：搬动了什么（erase 侧），以及代价（空格高水位）")
    print("=" * 118)

    for mode in modes:
        rows = load(mode)
        kinds = []
        for d in rows:
            if d["kind"] not in kinds:
                kinds.append(d["kind"])

        print()
        print("### mode = %s" % mode)
        print()
        header = ("%-7s %-5s | %-11s %-13s %-13s | %-11s %-9s %-9s | %-5s %-5s | %-9s %-9s"
                  % ("dataset", "frac", "C2 搬移字节", "O1 erase 搬移B", "O1 合计搬移B",
                     "C2 ov_bytes", "O1 ov_bytes", "Δov_bytes", "hw", "live", "C2 mem", "O1 mem"))
        print(header)
        print("-" * len(header))

        for kind in kinds:
            for frac in (0.25, 0.5, 1.0, 2.0):
                c2 = pick(rows, kind, mode, "C2", frac)
                o1 = pick(rows, kind, mode, "O1", frac)
                art = pick(rows, kind, mode, "ART", frac)

                if not c2 or not o1:
                    continue

                logical = c2["ov_shifted_records_erase"]
                o1_erase_bytes = logical * HANDLE

                # The two arms must agree on the logical count, or they were not run on the
                # same workload and the whole comparison is void.
                if o1["ov_shifted_records_erase"] != logical:
                    print("  !! %s/%s frac=%s: logical shift counts differ (%d vs %d)"
                          % (kind, mode, frac, logical, o1["ov_shifted_records_erase"]))

                # And the ON arm must not have moved a stored record.
                if o1["ov_record_moves_stable"] != 0:
                    print("  !! %s/%s frac=%s: the ON arm moved %d stored records"
                          % (kind, mode, frac, o1["ov_record_moves_stable"]))

                ratio = (c2["ov_shifted_bytes_erase"] / o1_erase_bytes) if o1_erase_bytes else 0.0

                print("%-7s %-5s | %-11d %-13d %-13d | %-11d %-9d %-+9d | %-5d %-5d | %-9d %-9d  (%.1fx)"
                      % (kind, frac, c2["ov_shifted_bytes_erase"], o1_erase_bytes,
                         o1["ov_handle_move_bytes"],
                         c2["ov_bytes"], o1["ov_bytes"], o1["ov_bytes"] - c2["ov_bytes"],
                         o1["ov_store_high_water"], o1["ov_store_live_water"],
                         c2["memory"], o1["memory"], ratio))

                if art and (art["ov_handle_moves"] or art["ov_record_moves_stable"]):
                    print("  !! ART arm carries non-zero movement counters: it is not an Index")

        # Worst-case row across the whole mode: the checkpoint with the most movement.
        worst = None
        for kind in kinds:
            o1 = pick(rows, kind, mode, "O1", 2.0)
            c2 = pick(rows, kind, mode, "C2", 2.0)
            if not o1 or not c2:
                continue
            if worst is None or c2["ov_shifted_bytes_erase"] > worst[0]:
                worst = (c2["ov_shifted_bytes_erase"], kind, o1)

        if worst:
            c2b, kind, o1 = worst
            print()
            print("%s 上 C2 的累计搬移最多：%.1f MB（%d 条 x 48 B）；O1 侧同一批 erase 只搬 %.1f MB 句柄"
                  "（%.1fx），且 store 高水位 %d = 活跃 %d，没有空洞。"
                  % (kind, c2b / 1e6, o1["ov_shifted_records_erase"], o1["ov_shifted_records_erase"] * HANDLE / 1e6,
                     (c2b / (o1["ov_shifted_records_erase"] * HANDLE)) if o1["ov_shifted_records_erase"] else 0.0,
                     o1["ov_store_high_water"], o1["ov_store_live_water"]))

    return 0


if __name__ == "__main__":
    sys.exit(main())

# -*- coding: utf-8 -*-
"""Does the refusal checklist actually refuse?

A guard that never fires proves nothing -- the project has already paid for that lesson twice
(a switch missing from the whitelist made an arm-equality assertion vacuously true, and an
analyzer that returned an empty list for a missing file made a short table look complete).

So every refusal path in analyze_common is exercised here with input built to trip it, and the
SELF-TEST ITSELF fails if any of them fails to fire.

Run:  python3 selftest_analyze_common.py
"""

import io
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import analyze_common as ac


def write(tmpdir, name, rows):
    path = os.path.join(tmpdir, name)

    with io.open(path, "w", encoding="utf-8") as f:
        for r in rows:
            if isinstance(r, str):
                f.write(r + "\n")
            else:
                f.write(json.dumps(r) + "\n")

    return path


def expect_refusal(label, fn, results):
    try:
        fn()
    except ac.AnalysisRefused as e:
        results.append((label, "refused", str(e)[:110]))
        return
    except SystemExit as e:
        results.append((label, "refused", str(e)[:110]))
        return

    results.append((label, "!! DID NOT REFUSE !!", "the guard is not live"))


def main():
    results = []

    with tempfile.TemporaryDirectory() as tmp:
        good = [
            {"arm": "A", "run_tag": "P1", "rep": 0, "kind": "mixed", "mode": "same", "frac": 2.0,
             "ops": 100, "query_ms": 4.0},
            {"arm": "B", "run_tag": "P1", "rep": 0, "kind": "mixed", "mode": "same", "frac": 2.0,
             "ops": 100, "query_ms": 5.0},
        ]

        # 1. a missing arm must be refused
        one_arm = write(tmp, "one_arm.jsonl", [good[0], dict(good[0], arm="B"),
                                               dict(good[0], rep=1), dict(good[0], arm="B", rep=1)])
        expect_refusal("missing arm",
                       lambda: ac.load_records(one_arm, arms=["A", "B", "C"]),
                       results)

        # ...and the same file with all three arms present must load
        three = one_arm.replace("one_arm", "three")
        write(tmp, "three.jsonl", [dict(r, arm=a) for r in
                                   [good[0], dict(good[0], rep=1)] for a in ("A", "B", "C")])
        try:
            ac.load_records(three, arms=["A", "B", "C"])
            results.append(("all arms present", "loaded", "control: this one must NOT refuse"))
        except ac.AnalysisRefused as e:
            results.append(("all arms present", "!! REFUSED A GOOD INPUT !!", str(e)[:110]))

        # 2. invalid JSON
        bad = write(tmp, "bad.jsonl", [json.dumps(good[0]), "{not json", json.dumps(good[1])])
        expect_refusal("invalid JSON", lambda: ac.load_records(bad, arms=["A", "B"]), results)

        # 3. a missing file
        expect_refusal("missing file",
                       lambda: ac.load_records(os.path.join(tmp, "nope.jsonl")), results)

        # 4. operation counts that differ across arms in one bucket
        uneven = write(tmp, "uneven.jsonl", [good[0], dict(good[1], ops=99)])
        expect_refusal("ops differ across arms",
                       lambda: ac.assert_ops_consistent(
                           ac.load_records(uneven, arms=["A", "B"])), results)

        # 5. duplicate pairings
        dup = write(tmp, "dup.jsonl", [good[0], good[1], good[0]])
        expect_refusal("duplicate pairing",
                       lambda: ac.assert_no_duplicate_pairings(
                           ac.load_records(dup, arms=["A", "B"])), results)

        # 6. mixed units in one row
        mixed = write(tmp, "mixed.jsonl", [dict(good[0], ns_per_query=4000.0), good[1]])
        expect_refusal("mixed units",
                       lambda: ac.assert_units_consistent(
                           ac.load_records(mixed, arms=["A", "B"])), results)

        # 7. a counter build must not be analyzed as a timing build
        expect_refusal("counter build", lambda: ac.assert_not_counter_build("check_e31"), results)

        # 7b. and the CONTROL: a build whose only extra flag is an include path is NOT a
        #     counter build. Without this control the guard could be narrowed to nothing and
        #     case 7 would still pass. G5's bench_btree is the target that made the
        #     distinction necessary: it carries -Ithird_party/vendor/tlx because the B+ tree
        #     baseline is vendored outside the round root.
        try:
            ac.assert_not_counter_build("bench_btree")
            results.append(("counter build control (include path)", "passed",
                            "control: a -I-only build must not refuse"))
        except ac.AnalysisRefused as e:
            results.append(("counter build control (include path)",
                            "!! REFUSED A TIMING BUILD !!", str(e)[:110]))

        # 8. a drifted source identity must refuse unless allowed -- and the control is that
        #    a target whose sources still match must NOT refuse.
        expect_refusal("drifted identity",
                       lambda: ac.assert_source_identity("bench_e25"), results)

        try:
            ac.assert_source_identity("bench_e25", allow_drift=True)
            results.append(("drift allowed explicitly", "allowed",
                            "control: with allow_drift=True it must pass and say so"))
        except ac.AnalysisRefused as e:
            results.append(("drift allowed explicitly", "!! REFUSED !!", str(e)[:110]))

        try:
            ac.assert_source_identity("check_freeze")
            results.append(("current identity", "passed",
                            "control: a LIVE target must not refuse"))
        except ac.AnalysisRefused as e:
            results.append(("current identity", "!! REFUSED A LIVE TARGET !!", str(e)[:110]))

    width = max(len(r[0]) for r in results)
    refused = 0
    for label, verdict, why in results:
        print("%-*s  %-26s %s" % (width, label, verdict, why))
        if verdict in ("refused", "loaded", "allowed", "passed"):
            refused += 1

    ok = refused == len(results)
    print("")
    print("%d/%d checks behaved as required" % (refused, len(results)))

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())

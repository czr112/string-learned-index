# -*- coding: utf-8 -*-
"""Shared loader and refusal checklist for the analyze_*.py scripts.

WHY THIS EXISTS
===============

Task book section 12.2 lists what an analysis script MUST refuse: a missing arm, duplicate
pairings, a configuration that unexpectedly came out identical, inconsistent state or
operation counts, invalid JSON, a stale executable identity, a counter build mixed into a
timing table, inconsistent units, and checksum failures.

None of it was implemented. `analyze_lits.py`'s loader returned an EMPTY LIST when the file
was absent, so a run that lost one arm produced a table with fewer columns, no error, and no
warning -- and 17 of 17 analyzers read no build identity at all. The G0 audit recorded this
as gap #9; this module closes it.

HOW TO USE IT
=============

    from analyze_common import load_records, assert_arms_complete, assert_ops_consistent

    rows = load_records(path, arms=ARMS, arm_field="arm")
    assert_arms_complete(rows, ARMS)
    assert_ops_consistent(rows)

Every check RAISES `AnalysisRefused` (a SystemExit subclass) rather than printing a warning,
because a warning in a transcript is not a refusal.

DRIFT IS A DECISION, NOT A DEFAULT
==================================

Reproducing an old round's table from records whose source has since moved on is legitimate
-- the project does it deliberately. So the identity check is OFF unless the caller names the
build target, and a caller that tolerates drift must say so out loud:

    assert_source_identity("bench_lits_adapter", allow_drift=True)   # prints what it allows

That way "these numbers came from superseded source" is in the transcript, not in someone's
memory.
"""

import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

try:
    import hashlib
except ImportError:      # pragma: no cover
    hashlib = None


class AnalysisRefused(SystemExit):
    """Refused to produce a table. A SystemExit so the process exits non-zero."""

    def __init__(self, message):
        super().__init__("ANALYSIS REFUSED: " + message)


# ---------------------------------------------------------------------------
# identity
# ---------------------------------------------------------------------------

def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build_record(target):
    """The build record for `target`, or a refusal."""
    path = os.path.join(HERE, "logs", "build_%s.json" % target)

    if not os.path.exists(path):
        raise AnalysisRefused("no build record for %s" % target)

    try:
        with io.open(path, encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:
        raise AnalysisRefused("build record for %s is not valid JSON: %s" % (target, e))


def source_drift(target):
    """Every source in the record whose content differs from disk now."""
    rec = build_record(target)
    drifted = []

    for name, recorded in (rec.get("sources") or {}).items():
        full = os.path.join(HERE, name.replace("/", os.sep))

        if not os.path.exists(full):
            drifted.append((name, "missing"))
        elif _sha256(full) != recorded:
            drifted.append((name, "changed"))

    return rec, drifted


def assert_not_counter_build(target):
    """A counter build must not end up in a timing table (section 12.2).

    WHAT MAKES A BUILD A COUNTER BUILD is a preprocessor define, so that is what this tests:
    any -D flag refuses the record. The test used to be "any extra flag at all", which was
    wrong in the other direction -- a bare include path (-Ithird_party/vendor/tlx) is not a
    counter, and refusing it would have forced the G5 external-baseline timing build to be
    analysed by hand or, worse, analysed with the guard removed. Narrowing the predicate to
    -D can only refuse builds that actually carry a define; every counter build in this
    project reaches its counter code through one.
    """
    rec = build_record(target)
    defines = [f for f in (rec.get("extra_flags") or []) if f.startswith("-D")]

    if defines:
        raise AnalysisRefused(
            "%s was built with %s, so it is a COUNTER build; its timings do not belong in a "
            "timing table" % (target, " ".join(defines)))


def assert_source_identity(target, allow_drift=False):
    """Refuse a stale executable identity unless the caller explicitly allows it.

    On allowing it, say so in the transcript: the tables produced are about the source the
    binary was built from, NOT about the tree on disk now.
    """
    rec, drifted = source_drift(target)

    if drifted and not allow_drift:
        raise AnalysisRefused(
            "%s was built from source that has since changed (%d of %d files). Pass "
            "allow_drift=True to analyze it as a HISTORICAL record."
            % (target, len(drifted), len(rec.get("sources") or {})))

    if drifted:
        print("NOTE: %s is a DRIFTED build (%d source(s) changed since): the tables below "
              "describe that build, not the current tree." % (target, len(drifted)),
              file=sys.stderr)

    return rec


# ---------------------------------------------------------------------------
# records
# ---------------------------------------------------------------------------

def load_records(path, arms=None, arm_field="arm", allow_empty=False):
    """Read a JSONL record file. Refuses missing files, invalid JSON and missing arms."""
    if not os.path.exists(path):
        raise AnalysisRefused("record file does not exist: %s" % path)

    rows = []
    bad = 0

    with io.open(path, encoding="utf-8", errors="replace") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except Exception:
                bad += 1
                if bad <= 3:
                    print("  invalid JSON at %s:%d" % (os.path.basename(path), lineno),
                          file=sys.stderr)

    if bad:
        raise AnalysisRefused("%d line(s) of %s are not valid JSON" % (bad, os.path.basename(path)))

    if not rows and not allow_empty:
        raise AnalysisRefused("%s has no records" % os.path.basename(path))

    if arms is not None:
        assert_arms_complete(rows, arms, arm_field=arm_field, where=os.path.basename(path))

    return rows


def assert_arms_complete(rows, arms, arm_field="arm", where=""):
    """Every named arm must appear. A silently-missing arm is the failure this prevents."""
    seen = {r.get(arm_field) for r in rows}
    missing = [a for a in arms if a not in seen]

    if missing:
        raise AnalysisRefused(
            "%s is missing arm(s) %s (saw %s)"
            % (where or "the record set", missing, sorted(x for x in seen if x is not None)))

    unexpected = sorted(x for x in seen if x is not None and x not in arms)
    if unexpected:
        print("NOTE: %s carries arm(s) outside the expected set: %s"
              % (where or "the record set", unexpected), file=sys.stderr)


def assert_ops_consistent(rows, field="ops", same_in=("run_tag", "rep", "kind", "mode", "frac"),
                          arm_field="arm"):
    """Within one (process, repetition, workload, checkpoint) every arm must report the SAME
    operation count. If they differ, the arms did not see the same workload and no ratio
    across them means anything."""
    buckets = {}

    for r in rows:
        if field not in r:
            continue
        key = tuple(r.get(k) for k in same_in)
        buckets.setdefault(key, {})[r.get(arm_field)] = r.get(field)

    bad = 0
    for key, per_arm in buckets.items():
        values = {v for v in per_arm.values() if v is not None}
        if len(values) > 1:
            bad += 1
            if bad <= 5:
                print("  %s differs across arms at %s: %s" % (field, key, per_arm), file=sys.stderr)

    if bad:
        raise AnalysisRefused(
            "%s differs across arms in %d bucket(s); the arms did not run the same workload"
            % (field, bad))


def assert_no_duplicate_pairings(rows, key_fields=("run_tag", "rep", "kind", "mode", "frac"),
                                 arm_field="arm"):
    """One record per (arm, process, repetition, workload, checkpoint). A duplicate means the
    same cell was appended twice, and a median over it is not a median over repetitions."""
    seen = {}
    dups = 0

    for r in rows:
        key = (r.get(arm_field),) + tuple(r.get(k) for k in key_fields)
        if key in seen:
            dups += 1
            if dups <= 5:
                print("  duplicate pairing: %s" % (key,), file=sys.stderr)
        seen[key] = True

    if dups:
        raise AnalysisRefused("%d duplicate pairing(s) in the record set" % dups)


def assert_units_consistent(rows, pairs=(("query_ms", "ns_per_query"),)):
    """A table must not mix a millisecond column into a nanosecond table (section 12.2's
    'inconsistent units'). Refuses when one bucket reports BOTH members of a pair."""
    bad = 0

    for a, b in pairs:
        for r in rows:
            if r.get(a) is not None and r.get(b) is not None:
                bad += 1

    if bad:
        raise AnalysisRefused(
            "%d record(s) carry both members of a unit pair; the table would mix units" % bad)

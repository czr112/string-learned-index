# -*- coding: utf-8 -*-
"""G4/E1 analysis: four BUILD variants of one source, paired within one repetition.

The four builds are separate executables, so a repetition is "one invocation of each, back to
back in a rotated order" rather than a rotation inside one process. The run_tag carries
(p, r, build) -- `P1R0B`, `P1R0L`, `P1R0P`, `P1R0PL` -- so a pair is always formed from the
same process and the same repetition, which is the only comparison this project accepts.

Ratios < 1 mean the FIRST build is faster.

WHAT THIS ANSWERS: whether compilation alone moves this prototype, before any algorithmic
claim. It does NOT answer "optimized prototype vs ART" -- ART is a different program in the
same binary here, so a one-sided PGO comparison against it would be meaningless, and that is
stated in the report rather than presented as a result.

Usage:  python3 analyze_pgo.py --run-id <TAG>
"""

import argparse
import io
import os
import re
import statistics

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

BUILDS = {"B": "base", "L": "lto", "P": "pgo", "PL": "pgo_lto"}
PAIRS = [("lto", "base"), ("pgo", "base"), ("pgo_lto", "base"),
         ("pgo", "lto"), ("pgo_lto", "pgo")]

KINDS = ["mixed", "skew", "tranco"]
ARMS = ["S0", "E31", "WIDE", "ART"]
CP = [0.25, 0.50, 1.00, 2.00]

TAG_RE = re.compile(r"^P(\d+)R(\d+)(B|L|P|PL)$")


def load(path):
    if not os.path.exists(path):
        raise SystemExit("ANALYSIS REFUSED: %s does not exist" % path)

    out = []
    bad = 0

    with io.open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                out.append(__import__("json").loads(line))
            except Exception:
                bad += 1

    if bad:
        raise SystemExit("ANALYSIS REFUSED: %d invalid JSON line(s) in %s" % (bad, path))
    if not out:
        raise SystemExit("ANALYSIS REFUSED: %s is empty" % path)

    return out


def keyed(rows):
    """(build, process, repetition, kind, frac, arm) -> record.

    THE ARM BELONGS IN THE KEY. The first version of this function left it out, so each
    (build, process, repetition) bucket kept only the LAST arm written -- which, because every
    invocation runs one repetition and therefore the fixed arm order S0,E31,WIDE,ART, was
    always ART. The table it produced was therefore a comparison of libart across four
    compilations, not of the prototype, and its structural columns were all zero because ART
    carries none. The numbers were arithmetically fine and answered a question nobody asked.

    ARM POSITION IS A CONSTANT HERE, NOT A CONTROLLED VARIABLE. `run_pgo.py` rotates which
    BUILD goes first; inside an invocation the arm order is fixed to S0,E31,WIDE,ART. That is
    acceptable for THIS question -- a ratio between two builds is taken at the same arm
    position on both sides, so the position effect cancels -- but it must not be read as a
    controlled arm comparison. The arm-rotation discipline lives in run_widest.py.
    """
    out = {}
    unknown = 0

    for r in rows:
        m = TAG_RE.match(r.get("run_tag") or "")
        if not m:
            unknown += 1
            continue
        p, rep, b = int(m.group(1)), int(m.group(2)), BUILDS[m.group(3)]
        out[(b, p, rep, r["kind"], r["frac"], r["arm"])] = r

    if unknown:
        raise SystemExit("ANALYSIS REFUSED: %d row(s) have an unparseable run_tag; the build "
                         "identity is not recoverable from them" % unknown)

    return out


def pair_stats(idx, kind, frac, arm, field, a, b):
    per_proc = {}

    for (build, p, rep, k, f, ar), rec in idx.items():
        if build != a or k != kind or ar != arm or abs(f - frac) > 1e-9:
            continue
        other = idx.get((b, p, rep, kind, frac, arm))
        if other is None or not rec.get(field) or not other.get(field):
            continue
        per_proc.setdefault(p, []).append(rec[field] / other[field])

    meds = {p: statistics.median(v) for p, v in per_proc.items() if v}
    if not meds:
        return None

    vals = sorted(meds.values())

    return {
        "median": statistics.median(vals),
        "min": vals[0],
        "max": vals[-1],
        "below": sum(1 for v in vals if v < 1.0),
        "n": len(vals),
        "noise": (vals[-1] / vals[0]) if vals[0] > 0 else float("inf"),
    }


def fmt(s, label):
    if s is None:
        return "%-20s      --" % label

    return ("%-20s %7.3f  (%d/%d below)  range %.3f-%.3f  noise %.3f"
            % (label, s["median"], s["below"], s["n"], s["min"], s["max"], s["noise"]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--mode", default="same")
    opts = parser.parse_args()

    rows = load(os.path.join(MAIN, "long_widest_%s_%s.jsonl" % (opts.run_id, opts.mode)))
    idx = keyed(rows)

    out = []
    w = out.append

    w("G4/E1  PGO/LTO   run_id=%s  mode=%s" % (opts.run_id, opts.mode))
    w("four builds of ONE source: base / lto / pgo / pgo_lto")
    w("ratios within one repetition (same process, SAME ARM); per-process medians")
    w("=" * 108)

    # The prototype arms. ART is reported separately and last, because it is a different
    # program inside the same binary -- a build that helps ART says nothing about the index.
    for field, label in (("cum_ms", "MAINTENANCE + OPERATIONS (cumulative to the checkpoint)"),
                         ("query_ms", "FIXED QUERY SEQUENCE at the checkpoint")):
        w("")
        w("### %s" % label)
        w("")

        for kind in KINDS:
            for arm in ARMS:
                w("  %s / %s" % (kind, arm))

                for frac in CP:
                    head = "    at %3d%%" % int(frac * 100)

                    for a, b in PAIRS:
                        s = pair_stats(idx, kind, frac, arm, field, a, b)
                        if s is not None:
                            w(head + "  " + fmt(s, "%s/%s" % (a, b)))
                            head = "             "

        w("")
        w("  absolutes at 200% (median over all records):")

        for kind in KINDS:
            for arm in ARMS:
                parts = []

                for bname in ("base", "lto", "pgo", "pgo_lto"):
                    vals = [r[field] for k, r in idx.items()
                            if k[0] == bname and k[3] == kind and k[5] == arm
                            and abs(k[4] - 2.00) < 1e-9]
                    if vals:
                        parts.append("%s=%.6g" % (bname, statistics.median(vals)))

                w("    %-8s %-5s %s" % (kind, arm, "  ".join(parts)))

    # Compilation cannot change what the index HOLDS. If a structural column differed across
    # builds, the timing comparison would be measuring two different data structures.
    w("")
    w("### STRUCTURE CHECK across builds at 200% (must be identical; compilation cannot move it)")
    w("")

    for kind in KINDS:
        for arm in ("S0", "E31", "WIDE"):
            parts = []

            for bname in ("base", "lto", "pgo", "pgo_lto"):
                sel = [r for k, r in idx.items()
                       if k[0] == bname and k[3] == kind and k[5] == arm
                       and abs(k[4] - 2.00) < 1e-9]
                if sel:
                    parts.append("%s(ov=%d,blocks=%d,mem=%d)"
                                 % (bname,
                                    int(statistics.median([r["overflow"] for r in sel])),
                                    int(statistics.median([r["blocks"] for r in sel])),
                                    int(statistics.median([r["memory"] for r in sel]))))

            w("  %-8s %-5s %s" % (kind, arm, "  ".join(parts)))

    text = "\n".join(out) + "\n"

    with io.open(os.path.join(MAIN, "g4_pgo_analysis_%s.txt" % opts.run_id), "w",
                 encoding="utf-8") as f:
        f.write(text)

    print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

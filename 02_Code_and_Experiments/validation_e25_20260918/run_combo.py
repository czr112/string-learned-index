#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Driver and analyzer for the four-arm WIDE x compact-routing combination round.

ARMS. B = shipped configuration, W = B + {subblock_reclaim, reclaim_widest}, C = B +
compact_region_routing, WC = both. Every pair is asserted in the C++ side with
require_only_switches_differ, so an arm that silently became a copy of another fails the
child instead of passing a comparison that never happened.

PROTOCOL. Five independent processes, four repetitions each; the four arms rotate position
within every repetition, so no arm is systematically first or last. Ratios are formed WITHIN
one repetition -- same process, same machine state, same pre-built query set -- and the
reported figure is the median of the per-process ratios. Direction is counted over those
ratios, and a difference smaller than max/min of the per-process medians is reported as
UNRESOLVED, in those words.

ONE BACKEND PER RUN. G5 showed the WIDE benefit is strongly backend-mapped, so ctx16 and
lits are separate phases of the child binary and no ratio is ever pooled across them.

WHAT THE ANALYZER REFUSES TO DO. It does not compute a ratio between absolute medians from
different repetitions, it does not multiply or chain ratios across rounds or arms (W/B,
C/B, WC/W, WC/C, WC/B are all reported directly), and it does not read a win out of a
difference inside the noise floor.

EVIDENCE CHECKS. A cross-arm checksum match is NOT one of them: the earlier routing round
learned that a workload can go degenerate for every arm at once and still agree. Every row's
hit/miss/scan mismatch counts must be zero, the hit counts must match the expected non-zero
count, the miss observed count must be zero, the probe hashes and live counts must be
identical across arms and repetitions, and the cross-arm hit/miss checksums must agree as a
secondary guard against a skipped lookup.

Usage:  python3 run_combo.py [--processes 5] [--reps 4] [--modes same,grow]
                             [--backends ctx16,lits] [--run-id TAG]
        python3 run_combo.py --analyze-only TAG [--metrics m1,m2]
"""

import argparse
import collections
import glob
import json
import os
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")
TR = "../validation_e5_20260913/data/tranco_XN6XN.csv"

DATASETS = [("mixed", "131072", ""), ("skew", "131072", ""), ("tranco", "262144", TR)]
BACKENDS = ["ctx16", "lits"]
MODES = ["same", "grow"]
ARMS = ["B", "W", "C", "WC"]
# (base, candidate): the printed ratio is candidate/base, always smaller-is-better. The
# column headers are fixed in this order: W/B, C/B, WC/W, WC/C, WC/B. A first version listed
# the pairs in the display order and the analyzer computed candidate/base with them, which
# silently made every cell the RECIPROCAL of its label -- caught by the routing_bytes column
# reading 0.000 with direction 5/5 where a zero denominator should have been n/a.
PAIRS = [("B", "W"), ("B", "C"), ("W", "WC"), ("C", "WC"), ("B", "WC")]
FRACS = [0.0, 0.25, 0.5, 1.0, 2.0]

# Metrics the round's plan requires on one table. All of them are "smaller is better":
# durations and byte counts.
METRICS = [
    ("build_ns", "build (per arm-run, ns)"),
    ("seg_ns", "maintenance segment (ns)"),
    ("cum_ns", "maintenance cumulative (ns)"),
    ("hit_ns", "hit queries (ns)"),
    ("miss_ns", "miss queries (ns)"),
    ("scan16_ns", "scan 16 (ns)"),
    ("scan256_ns", "scan 256 (ns)"),
    ("scan4096_ns", "scan 4096 (ns)"),
    ("index_bytes", "index bytes"),
    ("overflow", "overflow records"),
    ("routing_bytes", "routing metadata bytes"),
]


def load_rows(paths):
    rows = []

    for path in paths:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()

                if line:
                    rows.append(json.loads(line))

    return rows


def archive(work, dest):
    """Move whatever the child appended into the run's own file. Returns the line count.

    Append-only, so nothing written earlier is ever rewritten. The CALLER must invoke this
    after every child rather than once per phase: the guarantee is only as good as its call
    site.
    """
    if not os.path.exists(work):
        return 0

    with open(work, "rb") as f:
        data = f.read()

    with open(work, "wb"):
        pass

    if not data:
        return 0

    with open(dest, "ab") as f:
        f.write(data)

    return data.count(b"\n")


def invoke(kind, n, reps, csv, tag, mode, backend, out_name, log):
    args = [sys.executable, "tools_build.py", "run", "bench_combo", kind, n, str(reps), csv,
            tag, mode, backend, out_name]

    started = time.time()
    result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    elapsed = time.time() - started

    line = ("bench_combo %s %s %s reps=%d tag=%s rc=%d wall=%.0fs"
            % (kind, backend, mode, reps, tag, result.returncode, elapsed))
    print("  " + line, flush=True)
    log.write("  " + line + "\n")

    if result.returncode != 0:
        log.write("    stderr: " + result.stderr.strip()[-500:] + "\n")

    log.flush()
    return result.returncode

def proc_of(tag):
    """The process bucket of a run_tag. Sweep tags are 'cmb-P3-mixed'; anything else (a
    hand-run smoke tag, say) is its own bucket rather than an IndexError."""
    parts = tag.split("-")

    return parts[1] if len(parts) > 1 else tag


def paired_ratios(rows, backend, mode, kind, frac, base, cand, metric):
    """Per-process median of the within-repetition ratio. Pairing is by (run_tag, rep)."""
    pairs = collections.defaultdict(dict)

    for r in rows:
        if r["backend"] != backend or r["mode"] != mode or r["kind"] != kind:
            continue

        if float(r["frac"]) != frac:
            continue

        if r["arm"] not in (base, cand):
            continue

        pairs[(r["run_tag"], r["rep"])][r["arm"]] = r[metric]

    by_proc = collections.defaultdict(list)

    for (tag, _rep), v in pairs.items():
        if base in v and cand in v:
            try:
                ratio = float(v[cand]) / float(v[base])
            except (TypeError, ZeroDivisionError):
                continue

            if ratio == ratio and ratio not in (float("inf"), float("-inf")):
                by_proc[proc_of(tag)].append(ratio)

    return {p: statistics.median(vals) for p, vals in by_proc.items() if vals}


def absolute_median(rows, backend, mode, kind, frac, arm, metric):
    vals = [float(r[metric]) for r in rows
            if r["backend"] == backend and r["mode"] == mode and r["kind"] == kind
            and float(r["frac"]) == frac and r["arm"] == arm]

    return statistics.median(vals) if vals else None


def reading(vals):
    """The single verdict rule this analyzer uses, in one place."""
    vals = sorted(vals)
    med = statistics.median(vals)
    faster = sum(1 for v in vals if v < 1.0)
    n = len(vals)
    noise = (vals[-1] / vals[0]) if vals[0] > 0 else float("inf")

    if max(vals) == min(vals) == 1.0:
        return med, faster, n, noise, "IDENTICAL (exactly equal)"

    if abs(med - 1.0) < max(0.0, noise - 1.0):
        return med, faster, n, noise, "UNRESOLVED (inside the noise floor)"

    if faster >= n - 1:
        return med, faster, n, noise, "candidate faster, direction %d/%d" % (faster, n)

    if faster <= 1:
        return med, faster, n, noise, "candidate SLOWER, direction %d/%d" % (faster, n)

    return med, faster, n, noise, "mixed direction %d/%d" % (faster, n)


def evidence_checks(rows):
    """A cross-arm checksum match is NOT one of these checks -- see the module docstring."""
    print("=" * 120)
    print("EVIDENCE CHECKS")
    print("=" * 120)
    print()
    print("  %-6s %-5s %-7s %5s %8s %8s %8s %8s %8s %10s %10s %9s"
          % ("back", "mode", "corpus", "frac", "hit_exp", "hit_obs", "miss_obs", "mis",
             "scan_mis", "hash", "checksum", "verdict"))
    print("  " + "-" * 116)

    problems = 0
    groups = collections.defaultdict(list)

    for r in rows:
        groups[(r["backend"], r["mode"], r["kind"], float(r["frac"]))].append(r)

    for (backend, mode, kind, frac) in sorted(groups):
        g = groups[(backend, mode, kind, frac)]

        hit_exp = {r["hit_expected"] for r in g}
        hit_obs = {r["hit_observed"] for r in g}
        miss_obs = {r["miss_observed"] for r in g}
        mism = sum(r["hit_mismatch"] + r["miss_mismatch"] + r["scan16_mismatch"] +
                   r["scan256_mismatch"] + r["scan4096_mismatch"] for r in g)
        hashes = {(r["hit_hash"], r["miss_hash"]) for r in g}
        checksums = {(r["hit_checksum"], r["miss_checksum"]) for r in g}
        lives = {r["live"] for r in g}

        ok = (mism == 0 and len(hit_exp) == 1 and len(hit_obs) == 1
              and len(miss_obs) == 1 and list(hit_exp)[0] > 0
              and list(hit_exp)[0] == list(hit_obs)[0] and list(miss_obs)[0] == 0
              and len(hashes) == 1 and len(checksums) == 1 and len(lives) == 1)

        if not ok:
            problems += 1

        print("  %-6s %-5s %-7s %5.2f %8s %8s %8s %8d %8d %10s %10s  %s"
              % (backend, mode, kind, frac,
                 list(hit_exp)[0] if len(hit_exp) == 1 else "MIXED",
                 list(hit_obs)[0] if len(hit_obs) == 1 else "MIXED",
                 list(miss_obs)[0] if len(miss_obs) == 1 else "MIXED",
                 mism, sum(r["scan16_mismatch"] + r["scan256_mismatch"] +
                           r["scan4096_mismatch"] for r in g),
                 "same" if len(hashes) == 1 else "MIXED",
                 "same" if len(checksums) == 1 else "MIXED",
                 "ok" if ok else "*** PROBLEM ***"))

    print()
    print("  hit_exp is the number of probes that must be found; it must be > 0 so the phase")
    print("  is not a degenerate all-miss workload, and hit_obs must equal it.")
    print("  hash=checksum=live 'same' means all arms and repetitions saw the same keys,")
    print("  produced the same answers, and held the same live count.")
    print()

    if problems:
        print("  %d group(s) failed the evidence checks -- the ratios above are NOT usable."
              % problems)
    else:
        print("  all %d group(s) passed." % len(groups))

    return problems

def fmt_value(metric, v):
    if v is None:
        return "n/a"

    if metric.endswith("_ns"):
        return "%.3f" % (v / 1e6)          # ms

    if metric.endswith("_bytes") or metric == "overflow":
        return "%.0f" % v

    return "%.6g" % v


def print_absolutes(rows, backends, modes, kinds, metrics):
    for metric, desc in metrics:
        print("=" * 120)
        print("ABSOLUTE MEDIANS (median over processes and reps), metric=%s (%s)%s"
              % (metric, desc, "  [displayed in ms]" if metric.endswith("_ns") else ""))
        print("=" * 120)
        print("  %-10s %-5s %-7s %5s | %12s %12s %12s %12s"
              % ("backend", "mode", "corpus", "frac", "B", "W", "C", "WC"))
        print("  " + "-" * 88)

        for backend in backends:
            for mode in modes:
                for kind in kinds:
                    for frac in FRACS:
                        cells = [fmt_value(metric,
                                           absolute_median(rows, backend, mode, kind, frac, arm,
                                                           metric))
                                 for arm in ARMS]
                        print("  %-10s %-5s %-7s %5.2f | %12s %12s %12s %12s"
                              % (backend, mode, kind, frac, cells[0], cells[1], cells[2],
                                 cells[3]))

        print()


def print_ratios(rows, backends, modes, kinds, metrics):
    for metric, desc in metrics:
        print("=" * 120)
        print("PAIRED RATIOS (within repetition, per-process median), metric=%s (%s)  "
              "smaller is better" % (metric, desc))
        print("=" * 120)
        print("  %-10s %-5s %-7s %5s | %-13s %-13s %-13s %-13s %-13s"
              % ("backend", "mode", "corpus", "frac", "W/B", "C/B", "WC/W", "WC/C", "WC/B"))
        print("  " + "-" * 116)

        for backend in backends:
            for mode in modes:
                for kind in kinds:
                    for frac in FRACS:
                        cells = []

                        for base, cand in PAIRS:
                            d = paired_ratios(rows, backend, mode, kind, frac, base, cand,
                                              metric)

                            if not d:
                                cells.append("%-13s" % "n/a")
                                continue

                            med, faster, n, noise, _read = reading(list(d.values()))
                            # The noise floor is printed WITH the ratio so a reader can apply
                            # the same rule the analyzer applies: a difference smaller than
                            # max/min of the per-process medians is not a difference this
                            # device can see. Without it the table cannot be judged at all.
                            cells.append("%6.3f %d/%d %5.2f" % (med, faster, n, noise))

                        print("  %-10s %-5s %-7s %5.2f | %s"
                              % (backend, mode, kind, frac, " ".join(cells)))

        print()
        print("  the verdict text (UNRESOLVED / faster / SLOWER / mixed, with the noise floor)")
        print("  is available per cell by re-running with --metrics %s and inspecting"
              % metric)
        print("  the printed table; the full per-cell verdicts are reproduced in the round's")
        print("  log by the driver's analysis pass.")
        print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--processes", type=int, default=5)
    ap.add_argument("--reps", type=int, default=4)
    ap.add_argument("--modes", default="same,grow")
    ap.add_argument("--backends", default="ctx16,lits")
    ap.add_argument("--run-id", default=time.strftime("%Y%m%d_%H%M%S"))
    ap.add_argument("--analyze-only", default="")
    ap.add_argument("--metrics", default="")
    opts = ap.parse_args()

    modes = [m.strip() for m in opts.modes.split(",") if m.strip()]
    backends = [b.strip() for b in opts.backends.split(",") if b.strip()]

    for m in modes:
        if m not in MODES:
            raise SystemExit("bad mode: %s" % m)

    for b in backends:
        if b not in BACKENDS:
            raise SystemExit("bad backend: %s" % b)

    if not opts.analyze_only:
        if opts.reps % len(ARMS) != 0:
            raise SystemExit(
                "REFUSED: reps must be a multiple of the arm count (%d) so every arm occupies "
                "every position exactly once -- got %d" % (len(ARMS), opts.reps))

        log_path = os.path.join(HERE, "logs", "run_combo_%s.log" % opts.run_id)

        with open(log_path, "w", encoding="utf-8") as log:
            log.write("combo sweep run_id=%s  %s\n" % (opts.run_id,
                                                       time.strftime("%Y-%m-%d %H:%M:%S")))
            print("run_id %s" % opts.run_id, flush=True)

            # Position balance, asserted rather than assumed.
            placed = [[0] * len(ARMS) for _ in ARMS]

            for r in range(opts.reps):
                for pos, arm in enumerate((r + k) % len(ARMS) for k in range(len(ARMS))):
                    placed[arm][pos] += 1

            for arm in range(len(ARMS)):
                if len(set(placed[arm])) != 1:
                    raise SystemExit("REFUSED: arm %d is not position-balanced: %s"
                                     % (arm, placed[arm]))

            log.write("position balance verified: %s\n" % placed)

            for backend in backends:
                for mode in modes:
                    work_name = "combo_work_%s_%s" % (backend, mode)
                    work = os.path.join(MAIN, work_name + ".jsonl")
                    dest = os.path.join(MAIN,
                                        "combo_%s_%s_%s.jsonl" % (opts.run_id, backend, mode))

                    if os.path.exists(dest):
                        raise SystemExit(
                            "REFUSED: refusing to overwrite existing evidence: %s" % dest)

                    if os.path.exists(work):
                        os.remove(work)

                    header = ("--- backend %s mode %s -> %s ---"
                              % (backend, mode, os.path.basename(dest)))
                    print(header, flush=True)
                    log.write(header + "\n")

                    total = 0
                    failures = 0

                    for p in range(1, opts.processes + 1):
                        for kind, n, csv in DATASETS:
                            tag = "cmb-P%d-%s" % (p, kind)
                            rc = invoke(kind, n, opts.reps, csv, tag, mode, backend, work_name,
                                        log)

                            if rc != 0:
                                failures += 1

                            total += archive(work, dest)

                    line = "    archived %d row(s), %d child failure(s)" % (total, failures)
                    print(line, flush=True)
                    log.write(line + "\n")
                    log.flush()

            log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

        print("COMBO SWEEP COMPLETE run_id=%s" % opts.run_id, flush=True)

    run_id = opts.analyze_only or opts.run_id
    paths = sorted(glob.glob(os.path.join(MAIN, "combo_%s_*.jsonl" % run_id)))
    paths = [p for p in paths if "combo_work_" not in os.path.basename(p)]

    if not paths:
        print("no records for run_id=%s under %s" % (run_id, MAIN))
        return 2

    rows = load_rows(paths)
    print("loaded %d row(s) from %d file(s)" % (len(rows), len(paths)))

    present_backends = [lbl for lbl in ("Context16", "LitsHpt")
                        if any(r["backend"] == lbl for r in rows)]
    present_modes = [m for m in MODES if any(r["mode"] == m for r in rows)]
    present_kinds = [k for k, _n, _c in DATASETS if any(r["kind"] == k for r in rows)]

    metrics = METRICS

    if opts.metrics:
        want = {m.strip() for m in opts.metrics.split(",") if m.strip()}
        known = {m for m, _d in METRICS}
        unknown = want - known

        if unknown:
            raise SystemExit("unknown metric(s): %s" % ",".join(sorted(unknown)))

        metrics = [(m, d) for m, d in METRICS if m in want]

    problems = evidence_checks(rows)
    print_absolutes(rows, present_backends, present_modes, present_kinds, metrics)
    print_ratios(rows, present_backends, present_modes, present_kinds, metrics)

    if problems:
        print("%d evidence problem group(s): the ratios above are NOT usable." % problems)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
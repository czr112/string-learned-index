"""BlockLinearRank entry runner -- P2 of docs/NEXT_ACTION_PLAN_20261011_BLOCK_MODEL.md.

Pre-registered rules. Every cell (corpus x backend) must report gate_pass with every declared
gate true; the rank-specific gates must be true on the BlockLinearRank arm and JSON null on the
Context16 control; the row count must equal an independently computed unique-key count; the
dynamic program must positively trigger at least one region split; and the post-rebuild checks
must be clean. The analyzer self-tests itself against deliberately broken records before any
cell is built. The sanitizer cells (ASan+UBSan) must exit 0 with EMPTY stderr on every cell.

Two modes:
  * official (default): the project's WSL toolchain, -march=x86-64-v3, ASan/UBSan cells, taskset.
  * --native: a MinGW/Windows toolchain that cannot build the shipped ISA (the project's own
    notes record that this index is WSL-only) and has no sanitizer runtime. Such a run is
    recorded as official:false and is an INTERIM signal, never the round's acceptance.

This round changes only diagnostics plus the two identity fields the plan requires; the index
source is pinned, and the pin is checked against the adapter manifests that carry it.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
CORPORA = ("mixed", "skew", "tranco")
BACKENDS = ("blockrank", "context16")
MANIFESTS = ("our_index", "our_index_lits", "our_index_routing", "our_index_lits_routing")
RANK_GATES = ("gate_rank_prefix", "gate_rank_monotone", "gate_rank_range",
              "gate_rank_recompute", "gate_stats_replay", "gate_coverage")
COMMON_GATES = ("gate_load", "gate_hits", "gate_miss", "gate_partition", "gate_domain",
                "gate_boundary", "gate_dynamic", "gate_negative_control")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def unique_key_count(path):
    """The evaluator's raw-lines reader plus its sort/dedupe normalization, counted here so the
    row count is checked against something other than the checker's own loader."""
    raw = Path(path).read_bytes().split(b"\n")
    if raw and raw[-1] == b"":
        raw.pop()
    keys = set()
    for line in raw:
        if line.endswith(b"\r"):
            line = line[:-1]
        keys.add(line)
    return len(keys)


def evaluate(records, expectations):
    """Return the list of pre-registered violations; empty list = pass."""
    fails = []
    cells = {(r.get("corpus"), r.get("backend")): r for r in records}
    want = {(c, b) for c in CORPORA for b in BACKENDS}
    for key in sorted(want - set(cells)):
        fails.append("missing cell %s/%s" % key)
    for key in sorted(set(cells) - want):
        fails.append("unexpected cell %s/%s" % key)
    for key, r in sorted(cells.items()):
        corpus, backend = key
        if r.get("phase") != "block_model_entry":
            fails.append("%s/%s phase is not the entry phase" % (corpus, backend))
        if r.get("limit"):
            fails.append("%s/%s ran under --limit" % (corpus, backend))
        if r.get("rows") != expectations[corpus]:
            fails.append("%s/%s rows=%s, the dataset has %s unique keys"
                         % (corpus, backend, r.get("rows"), expectations[corpus]))
        for gate in COMMON_GATES:
            if r.get(gate) is not True:
                fails.append("%s/%s %s is not true" % (corpus, backend, gate))
        if r.get("gate_pass") is not True:
            fails.append("%s/%s gate_pass is not true" % (corpus, backend))
        if backend == "blockrank":
            if r.get("rank_applicable") is not True:
                fails.append("%s/blockrank does not report the rank gates as applicable" % corpus)
            for gate in RANK_GATES:
                if r.get(gate) is not True:
                    fails.append("%s/blockrank %s is not true" % (corpus, gate))
            if r.get("coverage_fits") != r.get("regions_static"):
                fails.append("%s/blockrank coverage_fits does not match the static region count"
                             % corpus)
            if not r.get("coverage_segments_total"):
                fails.append("%s/blockrank reports no fitted segments" % corpus)
            if r.get("status_counts", {}).get("ok", 0) + r.get("status_counts", {}).get(
                    "no_keys", 0) + r.get("status_counts", {}).get("too_few_keys", 0) + r.get(
                    "status_counts", {}).get("single_group", 0) != r.get("regions_static"):
                fails.append("%s/blockrank status counts do not cover the static regions" % corpus)
        else:
            if r.get("rank_applicable") is not False:
                fails.append("%s/context16 reports the rank gates as applicable" % corpus)
            for gate in RANK_GATES:
                if r.get(gate) is not None:
                    fails.append("%s/context16 %s is not null" % (corpus, gate))
        if r.get("dynamic_splits", 0) < 1:
            fails.append("%s/%s no split positively triggered" % key)
        if r.get("post_rebuild_hit_mismatch", 1) != 0:
            fails.append("%s/%s post-rebuild hits differ from the reference" % key)
        if r.get("post_rebuild_rank_violations", 1) != 0:
            fails.append("%s/%s post-rebuild rank violations" % key)
        if r.get("miss_mismatch", 1) != 0 or r.get("hit_mismatch", 1) != 0:
            fails.append("%s/%s differential mismatches" % key)
        if r.get("domain_hit_mismatch", 1) != 0 or r.get("domain_miss_mismatch", 1) != 0:
            fails.append("%s/%s domain mismatches" % key)
    return fails


def concentration(records):
    """The structural readout the plan asks the entry to record, per cell."""
    out = {}
    for r in records:
        key = "%s/%s" % (r.get("corpus"), r.get("backend"))
        out[key] = {
            "groups_total": r.get("groups_total"),
            "groups_empty": r.get("groups_empty"),
            "empty_share": (r.get("groups_empty", 0) / r["groups_total"])
            if r.get("groups_total") else None,
            "entries_p50": r.get("group_entries", {}).get("p50"),
            "entries_p99": r.get("group_entries", {}).get("p99"),
            "entries_max": r.get("group_entries", {}).get("max"),
            "overflow_records_static": r.get("overflow_records_static"),
            "subblocks_total": r.get("subblocks_total"),
            "candidate_blocks_per_query": (r.get("hit_probe_totals", {}).get("candidate_blocks", 0)
                                           / r["hits"]) if r.get("hits") else None,
            "primary_comparisons_per_query": (
                r.get("hit_probe_totals", {}).get("primary_comparisons", 0) / r["hits"])
            if r.get("hits") else None,
        }
    return out
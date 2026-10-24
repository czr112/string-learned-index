"""Verify a completed query run's inputs, group membership, checksums and reported CIs.

Uses only Python's standard library and the retained run directory. This verifies
evidence integrity; exit zero does NOT mean the candidate passed performance gates.
"""
import argparse
from bisect import bisect_right
import hashlib
import json
import math
from pathlib import Path
import struct

MASK = (1 << 64) - 1
STATES = ("bm_dynamic", "candidate_dynamic", "bm_snapshot", "candidate_snapshot")


def require(value, message):
    if not value:
        raise ValueError(message)


class Binary:
    def __init__(self, path, magic):
        self.data, self.pos = path.read_bytes(), 8
        require(self.data[:8] == magic, "invalid magic: " + str(path))

    def u64(self):
        result = struct.unpack_from("<Q", self.data, self.pos)[0]
        self.pos += 8
        return result

    def key(self):
        n = self.u64()
        result = self.data[self.pos:self.pos + n]
        require(len(result) == n, "truncated key")
        self.pos += n
        return result

    def finish(self):
        require(self.pos == len(self.data), "trailing/truncated binary file")


def rows(path):
    stream = Binary(path, b"SIBROW1\0")
    result = [(stream.key(), stream.u64()) for _ in range(stream.u64())]
    stream.finish()
    require(all(a[0] < b[0] for a, b in zip(result, result[1:])), "rows not sorted unique")
    return result


def interval(values):
    n = len(values)
    t = {15: 2.1314495456}[n - 1]
    mean = math.fsum(values) / n
    variance = math.fsum((v - mean) ** 2 for v in values) / (n - 1)
    half = t * math.sqrt(variance / n)
    return math.exp(mean), (math.exp(mean - half), math.exp(mean + half))


def close(actual, expected):
    return math.isclose(actual, expected, rel_tol=1e-11, abs_tol=1e-12)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path, help="Save the evidence-integrity result as UTF-8 JSON")
    args = parser.parse_args()
    root = args.directory.resolve()
    report = json.loads((root / "report.json").read_text())
    cases = json.loads((root / "cases.json").read_text())
    require(report["status"] == "complete", "run is not complete")
    require(len(cases) == 33 and sum(not v["control_only"] for v in cases.values()) == 32, "wrong case coverage")
    counts = {"original_cells": 32, "control_cells": 1, "process_blocks": 0, "timed_batches": 0, "query_entries": 0}
    input_cache = {}
    for name, meta in cases.items():
        for key, sha in meta["input_sha256"].items():
            path = root / "inputs" / (sha + ".bin")
            if key == "operations" and meta["control_only"]:
                path = root / "control_inputs/ops.bin"
            require(hashlib.sha256(path.read_bytes()).hexdigest() == sha, "input snapshot hash mismatch")
        pool_sha = meta["input_sha256"]["pool"]
        if pool_sha not in input_cache:
            input_cache[pool_sha] = {key: i for i, (key, _) in enumerate(rows(root / "inputs" / (pool_sha + ".bin")))}
        ranks = input_cache[pool_sha]
        terminal = root / "cases" / name / "final.bin"
        query_file = root / "cases" / name / "queries.bin"
        require(hashlib.sha256(terminal.read_bytes()).hexdigest() == meta["final_sha256"], "terminal hash mismatch")
        require(hashlib.sha256(query_file.read_bytes()).hexdigest() == meta["query_sha256"], "query hash mismatch")
        final = dict(rows(terminal))
        lower, touched = None, set()
        if meta["control_only"]:
            initial = rows(root / "inputs" / (meta["input_sha256"]["rows"] + ".bin"))
            lower = [b""] + [initial[i][0] for i in range(2048, len(initial), 2048)]
            replay = dict(initial)
            operation_stream = Binary(root / "control_inputs/ops.bin", b"SIBOPS1\0")
            for _ in range(operation_stream.u64()):
                kind = operation_stream.data[operation_stream.pos]; operation_stream.pos += 1
                value, limit, key = operation_stream.u64(), operation_stream.u64(), operation_stream.key()
                if kind == 2 and key not in replay:
                    replay[key] = value
                    touched.add(max(0, bisect_right(lower, key) - 1))
                else:
                    require(kind in (1, 5), "sparse control contains unexpected/failed mutation")
            operation_stream.finish()
            require(replay == final and len(touched) == meta["write_touched_initial_pages"], "sparse control replay mismatch")
        stream = Binary(query_file, b"SIBQRY2\0")
        lo, hi, n_groups = stream.u64(), stream.u64(), stream.u64()
        require([lo, hi] == meta["hot_range_inclusive"], "hot range mismatch")
        sums, sizes = {}, {}
        for _ in range(n_groups):
            group, size = stream.key().decode("ascii"), stream.u64()
            unique, hits, word_sum = set(), 0, 0
            for _ in range(size):
                key = stream.key()
                present = stream.data[stream.pos]; stream.pos += 1
                value, rank = stream.u64(), stream.u64()
                require(key not in unique and present in (0, 1), "duplicate/invalid query")
                unique.add(key)
                require(rank == ranks.get(key, MASK), "rank mismatch")
                require(bool(present) == (key in final), "query live/missing state mismatch")
                require(value == (final[key] if present else 0), "query value mismatch")
                hot = rank != MASK and lo <= rank <= hi
                if group == "live_hot":
                    require(present and hot, "live_hot mismatch")
                elif group == "live_nonhot":
                    require(present and not hot and rank != MASK, "live_nonhot mismatch")
                elif group == "absent_hot":
                    require(not present and hot, "absent_hot mismatch")
                elif group == "absent_nonhot":
                    require(not present and not hot and rank != MASK, "absent_nonhot mismatch")
                elif group == "synthetic_absent":
                    require(not present and rank == MASK, "synthetic_absent mismatch")
                elif group == "mixed90" and not present:
                    require(rank == MASK, "mixed90 miss is not a synthetic absent key")
                elif group in ("live_write_touched", "live_write_untouched"):
                    require(lower is not None and present, "invalid affected-range query")
                    affected = max(0, bisect_right(lower, key) - 1) in touched
                    require(affected == (group == "live_write_touched"), "affected/untouched range mismatch")
                hits += present
                term = ((value ^ 0x517cc1b727220a95) + 0x9e3779b97f4a7c15) if present else 0xd6e8feb86659fd93
                word_sum = (word_sum + term) & MASK
            require(size == meta["groups"][group]["keys"] and hits == meta["groups"][group]["hits"], "group counts mismatch")
            if group == "mixed90":
                require(hits * 10 == size * 9, "mixed90 proportion mismatch")
            sums[group], sizes[group] = word_sum, size
            counts["query_entries"] += size
        stream.finish()
        blocks = [json.loads(line) for line in (root / "raw" / (name + ".jsonl")).read_text().splitlines() if line.strip()]
        require(len(blocks) == 16 and {b["process"] for b in blocks} == set(range(16)), "block coverage mismatch")
        loops = dict(zip(meta["groups"], report["calibration"][name]["loops"]))
        all_times = {g: {s: [] for s in STATES} for g in sizes}
        all_aa = {g: {s: [] for s in STATES} for g in sizes}
        for block in sorted(blocks, key=lambda b: b["process"]):
            seen = {}
            for measured in block["measurements"]:
                group, state, repeat = measured["group"], measured["state"], measured["repeat"]
                key = group, state, repeat
                require(key not in seen and repeat in (0, 1) and state in STATES, "duplicate/unknown batch")
                require(measured["lookups"] == sizes[group] * loops[group] and measured["ns"] > 0, "lookup/duration mismatch")
                require(0 < measured["lookups"] <= 8192000 and loops[group] & (loops[group] - 1) == 0, "registered query budget mismatch")
                require(measured["checksum"] == sums[group] * loops[group] & MASK, "timed checksum mismatch")
                seen[key] = measured["ns"] / measured["lookups"]
                counts["timed_batches"] += 1
            for group, size in sizes.items():
                if not size:
                    continue
                for state in STATES:
                    a, b = seen[group, state, 0], seen[group, state, 1]
                    all_times[group][state].append(math.log(a * b) / 2)
                    all_aa[group][state].append(math.log(b / a))
            counts["process_blocks"] += 1
        for group, size in sizes.items():
            if not size:
                continue
            result = report["results"][name][group]
            aa_pass = {}
            for state in STATES:
                ratio, bounds = interval(all_aa[group][state]); stored = result["aa"][state]
                require(close(ratio, stored["ratio"]) and all(close(a, b) for a, b in zip(bounds, stored["ci95"])), "A/A CI mismatch")
                aa_pass[state] = bounds[0] >= 1 / 1.05 and bounds[1] <= 1.05
                require(aa_pass[state] == stored["equivalent_within_5pct"], "AA gate mismatch")
            for label, left, right, limit in (
                ("candidate_vs_bm_dynamic", STATES[1], STATES[0], 1.05),
                ("candidate_vs_bm_snapshot", STATES[3], STATES[2], 1.05),
                ("candidate_dynamic_vs_snapshot", STATES[1], STATES[3], 1.15),
                ("bm_dynamic_vs_snapshot", STATES[0], STATES[2], 1.15),
            ):
                ratio, bounds = interval([a - b for a, b in zip(all_times[group][left], all_times[group][right])])
                stored = result["contrasts"][label]
                require(close(ratio, stored["ratio"]) and all(close(a, b) for a, b in zip(bounds, stored["ci95"])), "reported paired CI mismatch")
                verdict = "pass" if bounds[1] <= limit else "regression" if bounds[0] > limit else "undecided"
                if not (aa_pass[left] and aa_pass[right]):
                    verdict = "measurement_unresolved"
                require(stored["verdict"] == verdict, "reported verdict mismatch")
    result = {"evidence_integrity_pass": True, **counts}
    if args.output:
        args.output.write_bytes((json.dumps(result, indent=2) + "\n").encode())
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()

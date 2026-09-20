#!/usr/bin/env python3
"""Render E24_TABLES.md from results/main/e24_diag.jsonl.

E24 takes no timings and uses fixed seeds, so these are COUNTS, not medians
over repetitions. There is no noise floor to report and no cross-process
aggregation -- a number here is reproducible by re-running the diagnostic.

Usage:  python3 analyze_e24.py
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "results", "main", "e24_diag.jsonl")
OUT = os.path.join(HERE, "E24_TABLES.md")

MB = 1048576.0
ORDER = {"initial": 0, "update_only": 1, "insert_only": 2, "replace25": 3}


def load():
    with open(SRC, encoding="utf-8") as f:
        return [json.loads(l) for l in f if l.strip()]


def main():
    rows = load()
    rows.sort(key=lambda r: (r["kind"], ORDER[r["state"]]))
    w = []
    a = w.append

    a("# E24 tables: update path, space decomposition, reclaim opportunity")
    a("")
    a("Source: `results/main/e24_diag.jsonl`, %d records (3 datasets x 4 states)." % len(rows))
    a("")
    a("**Counts, not timings.** E24 takes no timings and uses fixed seeds, so these")
    a("numbers are reproducible by re-running the diagnostic. There is no noise floor")
    a("to report and no cross-process aggregation.")
    a("")
    a("Test queries are answered against the reference map, never against the index")
    a("under test, and a probe whose answers disagree fails the run. So a row existing")
    a("at all means every probe in it was cross-checked.")
    a("")
    a("**These are three separately-run workloads, not one mixed workload.** The rows")
    a("for `update_only` / `insert_only` / `replace25` are NOT a time decomposition of")
    a("a mixed update stream; each is its own state, built from its own fresh initial")
    a("state and compared at the same counter checkpoint.")
    a("")

    # ---- A -----------------------------------------------------------------
    a("## 1. What each path walks, split by where `find` put the key")
    a("")
    a("`find` uses the fingerprint tag filter, the length pre-check, the boundary")
    a("directory and the overflow Hybrid. `update` and `erase` use **none** of them:")
    a("they walk every sub-block and every entry with a full-key compare.")
    a("")
    for r in rows:
        a("### %s / `%s`" % (r["kind"], r["state"]))
        a("")
        a("| where | keys | find | update | erase | update / find |")
        a("|---|---:|---:|---:|---:|---:|")
        for b in r["paths"]:
            n = max(b["keys"], 1)
            f, u, e = b["find_cmps"] / n, b["upd_cmps"] / n, b["era_cmps"] / n
            ratio = "**%.0fx**" % (u / f) if f > 0 else "n/a"
            a("| %s | %d | %.3f | %.1f | %.1f | %s |" % (b["where"], b["keys"], f, u, e, ratio))
        a("")

    # ---- B -----------------------------------------------------------------
    a("## 2. Space, in the two layers")
    a("")
    a("### 2.1 Main-block ledger (bytes)")
    a("")
    a("The block allocation covers ONLY block storage. Within a sub-block,")
    a("`bb = sizeof(Header) + slots*sizeof(Entry) + suffix_cap`, so")
    a("`header + descriptor + suffix_cap` must equal `blocks`. `closes` reports that.")
    a("")
    a("| dataset | state | header | descriptor | suffix cap | **sum** | blocks | closes |")
    a("|---|---|---:|---:|---:|---:|---:|:--:|")
    for r in rows:
        s = r["space"]
        ok = "yes" if s["main_ledger_sum"] == s["blocks"] else "**NO**"
        a("| %s | `%s` | %d | %d | %d | **%d** | %d | %s |" % (
            r["kind"], r["state"], s["main_header"], s["main_descriptor"], s["main_suffix_cap"],
            s["main_ledger_sum"], s["blocks"], ok))
    a("")
    a("Note: unused SLOTS are inside `descriptor` (the slots are preallocated), so they")
    a("are not added again; `suffix cap` is the whole suffix area, live + free + garbage.")
    a("")
    a("### 2.2 Main-block suffix area (MB) and slot use")
    a("")
    a("`garbage` = bytes inside the occupied region that no live entry references --")
    a("suffixes left behind by `erase`, which only decrements the count.")
    a("")
    a("| dataset | state | live | free | garbage | slots used |")
    a("|---|---|---:|---:|---:|---:|")
    for r in rows:
        s = r["space"]
        a("| %s | `%s` | %.2f | %.2f | %.2f | %.1f%% |" % (
            r["kind"], r["state"], s["main_suffix_live"] / MB, s["main_suffix_free"] / MB,
            s["main_garbage"] / MB, 100.0 * s["main_slots_used"] / max(s["main_slots_cap"], 1)))
    a("")
    a("### 2.3 Overflow: payload vs retained capacity")
    a("")
    a("`elem cap` is the record array's reserved bytes (capacity x sizeof(OverflowRecord));")
    a("`elem used` is what the live records need. The difference is capacity the")
    a("container still holds after records were erased. Suffix strings are counted")
    a("separately (`suffix live` / `suffix cap`) and are NOT folded into `elem`.")
    a("")
    a("| dataset | state | records | suffix live | suffix cap | elem used | elem cap | elem slack |")
    a("|---|---|---:|---:|---:|---:|---:|---:|")
    for r in rows:
        s = r["space"]
        used = s["ov_records"] * s["ov_rec_size"]
        a("| %s | `%s` | %d | %d | %d | %d | %d | **%d** |" % (
            r["kind"], r["state"], s["ov_records"], s["ov_live_bytes"], s["ov_suffix_cap"],
            used, s["ov_container_cap"], s["ov_container_cap"] - used))
    a("")
    a("### 2.4 Separately listed, and RSS")
    a("")
    a("`memory_breakdown()` is the index's own requested bytes. RSS is what the process")
    a("actually holds. These are DIFFERENT accounting bases and are reported apart.")
    a("")
    a("| dataset | state | model | directory | tables | fences+self | peak RSS (MB) |")
    a("|---|---|---:|---:|---:|---:|---:|")
    for r in rows:
        s = r["space"]
        misc = s["regions_self"] + s["fences"] + s["self"] + s["prefix"]
        a("| %s | `%s` | %.2f | %.2f | %.2f | %.2f | %.1f |" % (
            r["kind"], r["state"], s["model"] / MB, s["directory"] / MB, s["tables"] / MB,
            misc / MB, r["peak_rss_kb"] / 1024.0))
    a("")

    # ---- C -----------------------------------------------------------------
    a("## 3. Reclaim opportunity, and where the free space actually sits")
    a("")
    a("A record may move back only into the ONE sub-block its key order selects (M1),")
    a("and only if THAT sub-block has a free slot AND room for its length. `fits any`")
    a("is what the existing coarse predicate (`group_has_room`) would claim instead.")
    a("")
    a("| dataset | state | overflow records | groups | **fits (ordered)** | blocked: slots | blocked: bytes | blocked: order | fits any sub-block |")
    a("|---|---|---:|---:|---:|---:|---:|---:|---:|")
    for r in rows:
        s, c = r["space"], r["reclaim"]
        a("| %s | `%s` | %d | %d | **%d** | %d | %d | %d | %d |" % (
            r["kind"], r["state"], s["ov_records"], c["groups_with_ov"], c["fits_ordered"],
            c["blocked_slots"], c["blocked_bytes"], c["blocked_order"], c["fits_if_any_subblock"]))
    a("")
    a("**Is the free space in the record's own group?** A group that holds overflow AND")
    a("has both a free slot and free suffix bytes could in principle host something.")
    a("Free space in groups that hold NO overflow cannot serve overflow records at all.")
    a("")
    a("| dataset | state | groups with overflow **and** room | free slots in groups without overflow | free bytes in groups without overflow |")
    a("|---|---|---:|---:|---:|")
    for r in rows:
        c = r["reclaim"]
        a("| %s | `%s` | %d | %d | %d |" % (
            r["kind"], r["state"], c["grp_ov_has_slot_and_bytes"],
            c["free_slots_in_ovfree_groups"], c["free_bytes_in_ovfree_groups"]))
    a("")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(w) + "\n")
    print("wrote %s (%d lines)" % (OUT, len(w)))


if __name__ == "__main__":
    main()

# -*- coding: utf-8 -*-
"""Independent reference for the TRUNCDIR design (TRUNCDIR_ENTRY_DESIGN.md, 2026-10-11).

Written from the DESIGN TEXT ONLY: it does not read, link or mirror the C++ implementation.
It re-derives, for every region of the three real corpora, the region geometry the index uses
(region_keys=2048, slots=16, block=1024B, load=0.75, equal-depth ranks, region prefix = the
common prefix of the first and last key), then checks the four claims the design's section 3
makes, plus the scale numbers section 5 asks the gate to print:

  lemma A   routing of every STORED key is identical, truncated vs untruncated
  lemma B   stored separators stay non-decreasing
  lemma C   the bracket property holds for probes (stored keys, separator prefixes, mutations)
  depth     the equal-depth band sizes are exactly floor/ceil(n/G); empty groups are counted
  scale     arena bytes per region (full vs truncated), P50/P95/max, fallback counts

Negative controls at the end prove the checkers are not vacuous: a shortened separator and a
swapped pair must each be caught.
"""
import bisect, io, math, os, statistics, sys

ROOT = r"D:\StringLearnedIndex_Workspace"
DS = os.path.join(ROOT, "07_Eval_Integration", "datasets")
CSV = os.path.join(ROOT, "02_Code_and_Experiments", "validation_e5_20260913", "data", "tranco_XN6XN.csv")

REGION_KEYS = 2048
SLOTS, BLOCK, LOAD = 16, 1024, 0.75
HDR, ENTRY = 64, 16
CORPORA = [("mixed", 131072), ("skew", 131072), ("tranco", 262144)]


def lcp(a, b):
    n = min(len(a), len(b))
    i = 0
    while i < n and a[i] == b[i]:
        i += 1
    return i


def load_corpus(kind, n):
    if kind == "tranco":
        out = []
        with io.open(CSV, "rb") as f:
            for line in f:
                if len(out) >= n:
                    break
                p = line.find(b",")
                if p < 0:
                    continue
                key = line[p + 1:]
                if key.endswith(b"\r"):
                    key = key[:-1]
                if key.endswith(b"\n"):
                    key = key[:-1]
                out.append(key)
        if len(out) != n:
            raise SystemExit("tranco csv too short: %d" % len(out))
        out.sort()
        return out
    path = os.path.join(DS, kind + ".keys")
    with io.open(path, "rb") as f:
        keys = [l[:-1] if l.endswith(b"\n") else l for l in f]
    if len(keys) != n:
        raise SystemExit("%s has %d keys, expected %d" % (kind, len(keys), n))
    return keys


def derive_groups(n, suffix_bytes):
    count_groups = n / (float(SLOTS) * LOAD)
    byte_groups = suffix_bytes / ((BLOCK - HDR - SLOTS * ENTRY) * LOAD)
    est = int(max(count_groups, byte_groups)) + 1
    return max(1, est)


def stored_separators(keys, prefix_len, groups, truncate):
    """Design section 2, transcribed. Returns (stored, full, n_fallback, n_shortened)."""
    n = len(keys)
    full, stored = [], []
    fallback = shortened = 0
    for j in range(1, groups):
        rank = min(n - 1, (j * n) // groups)
        s = keys[rank][prefix_len:]
        full.append(s)
        t = s
        if truncate and rank > 0:
            left = keys[rank - 1][prefix_len:]
            c = lcp(left, s)
            if c < len(s):
                t = s[:c + 1]
                shortened += 1
            else:
                fallback += 1
        stored.append(t)
    return stored, full, fallback, shortened


def count_le(seps, q):
    return bisect.bisect_right(seps, q)


def check_region(keys, truncate, stats, problems, negative=None):
    n = len(keys)
    prefix = keys[0][:lcp(keys[0], keys[-1])]
    plen = len(prefix)
    sb = sum(len(k) - plen for k in keys)
    G = derive_groups(n, sb)
    seps, full, fallback, shortened = stored_separators(keys, plen, G, truncate)
    if negative == "shorten" and seps and len(seps) > 2:
        j = len(seps) // 2
        if len(seps[j]) > 1:
            seps[j] = seps[j][:-1]
    if negative == "swap" and len(seps) > 2:
        j = len(seps) // 2
        seps[j - 1], seps[j] = seps[j], seps[j - 1]

    # lemma B: non-decreasing
    for j in range(1, len(seps)):
        if seps[j - 1] > seps[j]:
            problems.append("B: separators decrease at j=%d (n=%d G=%d)" % (j, n, G))
            break

    # construction property: each stored separator is a prefix of its s_j and beats its left
    # neighbour, except in the counted duplicate-fallback case
    for j in range(len(seps)):
        rank = min(n - 1, ((j + 1) * n) // G)
        s = full[j]
        if not s.startswith(seps[j]):
            problems.append("P: t_%d is not a prefix of s_%d" % (j + 1, j + 1))
            break
        if rank > 0:
            left = keys[rank - 1][plen:]
            if not (seps[j] > left) and not (seps[j] == s and s.startswith(left)):
                problems.append("P: t_%d does not beat its left neighbour" % (j + 1))
                break

    # lemma A + group vector
    groups_vec = []
    for k in keys:
        q = k[plen:] if len(k) > plen else b""
        ga = count_le(seps, q)
        gb = count_le(full, q)
        if ga != gb:
            problems.append("A: routing differs for a stored key (n=%d G=%d): %d vs %d"
                            % (n, G, ga, gb))
        groups_vec.append(ga)

    for i in range(1, n):
        if groups_vec[i] < groups_vec[i - 1]:
            problems.append("M: group vector decreases at i=%d" % i)
            break

    # depth: band sizes are floor/ceil(n/G)
    load = [0] * G
    for g in groups_vec:
        load[min(g, G - 1)] += 1
    empty = sum(1 for v in load if v == 0)
    want = []
    for g in range(G):
        lo = (g * n) // G
        hi = ((g + 1) * n) // G
        want.append(hi - lo)
    # groups_vec counts separators <= key; the band of a key is its rank's band, so the
    # multiset of group sizes must equal the multiset of band sizes.
    if sorted(load) != sorted(want):
        problems.append("D: group sizes are not the equal-depth bands (n=%d G=%d)" % (n, G))

    # lemma C: bracket property on probes
    probes = []
    for idx in (0, 1, n // 3, n // 2, (2 * n) // 3, n - 2, n - 1):
        if 0 <= idx < n:
            probes.append(keys[idx][plen:])
    for j in (0, len(seps) // 2, len(seps) - 1):
        if 0 <= j < len(seps):
            t = seps[j]
            probes.append(t)
            if t:
                probes.append(t[:-1])
            probes.append(t + b"\x00")
            probes.append(t + b"\xff")
            probes.append(full[j])
    k0 = keys[0]
    probes += [b"", b"\x00", b"\xff", b"\x00\x00", k0 + b"\x00", k0 + b"\xff",
               keys[n - 1], keys[n - 1][:plen], keys[n - 1][:plen + 1],
               keys[n - 1] + b"\x01", keys[n - 1][:-1] if len(keys[n - 1]) else b""]
    for q in probes:
        g = count_le(seps, q)
        gi = bisect.bisect_left(groups_vec, g)          # first key with group >= g
        gj = bisect.bisect_right(groups_vec, g)          # first key with group > g
        if gi > 0 and not (keys[gi - 1][plen:] < q):
            problems.append("C: key in a group before g is not < probe (n=%d G=%d)" % (n, G))
        if gj < n and not (keys[gj][plen:] > q):
            problems.append("C: key in a group after g is not > probe (n=%d G=%d)" % (n, G))
        if g > G - 1:
            problems.append("C: group out of range")

    stats.append((n, G, sum(len(s) for s in full), sum(len(s) for s in seps),
                  fallback, shortened, empty, len(seps)))
    return len(seps)


def main():
    corpora = {}
    for kind, n in CORPORA:
        corpora[kind] = load_corpus(kind, n)

    all_problems, summary = [], []
    for kind, n in CORPORA:
        keys = corpora[kind]
        assert keys == sorted(keys) and len(set(keys)) == len(keys), kind
        full_stats, trunc_stats = [], []
        for start in range(0, len(keys), REGION_KEYS):
            part = keys[start:start + REGION_KEYS]
            if len(part) < 2:
                continue
            check_region(part, False, full_stats, all_problems)
            check_region(part, True, trunc_stats, all_problems)

        def agg(stats, idx):
            vals = [s[idx] for s in stats]
            return vals
        provs = len(trunc_stats)
        full_bytes = sum(s[2] for s in full_stats)
        trunc_bytes = sum(s[3] for s in trunc_stats)
        per_region = [s[3] for s in trunc_stats]
        per_region_full = [s[2] for s in full_stats]
        p = lambda q: sorted(per_region)[int(q * (len(per_region) - 1))]
        ratios = sorted(t / f for t, f in zip(per_region, per_region_full) if f)
        q = lambda v, qq: sorted(v)[int(qq * (len(v) - 1))]
        top = sorted(zip(per_region, per_region_full), reverse=True)[:5]
        summary.append({
            "kind": kind, "regions": provs, "keys": n,
            "full_bytes": full_bytes, "trunc_bytes": trunc_bytes,
            "per_region_full_mean": statistics.mean(per_region_full),
            "per_region_trunc_mean": statistics.mean(per_region),
            "p50": p(0.5), "p95": p(0.95), "max": max(per_region),
            "fallback": sum(s[4] for s in trunc_stats),
            "shortened": sum(s[5] for s in trunc_stats),
            "thresholds": sum(s[7] for s in trunc_stats),
            "empty_groups": sum(s[6] for s in trunc_stats),
            "groups": sum(s[1] for s in trunc_stats),
            "ratio_p01": q(ratios, 0.01), "ratio_p25": q(ratios, 0.25),
            "ratio_p75": q(ratios, 0.75), "ratio_p99": q(ratios, 0.99),
            "top5_trunc": [t for t, _ in top], "top5_full": [f for _, f in top],
        })

    # negative controls, on one real region of each corpus
    controls = []
    for kind, n in CORPORA:
        part = corpora[kind][:REGION_KEYS]
        for mode in ("shorten", "swap"):
            probs = []
            check_region(part, True, [], probs, negative=mode)
            controls.append((kind, mode, len(probs)))

    print("=== independent TRUNCDIR reference (design-text only) ===")
    for s in summary:
        print(("%-7s regions=%-4d thresholds=%-6d arena full=%9d trunc=%9d "
               "(%5.1f%% of full) per-region mean=%.0f B p50=%d p95=%d max=%d "
               "shortened=%d fallback=%d empty_groups=%d")
              % (s["kind"], s["regions"], s["thresholds"], s["full_bytes"], s["trunc_bytes"],
                 100.0 * s["trunc_bytes"] / s["full_bytes"] if s["full_bytes"] else 0.0,
                 s["per_region_trunc_mean"], s["p50"], s["p95"], s["max"],
                 s["shortened"], s["fallback"], s["empty_groups"]))
    for s in summary:
        print("        per-region ratio trunc/full: p01=%.3f p25=%.3f p75=%.3f p99=%.3f; "
              "largest truncated regions (trunc/full bytes): %s"
              % (s["ratio_p01"], s["ratio_p25"], s["ratio_p75"], s["ratio_p99"],
                 ", ".join("%d/%d" % (t, f) for t, f in zip(s["top5_trunc"], s["top5_full"]))))
    print("negative controls (violations found, must be > 0):", controls)
    ok = all(c[2] > 0 for c in controls)
    if all_problems:
        print("PROBLEMS (%d):" % len(all_problems))
        for p_ in all_problems[:40]:
            print("  -", p_)
    else:
        print("lemmas A/B/C + depth property: 0 violations over all regions of all corpora")
    return 0 if (ok and not all_problems) else 1


if __name__ == "__main__":
    sys.exit(main())





# Independent reference for the TRUNCDIR rule (2026-10-11)

Who/what: written from `TRUNCDIR_ENTRY_DESIGN.md` §2-§3 alone, in Python, without reading or
linking the C++ implementation. It re-derives the region geometry the index uses for the
initial build (`region_keys=2048`, `slots=16`, block `1024 B`, `load=0.75`, region prefix =
common prefix of first and last key, equal-depth ranks `r_j = min(n-1, floor(j*n/G))`), builds
both stored forms (full separators and the §2 truncated ones), then checks:

* lemma A -- routing of every STORED key is identical between the two forms;
* lemma B -- stored separators stay non-decreasing;
* lemma C -- the bracket property for probes (stored keys, separator prefixes, mutations,
  empty key, high bytes, keys shorter than the prefix);
* the equal-depth property -- group sizes are exactly the floor/ceil bands, empty groups counted;
* the scale numbers -- arena bytes per region (full vs truncated), P50/P95/max, fallbacks,
  how many thresholds actually got shorter.

Two negative controls must fire (a separator cut one byte too far; two separators swapped),
which is what makes "0 violations" mean something.

Run: `python scratch_truncdir/independent_python\verify_truncdir.py` (native Windows; Miniconda python 3.13).
No WSL, no compiler, no writes outside this directory.

## Result: 0 violations over every region of all three corpora

```
mixed   regions=64   thresholds=21187  arena full=  2121557 trunc=   225478 ( 10.6% of full) per-region mean=3523 B p50=2054 p95=8267 max=43002 shortened=21187 fallback=0 empty_groups=0
skew    regions=64   thresholds=20099  arena full=  1799334 trunc=   332242 ( 18.5% of full) per-region mean=5191 B p50=5114 p95=7793 max=8173 shortened=20099 fallback=0 empty_groups=0
tranco  regions=128  thresholds=21760  arena full=   285979 trunc=   113421 ( 39.7% of full) per-region mean=886 B p50=896 p95=1032 max=1360 shortened=21760 fallback=0 empty_groups=0
        per-region ratio trunc/full: p01=0.025 p25=0.033 p75=0.173 p99=0.445  (mixed)
        per-region ratio trunc/full: p01=0.126 p25=0.158 p75=0.290 p99=0.472  (skew)
        per-region ratio trunc/full: p01=0.330 p25=0.379 p75=0.415 p99=0.446  (tranco)
negative controls (violations found, must be > 0): [('mixed','shorten',11), ('mixed','swap',14), ('skew','shorten',6), ('skew','swap',14), ('tranco','shorten',7), ('tranco','swap',14)]
lemmas A/B/C + depth property: 0 violations over all regions of all corpora
```

## Why this replica can be trusted to describe the index's configuration

Three independent cross-checks against C++ records already on disk, none of them used as input:

| Quantity | This replica | Existing C++ record |
|---|---|---|
| thresholds, mixed / skew / tranco | 21,187 / 20,099 / 21,760 | `dir_thresholds` in `results/main/depth_ab_*.jsonl`: 21,187 / 20,099 / 21,760 |
| untruncated arena, per region | 33.1 / 28.1 / 2.2 KB | RESULTS_DEPTH_DIR §6: "33 KB/区域、28 KB/区域、2 KB/区域" |
| truncated arena, per region | 3.5 / 5.2 / 0.9 KB | RESULTS_THRESHOLD_ENTRY: 3.4-5.0 KB/区域, tranco 0.6 KB/区域 |

(1,024-byte blocks, 16 slots, load 0.75, `region_keys=2048`; the threshold counts match exactly,
which is what pins the group counts and therefore the ranks.)

## Mechanism notes for the write-up

* **The mean hides the tail.** mixed's truncated arena is 3,523 B/region on average but its
  median is 2,054 B and its largest region stores 43,002 B (vs 96,696 B untruncated). Per-region
  ratios run from 0.025 (p01) to 0.445 (p99). A memory claim must quote the distribution, not
  the mean.
* **Savings differ sharply by corpus**: 10.6 % of the untruncated arena on mixed, 18.5 % on skew,
  39.7 % on tranco. tranco's regions are already small, so the truncation buys least there in
  absolute terms (886 B/region).
* **No duplicate-suffix fallback fired** on any of the three corpora (all keys distinct): every
  threshold really got shorter (`shortened == thresholds`).
* **Correctness rests on the rank rule, not on the probe**: lemma A holds for every stored key in
  all 256 regions, and the negative controls show the checkers fire when the rule is broken.

## Boundary

This is corroborating, independent evidence for the DESIGN text. It does not exercise the C++
`equivdir::RegionModel` code path, the live `Region` wiring, the gates, or any timing; that is
`check_depth_dir_trunc` (C++) and the A/B round's job. Counts here are never speedups.

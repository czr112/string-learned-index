# E15 tables: one-pass construction

Source: `results/main/e15_build.jsonl`, 30 measured records (5 processes x 3 datasets x 2 targets). Warm-ups excluded by run tag.

## 1. Layout digest gate

Every run builds S both ways inside the same process and compares the two digests
BEFORE reporting any timing. The digest serialisation is byte-for-byte E14's, so the
comparison below is about the same object E14 was about.

| dataset | structure | distinct digests inside E15 | equal to E14's two-pass digest |
|---|---|---:|---|
| mixed | S | **1** | **yes** |
| mixed | D | **1** | **yes** |
| skew | S | **1** | **yes** |
| skew | D | **1** | **yes** |
| tranco | S | **1** | **yes** |
| tranco | D | **1** | **yes** |

**Normalised structural digests agree, and they agree with the digest E14 measured the
two-pass construction to produce.**

What that does and does not establish:

**Covered** (hashing the listed quantity, or the listed quantity is contained in what is
hashed): region count and order; each region's prefix; all three capacity tables; the
derived sub-block count and per-sub-block entry count; each sub-block's KEYS as stored;
the boundary directory (key and target sub-block); each region's overflow record count;
and every (key, value) in scan order, which is primary records UNION overflow records.

**Not covered**: the model's internal state (the digest hashes none of it); free and
garbage byte distribution inside a block; fingerprint tags; an entry's `offset`/`length`
fields as such; intra-block padding and alignment; and memory addresses.

**Derivable but not explicitly encoded**: primary-vs-overflow membership. It follows from
(all records in scan order) minus (each sub-block's stored keys), but the digest does not
encode the attribution, so a hypothetical swap of two same-sized records between primary
and overflow would be caught only through that derivation.

So the supported statement is: **normalised structure digest equal, test-query results
equal, and no regression observed by the existing functional checks.** Not "physically
byte-identical", and not "all model output identical". SHA-256's self-test validates the
hash implementation; it does not widen what is serialised into it.

## 2. Query A/B

After the digest comparison, a shared 50%-hit query workload is run against both
constructions and every `found`/`value` is compared. A mismatch aborts the run, so a
record existing at all means the comparison passed.

Queries compared per run: 131072 — 30 runs, 3,932,160 comparisons, 0 mismatches.

**Coverage caveat**: the workload draws keys from the key set WITH REPLACEMENT, so it is
not an exhaustive sweep -- for a 131,072-key set, 65,536 hit queries hit roughly 63% of
distinct keys. This is a query-level check, not a proof that every record is reachable.
The structural digest in section 1 is what covers the records the queries miss.

## 2b. Direct coverage of the new interface

The four inherited gates never call `bulk_load_with_plan`, so they protect only the old
paths. `check_e15_plan.cpp` covers the new one directly; a failure exits non-zero and the
runner records it as a failure.

| case | what it pins down |
|---|---|
| A1 | a plan shorter than the region count is rejected, **and leaves the index in its previous state** rather than half-built |
| A2 | a per-region shape whose length differs from that region's group count is rejected |
| A2b | a DIFFERENT but legal slot count is accepted, so A2 is not passing merely because any change is rejected |
| A2c | zero slots is rejected |
| A3 | a sub-block cap table of the wrong length is rejected |
| B | the empty input yields exactly one empty region, matching `bulk_load`'s fallback |
| C | a split whose last region is shorter than `region_keys` produces that short region on both paths |
| D | over region sizes {32, 64, 100} x {single-block, multi-block} x {short, full} key sets: region count, fences, prefixes, all three capacity tables and the structural digest agree with the two-pass path |
| E | after a new-path build, 12,000 mixed insert/update/upsert/erase/find/scan operations against `std::map`, including prefix-changing inserts that force splits |

Result: `passed` — 4 plan rejections, 12 equivalence trials, 3 edge cases, 12000 dynamic operations, 1695 scans.

A1 is not hypothetical: the first version of `bulk_load_with_plan` cleared `nodes_` and
set `size_` up front, so a plan rejected mid-loop left the index holding some of the new
regions while `size_` already reported the full key count. The gate failed on it, and the
method now builds into a local vector and swaps it in only on success.

## 3. Build time: two-pass vs one-pass (ms)

`s_old` is the E12/E13/E14 pipeline (`bulk_load` + one `rebuild_with_caps` per region).
`s_new` is `Index::bulk_load_with_plan` -- **the direct construction of the final index
given a capacity plan**. It is not a statement about the whole flow: the probe, the
statistics and the allocators all still run, and section 4 measures them. Both are built
in the same process, 3 repetitions each, with the build order alternated per repetition so
"ran first" is not confounded with "is the old one".

Per process the median of its 3 repetitions is taken; the table reports the median of
those across processes.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| S, two-pass | 96.29 | 120.28 | 167.59 |
| S, one-pass | 43.70 | 51.62 | 84.88 |
| D, two-pass | 71.13 | 80.14 | 143.58 |
| D, one-pass | 34.40 | 38.73 | 74.75 |
| **S one-pass / S two-pass** | **0.451x** | **0.431x** | **0.499x** |
| D one-pass / D two-pass | 0.478x | 0.484x | 0.516x |

**That ratio is for the FINAL INDEX CONSTRUCTION STAGE ONLY** -- the step that takes the
capacity plan and builds the index. The round's change does not touch anything upstream of
it, so the full construction flow improves by less:

| | mixed | skew | tranco |
|---|---:|---:|---:|
| full flow, two-pass (ms) | 147.40 | 179.01 | 247.69 |
| full flow, one-pass (ms) | 95.42 | 110.34 | 163.13 |
| **full flow, one-pass / two-pass** | **0.646x** | **0.617x** | **0.657x** |

Spread of the per-process S ratio (5 processes):

| dataset | min | median | max |
|---|---:|---:|---:|
| mixed | 0.448 | 0.451 | 0.465 |
| skew | 0.368 | 0.431 | 0.436 |
| tranco | 0.354 | 0.499 | 0.552 |

Pooled over every repetition in every process (15 samples per side), the same ratio:

| dataset | two-pass median | one-pass median | ratio |
|---|---:|---:|---:|
| mixed | 96.51 | 43.70 | **0.453x** |
| skew | 118.01 | 51.55 | **0.437x** |
| tranco | 167.59 | 85.68 | **0.511x** |

## 3b. Reconciling with E14's prediction

E14 estimated the cost of the discarded half -- `s_bulk` plus the `region_all` slice -- at
**55.6%-66.3%** of the two-pass build. The reduction actually achieved in section 3 is
smaller. The split of the OLD pipeline, measured in this same process on its first
repetition, says why.

**Statistic used in every cell below: the median across processes of that process's own
value.** Differences are given twice, once each way, because the two are not the same
number and mixing them is how this section was wrong the first time.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| old `bulk_load` (ms) | 53.60 | 66.21 | 83.80 |
| old `rebuild` loop (ms) | 44.82 | 51.81 | 88.34 |
| of which `Region::all` (ms) | 11.32 | 10.55 | 12.29 |

The one-pass path still has to materialise each region's keys as a `vector<Pair>` -- it
builds the same slice `bulk_load` built. What it no longer does is construct a Region for
that slice under the default capacity. So the removable work is `bulk_load`'s
construction, while `Region::all` is not so much saved as REPLACED by the new path's own
slice; E14's `region_all` term is therefore not purely additive.

The old `rebuild` loop and the new one-pass build are the closest pair of quantities
available -- each is (materialise one region's keys) + (construct one Region under the
plan). They are a COMPARISON OF SIMILAR WORK, not the same quantity: the two materialise
their slice differently (`Region::all` runs a full `scan` merging sub-blocks and overflow;
the new path copies a contiguous range), and the old side additionally replaces and
destroys an existing Region object. Their difference locates cost; it is not required to
be zero.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| old `rebuild` loop (ms) | 44.82 | 51.81 | 88.34 |
| new one-pass build (ms) | 43.70 | 51.62 | 84.88 |
| difference of the two medians (ms) | +1.12 | +0.19 | +3.46 |
| median of per-process paired differences (ms) | +0.57 | +0.94 | +5.98 |

The two estimators differ, which is the point of showing both. In this run the old side
is more expensive by +1.12 ms (mixed), +0.19 ms (skew), +3.46 ms (tranco) by one estimator and +0.57 ms (mixed), +0.94 ms (skew), +5.98 ms (tranco) by the other. The gap is NOT stable across runs of the same comparison, which is why
it is reported rather than interpreted: on tranco an earlier run of this same sweep put
it at 13-14 ms and this one puts it at 3-6 ms. Where a gap does appear, the candidate
explanation is that `Region::all` runs a full `scan` merging sub-blocks and overflow while
the new path copies a contiguous range -- but this round does not isolate that, so it
stays a candidate.

Putting the removable work at `bulk_load` alone, against what was achieved:

| | mixed | skew | tranco |
|---|---:|---:|---:|
| predicted removable (E15 refinement) | 54.4% | 56.4% | 49.3% |
| achieved reduction | **54.9%** | **56.9%** | **50.1%** |
| difference of the two percentages (points) | +0.5 | +0.5 | +0.8 |
| median of per-process paired differences (points) | +0.0 | -0.0 | +3.5 |

Achieved is within a point of the refined estimate on all three datasets. **E14's
55.6%-66.3% should now be read as a STAGE-COST ESTIMATE, not as a bound of any kind**:
`bulk_load + region_all` itemises where the time went under the old pipeline, but the two
paths differ in slicing, object replacement and destruction, and in cache and allocator
state, so those costs are not simply subtractable. E14 had already marked the figure as an
unverified extrapolation from the code (E14 §7.2); this is that verification, and it
revises the quantity to `bulk_load` alone.

The revision is small enough not to change E14's conclusion -- the discarded half is still
the majority of the two-pass build -- but the estimate should not be quoted as
`s_bulk + region_all` going forward.

## 4. The whole pipeline, against ART

The one-pass change removes construction work; it does not remove the probe, which is
still built to obtain `group_stats()`. Both pipeline totals below include it.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| probe (ms) | 47.37 | 55.17 | 75.29 |
| stats + alloc (ms) | 3.63 | 3.19 | 4.73 |
| ART (ms) | 46.08 | 59.36 | 34.77 |
| old pipeline total (ms) | 147.40 | 179.01 | 247.69 |
| new pipeline total (ms) | 95.42 | 110.34 | 163.13 |
| old pipeline / ART | **3.28x** | **2.83x** | **7.10x** |
| new pipeline / ART | **2.12x** | **1.76x** | **4.88x** |

**ART's own absolute time varies between runs (33-59 ms here across the three datasets,
and 44-76 ms in E14), so these ratios carry more noise than the same-process A/B in
section 3. They are reported for scale, not as the round's claim.**


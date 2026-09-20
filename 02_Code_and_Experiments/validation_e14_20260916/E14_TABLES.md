# E14 tables: build-cost decomposition

Source: `results/main/e14_build.jsonl`, 30 measured records (5 processes x 3 datasets x 2 targets). Warm-up records are excluded by their run tag. Every cell is a **median across processes**.

## 1. Layout digest check

The plain target and the `-DE14_PHASE` target compute a SHA-256 over the canonical
layout serialisation. If the instrumentation were not inert this is where it shows.

| dataset | structure | distinct digests over all measured runs |
|---|---|---:|
| mixed | D | **1** |
| mixed | S | **1** |
| skew | D | **1** |
| skew | S | **1** |
| tranco | D | **1** |
| tranco | S | **1** |

**All layouts are identical across processes and across both targets.**

## 2. Stage timings (ms, median across processes)

`d_counted`/`s_counted` reproduce E12/E13's definition of a structure's build time:
`bulk_load` followed by one `rebuild_with_caps` per region. They are the numbers that
must reconcile with E12. `pipeline_total` adds the stages the same pipeline really runs
before that, so it is the honest cost of getting an S index out of a sorted key vector.

| stage | mixed | skew | tranco |
|---|---:|---:|---:|
| `probe` (plain) | 44.44 | 52.01 | 74.68 |
| `stats` (plain) | 2.38 | 2.09 | 3.37 |
| `alloc` (plain) | 0.96 | 1.01 | 1.87 |
| `d_bulk` (plain) | 44.18 | 52.68 | 74.53 |
| `d_rebuild` (plain) | 41.71 | 43.27 | 89.03 |
| `d_counted` (plain) | 86.95 | 95.08 | 163.80 |
| `s_bulk` (plain) | 71.71 | 81.13 | 91.66 |
| `s_rebuild` (plain) | 47.82 | 56.83 | 100.30 |
| `s_counted` (plain) | 116.42 | 136.60 | 192.44 |
| `pipeline_total` (plain) | 164.03 | 195.64 | 272.68 |
| `art` (plain) | 50.10 | 75.69 | 44.00 |
| `probe` (phase) | 46.51 | 54.61 | 74.50 |
| `stats` (phase) | 2.33 | 2.26 | 3.58 |
| `alloc` (phase) | 0.90 | 0.98 | 1.82 |
| `d_bulk` (phase) | 44.30 | 52.97 | 73.94 |
| `d_rebuild` (phase) | 41.38 | 41.96 | 89.92 |
| `d_counted` (phase) | 89.40 | 94.93 | 163.46 |
| `s_bulk` (phase) | 64.63 | 79.04 | 99.22 |
| `s_rebuild` (phase) | 49.11 | 59.03 | 107.52 |
| `s_counted` (phase) | 113.70 | 138.07 | 199.94 |
| `pipeline_total` (phase) | 158.48 | 194.64 | 279.69 |
| `art` (phase) | 48.76 | 73.03 | 43.92 |

## 3. What S's counted build contains

S's counted build issues **two full Region constructions per region**: the `bulk_load`
under the uniform default capacity, and then `rebuild_with_caps` under the strict plan.
The first one's only consumer is `Region::all()`, which supplies the per-region sorted
slice -- and that slice is already a contiguous range of the input `keys` vector.

| share of S's counted build | mixed | skew | tranco |
|---|---:|---:|---:|
| `s_bulk` / `s_counted` | 57.1% | 58.5% | 47.7% |
| `s_rebuild` / `s_counted` | 42.9% | 41.5% | 52.3% |
| `region_all` inside `s_rebuild` / `s_counted` | 9.0% | 8.3% | 6.7% |

`region_all` is measured only by the phase target, so its share is taken over those runs.

Adding the two rows that describe work whose result does not survive into the finished
structure -- the pre-rebuild `bulk_load` and the `region_all` slice it is immediately
replaced by -- gives a **stage-cost estimate** of that work:

| work not retained / `s_counted` (stage-cost estimate) | **66.3%** | **65.7%** | **55.6%** |

**This is an itemisation of where the time went under this pipeline, NOT a bound on what
removing it would save, and not an upper bound of any kind.** Two paths that drop a pass
differ from it in slicing, object replacement and destruction, and in cache and allocator
state, so the costs are not simply subtractable. E15 implemented the removal and measured
it: the removable quantity turned out to be `bulk_load` alone, because the replacement
path still has to materialise each region's keys, which is what `region_all` was paying
for. See E15 §2.1 / `E15_TABLES.md` §3b. Nothing in this section is retracted; only the
reading of this one row is narrowed.

## 4. The whole pipeline, against ART

E12 compared S's `s_counted` against ART's construction. But ART is built by one call,
while S needs the probe, the statistics and the allocators as well. This is the same
comparison on the full pipeline.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| S, E12/E13 definition (ms) | 116.42 | 136.60 | 192.44 |
| S, whole pipeline (ms) | 164.03 | 195.64 | 272.68 |
| ART (ms) | 50.10 | 75.69 | 44.00 |
| whole pipeline / ART | **3.49x** | **2.58x** | **6.15x** |
| `s_counted` / ART (E12's ratio) | 2.49x | 1.87x | 4.36x |

## 5. Region constructor phases (ms, median across processes)

Accumulated over every Region the stage constructs. `probe`, `d_bulk` and `s_bulk` all
run the FULL constructor; only the plan handed to it differs.

### `ph_probe`

| phase | mixed | skew | tranco |
|---|---:|---:|---:|
| `prefix` | 0.01 | 0.01 | 0.01 |
| `model` | 4.97 | 5.66 | 12.15 |
| `suffix_scan` | 0.07 | 0.07 | 0.14 |
| `demand_pass` | 0.00 | 0.00 | 0.00 |
| `layout` | 0.10 | 0.10 | 0.14 |
| `storage` | 10.31 | 9.28 | 11.33 |
| `place` | 19.21 | 24.94 | 43.68 |
| `dir` | 0.06 | 0.06 | 0.07 |
| **ctor_total** | **34.61** | **39.89** | **67.03** |

| share of `ctor_total` | mixed | skew | tranco |
|---|---:|---:|---:|
| `place` | 56.2% | 61.9% | 64.7% |
| `storage` | 28.5% | 24.0% | 16.8% |
| `demand_pass` | 0.0% | 0.0% | 0.0% |
| `layout` | 0.3% | 0.2% | 0.2% |
| `model` | 14.4% | 13.5% | 18.2% |
| `dir` | 0.2% | 0.1% | 0.1% |

### `ph_d_bulk`

| phase | mixed | skew | tranco |
|---|---:|---:|---:|
| `prefix` | 0.01 | 0.01 | 0.01 |
| `model` | 4.96 | 5.54 | 11.94 |
| `suffix_scan` | 0.07 | 0.09 | 0.12 |
| `demand_pass` | 0.00 | 0.00 | 0.00 |
| `layout` | 0.08 | 0.09 | 0.12 |
| `storage` | 9.32 | 9.25 | 11.02 |
| `place` | 19.37 | 25.24 | 43.12 |
| `dir` | 0.06 | 0.06 | 0.07 |
| **ctor_total** | **33.80** | **40.62** | **65.88** |

| share of `ctor_total` | mixed | skew | tranco |
|---|---:|---:|---:|
| `place` | 56.6% | 62.1% | 64.9% |
| `storage` | 27.6% | 23.5% | 16.5% |
| `demand_pass` | 0.0% | 0.0% | 0.0% |
| `layout` | 0.2% | 0.2% | 0.2% |
| `model` | 15.0% | 13.8% | 17.8% |
| `dir` | 0.2% | 0.1% | 0.1% |

### `ph_d_rebuild`

| phase | mixed | skew | tranco |
|---|---:|---:|---:|
| `prefix` | 0.01 | 0.01 | 0.01 |
| `model` | 3.32 | 3.28 | 8.41 |
| `suffix_scan` | 0.08 | 0.07 | 0.15 |
| `demand_pass` | 0.00 | 0.00 | 0.00 |
| `layout` | 0.07 | 0.08 | 0.12 |
| `storage` | 7.26 | 5.61 | 9.69 |
| `place` | 15.87 | 19.89 | 52.31 |
| `dir` | 0.06 | 0.06 | 0.07 |
| **ctor_total** | **26.33** | **28.79** | **70.94** |

| share of `ctor_total` | mixed | skew | tranco |
|---|---:|---:|---:|
| `place` | 58.9% | 69.0% | 74.2% |
| `storage` | 27.6% | 18.6% | 13.7% |
| `demand_pass` | 0.0% | 0.0% | 0.0% |
| `layout` | 0.3% | 0.3% | 0.2% |
| `model` | 12.3% | 11.6% | 11.3% |
| `dir` | 0.2% | 0.2% | 0.1% |

### `ph_s_bulk`

| phase | mixed | skew | tranco |
|---|---:|---:|---:|
| `prefix` | 0.01 | 0.01 | 0.01 |
| `model` | 4.45 | 4.17 | 8.39 |
| `suffix_scan` | 0.07 | 0.08 | 0.13 |
| `demand_pass` | 5.88 | 7.75 | 9.62 |
| `layout` | 0.20 | 0.19 | 0.26 |
| `storage` | 16.21 | 22.59 | 17.42 |
| `place` | 22.90 | 29.79 | 50.27 |
| `dir` | 1.62 | 1.11 | 1.36 |
| **ctor_total** | **52.35** | **64.40** | **89.87** |

| share of `ctor_total` | mixed | skew | tranco |
|---|---:|---:|---:|
| `place` | 45.2% | 45.4% | 55.9% |
| `storage` | 30.7% | 34.9% | 20.6% |
| `demand_pass` | 11.5% | 11.4% | 10.7% |
| `layout` | 0.4% | 0.3% | 0.3% |
| `model` | 8.7% | 6.2% | 9.8% |
| `dir` | 3.1% | 1.8% | 1.5% |

### `ph_s_rebuild`

| phase | mixed | skew | tranco |
|---|---:|---:|---:|
| `prefix` | 0.01 | 0.01 | 0.01 |
| `model` | 3.29 | 3.58 | 8.96 |
| `suffix_scan` | 0.06 | 0.07 | 0.14 |
| `demand_pass` | 5.83 | 7.31 | 9.42 |
| `layout` | 0.15 | 0.17 | 0.20 |
| `storage` | 6.10 | 6.32 | 9.15 |
| `place` | 17.72 | 24.32 | 52.78 |
| `dir` | 0.20 | 0.34 | 0.41 |
| **ctor_total** | **33.55** | **41.77** | **87.91** |

| share of `ctor_total` | mixed | skew | tranco |
|---|---:|---:|---:|
| `place` | 53.7% | 58.2% | 65.8% |
| `storage` | 18.0% | 14.6% | 11.2% |
| `demand_pass` | 17.4% | 17.4% | 11.2% |
| `layout` | 0.5% | 0.4% | 0.2% |
| `model` | 9.8% | 8.5% | 10.4% |
| `dir` | 0.6% | 0.8% | 0.5% |

## 6. Reconciliation: do the phases partition the constructor?

`ctor_total` is one span; the eight phases are disjoint sub-spans of it. Their sum must
therefore sit just BELOW `ctor_total`, with the shortfall attributable to whatever is not
covered by any of the eight intervals.

| stage | mixed | skew | tranco |
|---|---:|---:|---:|
| `ph_probe` | 0.9993 | 0.9994 | 0.9994 |
| `ph_d_bulk` | 0.9993 | 0.9994 | 0.9994 |
| `ph_d_rebuild` | 0.9991 | 0.9992 | 0.9994 |
| `ph_s_bulk` | 0.9995 | 0.9996 | 0.9994 |
| `ph_s_rebuild` | 0.9993 | 0.9994 | 0.9993 |

The ratios land in **0.9991-0.9996**, so the eight intervals account for essentially the
whole constructor and the decomposition is internally consistent. **The 0.04%-0.09%
shortfall is NOT attributed**: the expected cause is the timing calls themselves (~8 per
construction, ~10 ns each), but this round did not measure that, so it stays an
expectation rather than a result. It is far too small to affect any conclusion here.

What the check DID catch is worth recording: an early version measured `layout` as ONE
interval spanning the demand pass, so the demand pass was counted twice and the eight
phases summed to **116%** of `ctor_total`. Laid out as a table, that error looked
perfectly normal -- the largest phase simply read as two phases' worth.

Region constructions per stage: 64 (probe), 64 (d_bulk), 64 (d_rebuild), 64 (s_bulk), 64 (s_rebuild) -- i.e. one per region, per construction.


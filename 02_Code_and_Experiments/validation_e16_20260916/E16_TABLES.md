# E16 tables: the statistics path

Source: `results/main/e16_build.jsonl`. Timings come from the **clean** build (15 measured records = 5 processes x 3 datasets, warm-ups excluded by run tag). The footprint comes from a separate instrumented build (9 records, tagged MEM). The two are kept apart deliberately -- see section 5.

**Statistic used in every cell: the median across processes of that process's own value.**

Ratios are given BOTH ways, because the two are not the same number and mixing them is a
recorded failure mode in this project (E15 §3b):

* **ratio of the medians** -- median(a) / median(b), the two displayed rows divided
* **median of the per-process ratios** -- each process's own a/b, then the median

## 1. Structure unchanged, against E15's recorded digest

E16 replaces the probe with `Index::demand_profile`, which produces the same
`vector<GroupStats>` without allocating blocks. The claim that this does not change the
built structure is checked two ways: inside each run (the two statistics paths are
compared before any timing), and against E15's RECORDED digest for the same key set.

The digest serialisation is E15's, including the SHA-256 self-test, so the two values are
the same object.

| dataset | structure | inside-run agreement | equals E15's recorded digest |
|---|---|---|---|
| mixed | S | **yes** | **yes** |
| skew | S | **yes** | **yes** |
| tranco | S | **yes** | **yes** |

**Replacing the probe leaves the built structure unchanged.**

## 2. The statistics path is faithful, field by field

`check_e16_probe.cpp` compares `demand_profile` against a real probe in three layers:

| layer | what is compared |
|---|---|
| statistics | **every field** of every `GroupStats` -- not just the four the allocators read -- for every group of every region |
| plan | the D and S capacity plans element by element, plus the S allocator's own decision log (candidates / funded / unfunded / slack / shaved) |
| structure | the final D and S indexes built from each plan, by structural digest |

Configurations vary along every axis the statistics depend on: region size {32, 64, 2048}, slots {1, 2, 16, 48}, block bytes {256, 1024, 4096}, load {0.5, 0.75}, canonical route {Full, Interval, Residual}, model kind {Context16, Position16, Unigram}, over key sets including empty, single-key and two-key ranges. A multi-block Config, which `demand_profile` refuses rather than approximates, is asserted to throw.

Result: `passed` — 2700 configurations, **1,134,675 group statistics compared field by field**, 23,580 plan checks, 5,400 structure checks.

## 3. Timings (ms)

`probe` is `probe.bulk_load(keys)` followed by `group_stats()` for every region — what the
pipeline did before. `light` is `Index::demand_profile(keys)`. `plan` is the two allocators.
`build` is E15's one-pass `bulk_load_with_plan`. The flow totals are `statistics + plan + build`.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| statistics: probe (full build) | 35.83 | 39.73 | 63.31 |
| statistics: demand_profile | 25.98 | 28.75 | 21.98 |
| plan, from the probe | 0.92 | 1.06 | 1.78 |
| plan, from demand_profile | 0.93 | 1.02 | 1.77 |
| build (one-pass, E15) | 45.17 | 53.09 | 88.62 |
| flow: probe path | 82.07 | 94.55 | 152.80 |
| flow: demand_profile path | 72.08 | 82.94 | 112.99 |
| statistics phase: light / probe, median of per-process ratios | **0.725x** | **0.714x** | **0.358x** |
| statistics phase: light / probe, ratio of the medians | 0.725x | 0.724x | 0.347x |
| whole flow: light / probe, median of per-process ratios | **0.884x** | **0.877x** | **0.737x** |
| whole flow: light / probe, ratio of the medians | 0.878x | 0.877x | 0.739x |

The whole flow improves by less than the statistics phase, because the build does not
shrink and it is now the majority of the flow. That is the honest headline: this round
removed the probe, not the build.

## 4. Transient footprint of the statistics phase (MB)

From the SEPARATE instrumented build (`bench_e16_mem`, `-DE16_MEM`).
Peak live heap during the statistics phase, from wrapped allocations.
Accounting runs for the whole process (see the comment on `PeakScope`: a scope-local
counter would see allocations made outside it and be dragged negative by their frees).
`probe memory()` is the prototype's own accounting for the probe's structure, shown as a
calibration — it should be the same order as the measured peak.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| probe path, peak delta | 38.34 | 44.04 | 38.28 |
| demand_profile path, peak delta | 2.24 | 2.13 | 2.31 |
| calibration: `probe.memory()` | 35.36 | 40.85 | 37.47 |
| light / probe, ratio of the medians | **0.058x** | **0.048x** | **0.060x** |

Both paths must produce the same `vector<GroupStats>`, so that part is common; the
difference is the structure the probe builds to obtain them.

The calibration row is not decoration. Wrapping `malloc` alone undercounted the probe
path by roughly half (16.4 MB measured against 35.4 MB accounted), because Region storage
comes from `::operator new(size, align_val_t)`, which goes through an ALIGNED allocation
rather than malloc. Adding `aligned_alloc` and `posix_memalign` brought the measured peak
to the same order as the prototype's own accounting, which is what makes the number
trustworthy rather than merely precise.

## 5. Why the timing build carries no accounting

The first version of this round enabled allocator accounting for the whole process and
timed inside it. That inflates exactly the path the round is meant to show is expensive:
the probe path allocates far more than the statistics path (block arenas, overflow
containers, one string per overflow record), so per-allocation bookkeeping lands
disproportionately on it.

Measured directly, on the same dataset, clean build against instrumented build:

| dataset | phase | clean (ms) | instrumented (ms) | instrumented / clean |
|---|---|---:|---:|---:|
| mixed | probe | 35.83 | 40.90 | 1.142x |
| mixed | demand_profile | 25.98 | 26.13 | 1.006x |
| skew | probe | 39.73 | 40.73 | 1.025x |
| skew | demand_profile | 28.75 | 27.97 | 0.973x |
| tranco | probe | 63.31 | 60.83 | 0.961x |
| tranco | demand_profile | 21.98 | 21.43 | 0.975x |

**Every timing number in section 3 therefore comes from a build with no wrappers and no
accounting at all.** The instrumented build is used for the footprint only.

Read honestly: the effect is **1.142x on mixed/probe** and within
run-to-run noise on the rest. Three instrumented runs against a five-process clean median
is not enough to put a number on it, and this round does not try to.

The split does not depend on that number being large. A timed region should not carry
bookkeeping whose cost scales with the allocation count, because the two paths being
compared differ by orders of magnitude in exactly that count -- which is what makes the
result sensitive to the instrument, in a direction that flatters the change. The clean
build removes that dependence entirely rather than relying on it being small.

The footprint itself should be read as **peak requested heap bytes under the allocation
interfaces that were covered** -- not RSS, and not including allocator metadata. The
`probe.memory()` calibration row shows the two are the same order, which is a consistency
check, not a proof that no allocation interface was missed.


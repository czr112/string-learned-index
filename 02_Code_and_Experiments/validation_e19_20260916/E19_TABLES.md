# E19 tables: R1 -- reusing the group ids inside the construction

Source: `results/main/e19_build.jsonl`. Headline timings from the CLEAN build (15 records = 5 processes x 3 datasets). Prediction counts and allocation figures come from the separate INSTRUMENTED build (9 records, tagged DIAG).

**Statistic: the median across processes of that process's own value.** Ratios are the
median of the per-process paired ratios, and their spread is shown.

## 1. Gate: `check_e19_reuse`

| what is checked | scale |
|---|---|
| structure digest identical with the flag off and on | over a grid of region sizes, slot counts, block sizes, routes and model kinds |
| capacity plan identical | the plan comes from the probe, which R1 does not touch -- asserted so a later change that moved statistics into the construction is caught |
| query results identical | a workload against both, in both locate and scan modes, plus a full scan |
| prediction count drops by exactly one per key | and by zero when `multiblock` is off |
| the fallback works | reuse requested with an empty key set falls back rather than indexing an empty vector |

Result: `passed` — 96 configurations, 96 digest checks, 96 plan checks, **38,400 query comparisons**, 96 prediction-count checks.

The digest is also compared with E15's RECORDED value, so the gate establishes both
"reuse changes nothing" and "nothing has drifted since E15".

**What the guard does and does not guarantee.** The array is produced and
consumed inside one constructor, with the key order and the model unchanged --
that is the correctness argument. The length check is a cheap screen, not a
proof: it catches some breakages and cannot make an arbitrary future change
safe to fall back from.
| dataset | clean build matches E15 | instrumented build matches E15 |
|---|---|---|
| mixed | **yes** | **yes** |
| skew | **yes** | **yes** |
| tranco | **yes** | **yes** |

**R1 leaves the structure exactly as E15 recorded it.**

## 2. Model predictions on the final path

R1 removes the second prediction the construction was making. It does not remove the
statistics stage's prediction, and it does not touch the fingerprint -- so the total goes
**from three per key to two, not to one**.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| prediction calls, reuse OFF (construction stage) | 262,144 | 262,144 | 524,288 |
| prediction calls, reuse ON (construction stage) | 131,072 | 131,072 | 262,144 |
| **calls removed** | **131,072** (2.00 -> 1.00 per key) | **131,072** (2.00 -> 1.00 per key) | **262,144** (2.00 -> 1.00 per key) |

On the whole final path the count is **1 (statistics) + 2 (construction) = 3 per key
before, and 1 + 1 = 2 per key after.** The fingerprint is unchanged at one per key.

## 3. Clean-build A/B (ms)

Both variants build the same final index under the same capacity plan, in the same
process, alternating which runs first, five repetitions each.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| final flow, reuse OFF (ms) | 71.41 | 83.34 | 106.37 |
| final flow, reuse ON (ms) | 66.33 | 75.25 | 102.24 |
| **ON / OFF** | **0.9210x** | **0.9145x** | **0.9646x** |

Spread over the five processes:

| dataset | min | median | max | processes below 1 |
|---|---:|---:|---:|---:|
| mixed | 0.9090 | 0.9210 | 0.9356 | 5/5 |
| skew | 0.8840 | 0.9145 | 0.9270 | 5/5 |
| tranco | 0.8794 | 0.9646 | 1.0104 | 4/5 |

**R1 reduces the final construction flow by roughly 3.5%-8.6%.** Two of the three
datasets are below 1 in every process; `tranco` has one process above 1, so its spread
straddles the line and its point estimate should be read with that in mind.

## 4. What R1 does not save

* **The fingerprint.** It is computed once per key, in placement only. The counters show
  `fingerprint_calls` = 1 per key and `group_calls` = 2 per key in the construction
  stage, so exactly one group prediction is removed and **none of the fingerprint work
  in `grp_prep` is claimed**.
* **The statistics stage's prediction.** Cross-stage reuse is R2 and is not attempted here.
* **The rest of `demand_pass`.** It also fills two per-group accumulators and walks the
  key array; only its model call disappears. The E18 phase figure for
  `grp_prep` + `demand_pass` is therefore an **optimisation-potential indicator, not a
  bound**: it counts loop, array and capacity-state work that R1 does not remove.

  **The two are not on the same denominator and must not be subtracted.** The E18
  figure is a share of the CONSTRUCTION stage; the reduction measured here is a share
  of the WHOLE FLOW. Quantifying the shortfall would need both measured over the same
  interval, which this round did not do. What holds is the direction: a phase share is
  not a bound.
* **Anything upstream or downstream.** Capacity planning and the statistics themselves are
  untouched.

## 5. Temporary memory and allocation counts (INSTRUMENTED BUILD)

The recorded ids are per region -- `region_keys` entries at most -- and are released when
the constructor returns, so the peak should barely move.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| peak transient heap, reuse OFF (MB) | 24.54 | 24.34 | 26.43 |
| peak transient heap, reuse ON (MB) | 24.54 | 24.35 | 26.44 |
| difference (MB) | +0.01 | +0.01 | +0.01 |

| | mixed | skew | tranco |
|---|---:|---:|---:|
| allocation calls, reuse OFF | 135,581 | 145,290 | 76,149 |
| allocation calls, reuse ON | 135,645 | 145,354 | 76,277 |
| **difference** | **+64** | **+64** | **+128** |

The extra allocations are exactly one per region (64 for the 131,072-key sets, 128 for
the 262,144-key set) -- the id array itself, allocated once per region and freed with it.
The peak is unchanged to two decimal places, so **the cache costs no measurable memory**
at this granularity. The wrappers cover `malloc/calloc/realloc/aligned_alloc/
posix_memalign/free`; the figure is peak requested heap under those interfaces, not RSS.

**These are instrumented numbers and are not used for any headline figure.**


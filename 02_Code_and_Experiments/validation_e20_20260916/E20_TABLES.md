# E20 tables: R2 against R1 against ART, in one process

Source: `results/main/e20_build.jsonl`. Headline timings from the CLEAN build (15 records = 5 processes x 3 datasets). Counters and memory figures come from the instrumented build (9 records, DIAG).

| variant | what it is |
|---|---|
| **V1 / R1** | statistics -> plan -> construction, reusing ids WITHIN the construction |
| **V2 / R2** | the same, with the statistics stage's ids, prefix and group count handed across |
| **V3 / ART** | armong/libart built in this environment |

**Timing endpoints are unified**: each variant times everything it must do to turn the
sorted key set into a queryable index. Nothing sits outside anyone's span.

**Statistic: the median across processes of that process's own value.** Ratios are the
median of the per-process paired ratios, with their spread.

## 1. Gate: `check_e20_r2`

| what is checked | why |
|---|---|
| structure digest, prefix and logical group count identical for R1 and R2 | the change must not alter the layout |
| capacity plan identical | asserted so a later move of statistics into the construction is caught |
| query results identical | locate and scan modes, plus a full scan |
| the construction's predictions go 1/key -> **ZERO** | R2 removes both, not one |
| **a MISMATCHED context is refused -- three ways** | (i) same keys, different slot count: same id COUNT, different logical group count, so a length check alone accepts it; (ii) same keys, same prefix and group count, DIFFERENT MODEL KIND; (iii) DIFFERENT KEYS with the same prefix and group count |
| (ii) and (iii) are why the context carries a model digest | prefix and group count alone accept both, and either would let the construction place records by a stale mapping |

Result: `passed` — 48 configurations, 48 digest checks, 48 plan checks, **19,200 query comparisons**, 48 prediction-count checks.

Guard: **48 group-count mismatches** refused, **48 model-kind mismatches** refused (every configuration exercised this one), **16 key-set mismatches** refused. The key-set case is exercised only where the modified key set happened to keep the same prefix and group count; elsewhere the earlier prefix/group check refuses it first.

| dataset | clean build matches E15's recorded digest |
|---|---|
| mixed | **yes** |
| skew | **yes** |
| tranco | **yes** |

ART answered 20,000 sampled queries with the same `found`/`value` as the prototype in every
run — a sanity check on the external reference, not the correctness claim.

**R2 leaves the structure exactly as E15 recorded it.**

## 2. Model predictions on the whole final path

| | mixed | skew | tranco |
|---|---:|---:|---:|
| prediction calls, R1 | 262,144 | 262,144 | 524,288 |
| prediction calls, R2 | 131,072 | 131,072 | 262,144 |
| **per key** | **2.00 -> 1.00 per key** | **2.00 -> 1.00 per key** | **2.00 -> 1.00 per key** |

R1 leaves two per key on the whole path (one in statistics, one in the construction); R2
leaves **one**, because the construction makes none. **R2 does not remove the model
training, and no training saving is claimed.**

## 3. Times (ms) -- CLEAN BUILD

| variant | mixed | skew | tranco |
|---|---:|---:|---:|
| V1  R1 (ms) | 86.13 | 97.52 | 138.36 |
| V2  R2 (ms) | 91.99 | 99.11 | 127.05 |
| V3  ART (ms) | 52.94 | 75.77 | 36.46 |

| **R2 / R1** | **0.979x** | **0.941x** | **0.923x** |
| ART / R1 | 0.594x | 0.746x | 0.265x |
| **ART / R2** | **0.642x** | **0.805x** | **0.279x** |

Spread of the per-process `R2 / R1`:

| dataset | min | median | max | processes below 1 |
|---|---:|---:|---:|---:|
| mixed | 0.8612 | 0.9790 | 1.1235 | 3/5 |
| skew | 0.8728 | 0.9413 | 0.9528 | 5/5 |
| tranco | 0.9207 | 0.9226 | 0.9791 | 5/5 |

**R2 removes a further 2.1%-7.7% of the final flow, on top of R1**
(per dataset: `mixed` 2.1%, `skew` 5.9%, `tranco` 7.7%).

**Processes below 1: `mixed` 3/5, `skew` 5/5, `tranco` 5/5.** Where that is not all of them, **the direction is not established in every
process** and the point estimate should be read with the spread above.

**Every reduction quoted anywhere in this round is `1 - R2/R1` from the PAIRED per-process
ratios**, never from dividing the two displayed medians. The two estimators give different
numbers and must not be mixed.

**Against ART, on the same round and the same endpoints.** The quantity below is
`T_R2 / T_ART` -- the final prototype's flow divided by ART's, so a value above 1 means the
prototype is slower:
**The frozen default is R1, so its own ART score is reported first.**

| **T_R1 / T_ART** (FROZEN DEFAULT flow / ART flow) | **1.68x** | **1.34x** | **3.77x** |
| its spread over the five processes | 1.57-1.82 | 1.26-1.38 | 3.63-4.39 |
| T_R2 / T_ART (optional path flow / ART flow) | **1.56x** | **1.24x** | **3.58x** |
| its spread over the five processes | 1.44-2.08 | 1.11-1.32 | 3.20-3.73 |

**After all the construction work, BOTH paths still build more slowly than
armong/libart on every dataset.** The frozen default (R1) is 1.34x-3.77x ART's flow;
the optional path (R2) is 1.24x-3.58x. **R2's ART ratios may only be quoted as an
optional-path result** — the frozen default's build score is the R1 row.
This is the same-round, same-endpoint comparison; the older E15-round figures are not
subtracted from it.

## 4. Transient peak and allocation counts (INSTRUMENTED BUILD)

| | mixed | skew | tranco |
|---|---:|---:|---:|
| peak, R1 (MB) | 27.06 | 26.74 | 29.04 |
| peak, R2 (MB) | 27.33 | 27.02 | 29.81 |
| peak, ART (MB) | 22.26 | 27.70 | 19.16 |
| R2 - R1 (MB) | +0.26 | +0.28 | +0.77 |

| | mixed | skew | tranco |
|---|---:|---:|---:|
| allocations, R1 | 136,913 | 146,998 | 79,333 |
| allocations, R2 | 136,963 | 147,048 | 79,445 |
| R2 - R1 | +50 | +50 | +112 |

**The peak rises by one `uint32` per key on each dataset** (+0.52 / +0.53 / +1.05 MB),
because the id array now lives from the statistics stage through the plan and the
construction instead of inside one constructor.

**That is an observation of this round, not a general guarantee.** The figure follows from
*where* the array is allocated and *when* it is released on this single-threaded build path.
A build that releases it earlier, or that constructs regions concurrently, would show a
different peak -- the mechanism (shorter or longer lifetime) is what generalises, not the
number.

**The allocation COUNT is unchanged, and that is not an accident.** R1 allocates one id
array per region inside the construction; R2 allocates one per region in the statistics
stage and the construction allocates none, because the preset is accepted. The array moves
between stages; only its LIFETIME changes. A direct probe of the two entry points
(`probe_alloc`) confirms the statistics stage really does allocate them: 473 calls for
`demand_profile`, 537 for `demand_profile_ctx`, +64 = one per region, and 131,072 ids in
total = one per key.

ART's peak and allocation counts are shown for scale only. Its allocations are ~2x-5x the
prototype's, which is a structural difference between the two designs, not a finding of
this round.

**These are instrumented numbers and are not used for any headline figure.**


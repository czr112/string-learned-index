# E24 tables: update path, space decomposition, reclaim opportunity

Source: `results/main/e24_diag.jsonl`, 12 records (3 datasets x 4 states).

**Counts, not timings.** E24 takes no timings and uses fixed seeds, so these
numbers are reproducible by re-running the diagnostic. There is no noise floor
to report and no cross-process aggregation.

Test queries are answered against the reference map, never against the index
under test, and a probe whose answers disagree fails the run. So a row existing
at all means every probe in it was cross-checked.

**These are three separately-run workloads, not one mixed workload.** The rows
for `update_only` / `insert_only` / `replace25` are NOT a time decomposition of
a mixed update stream; each is its own state, built from its own fresh initial
state and compared at the same counter checkpoint.

## 1. What each path walks, split by where `find` put the key

`find` uses the fingerprint tag filter, the length pre-check, the boundary
directory and the overflow Hybrid. `update` and `erase` use **none** of them:
they walk every sub-block and every entry with a full-key compare.

### mixed / `initial`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 4019 | 1.274 | 70.4 | 68.3 | **55x** |
| overflow | 24 | 1.667 | 216.2 | 209.4 | **130x** |
| miss | 4096 | 0.004 | 141.8 | 137.4 | **34155x** |

### mixed / `update_only`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 4019 | 1.274 | 70.4 | 68.3 | **55x** |
| overflow | 24 | 1.667 | 216.2 | 209.4 | **130x** |
| miss | 4096 | 0.004 | 141.8 | 137.4 | **34155x** |

### mixed / `insert_only`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 4019 | 1.274 | 70.4 | 68.3 | **55x** |
| overflow | 24 | 2.333 | 259.6 | 252.8 | **111x** |
| miss | 4096 | 0.436 | 171.6 | 167.3 | **394x** |

### mixed / `replace25`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 3037 | 1.249 | 67.5 | 65.5 | **54x** |
| overflow | 22 | 2.000 | 202.6 | 197.1 | **101x** |
| miss | 5080 | 0.687 | 136.4 | 133.1 | **199x** |

### skew / `initial`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 3904 | 1.569 | 145.6 | 141.0 | **93x** |
| overflow | 139 | 1.237 | 66.7 | 64.6 | **54x** |
| miss | 4096 | 0.012 | 286.6 | 277.6 | **23957x** |

### skew / `update_only`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 3904 | 1.569 | 145.6 | 141.0 | **93x** |
| overflow | 139 | 1.237 | 66.7 | 64.6 | **54x** |
| miss | 4096 | 0.012 | 286.6 | 277.6 | **23957x** |

### skew / `insert_only`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 3904 | 1.569 | 145.6 | 141.0 | **93x** |
| overflow | 139 | 2.050 | 82.9 | 80.9 | **40x** |
| miss | 4096 | 4.169 | 360.8 | 351.8 | **87x** |

### skew / `replace25`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 2956 | 1.422 | 108.6 | 105.2 | **76x** |
| overflow | 103 | 1.854 | 63.2 | 61.8 | **34x** |
| miss | 5080 | 4.782 | 290.6 | 283.9 | **61x** |

### tranco / `initial`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 3878 | 1.010 | 34.3 | 33.8 | **34x** |
| overflow | 195 | 1.056 | 43.5 | 42.9 | **41x** |
| miss | 4096 | 0.011 | 68.3 | 67.2 | **5949x** |

### tranco / `update_only`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 3878 | 1.010 | 34.3 | 33.8 | **34x** |
| overflow | 195 | 1.056 | 43.5 | 42.9 | **41x** |
| miss | 4096 | 0.011 | 68.3 | 67.2 | **5949x** |

### tranco / `insert_only`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 3878 | 1.010 | 34.4 | 33.8 | **34x** |
| overflow | 195 | 1.072 | 54.4 | 53.8 | **51x** |
| miss | 4096 | 0.098 | 85.3 | 84.2 | **871x** |

### tranco / `replace25`

| where | keys | find | update | erase | update / find |
|---|---:|---:|---:|---:|---:|
| primary | 2886 | 1.009 | 28.8 | 28.3 | **29x** |
| overflow | 137 | 1.124 | 41.1 | 40.6 | **37x** |
| miss | 5146 | 0.092 | 68.8 | 68.0 | **750x** |

## 2. Space, in the two layers

### 2.1 Main-block ledger (bytes)

The block allocation covers ONLY block storage. Within a sub-block,
`bb = sizeof(Header) + slots*sizeof(Entry) + suffix_cap`, so
`header + descriptor + suffix_cap` must equal `blocks`. `closes` reports that.

| dataset | state | header | descriptor | suffix cap | **sum** | blocks | closes |
|---|---|---:|---:|---:|---:|---:|:--:|
| mixed | `initial` | 1426688 | 2332784 | 17995920 | **21755392** | 21755392 | yes |
| mixed | `update_only` | 1426688 | 2332784 | 17995920 | **21755392** | 21755392 | yes |
| mixed | `insert_only` | 1426688 | 2332784 | 17995920 | **21755392** | 21755392 | yes |
| mixed | `replace25` | 1426688 | 2332784 | 17995920 | **21755392** | 21755392 | yes |
| skew | `initial` | 1457728 | 2420400 | 16763856 | **20641984** | 20641984 | yes |
| skew | `update_only` | 1457728 | 2420400 | 16763856 | **20641984** | 20641984 | yes |
| skew | `insert_only` | 1457728 | 2420400 | 16763856 | **20641984** | 20641984 | yes |
| skew | `replace25` | 1457728 | 2420400 | 16763856 | **20641984** | 20641984 | yes |
| tranco | `initial` | 1547904 | 4402880 | 16203136 | **22153920** | 22153920 | yes |
| tranco | `update_only` | 1547904 | 4402880 | 16203136 | **22153920** | 22153920 | yes |
| tranco | `insert_only` | 1547904 | 4402880 | 16203136 | **22153920** | 22153920 | yes |
| tranco | `replace25` | 1547904 | 4402880 | 16203136 | **22153920** | 22153920 | yes |

Note: unused SLOTS are inside `descriptor` (the slots are preallocated), so they
are not added again; `suffix cap` is the whole suffix area, live + free + garbage.

### 2.2 Main-block suffix area (MB) and slot use

`garbage` = bytes inside the occupied region that no live entry references --
suffixes left behind by `erase`, which only decrements the count.

| dataset | state | live | free | garbage | slots used |
|---|---|---:|---:|---:|---:|
| mixed | `initial` | 10.10 | 7.06 | 0.00 | 89.4% |
| mixed | `update_only` | 10.10 | 7.06 | 0.00 | 89.4% |
| mixed | `insert_only` | 10.10 | 7.06 | 0.00 | 89.4% |
| mixed | `replace25` | 9.25 | 5.55 | 2.36 | 81.1% |
| skew | `initial` | 9.63 | 6.36 | 0.00 | 83.4% |
| skew | `update_only` | 9.63 | 6.36 | 0.00 | 83.4% |
| skew | `insert_only` | 9.63 | 6.35 | 0.00 | 83.5% |
| skew | `replace25` | 7.60 | 6.61 | 1.78 | 62.7% |
| tranco | `initial` | 2.89 | 12.56 | 0.00 | 91.0% |
| tranco | `update_only` | 2.89 | 12.56 | 0.00 | 91.0% |
| tranco | `insert_only` | 2.90 | 12.55 | 0.00 | 91.1% |
| tranco | `replace25` | 2.83 | 11.98 | 0.65 | 80.8% |

### 2.3 Overflow: payload vs retained capacity

`elem cap` is the record array's reserved bytes (capacity x sizeof(OverflowRecord));
`elem used` is what the live records need. The difference is capacity the
container still holds after records were erased. Suffix strings are counted
separately (`suffix live` / `suffix cap`) and are NOT folded into `elem`.

| dataset | state | records | suffix live | suffix cap | elem used | elem cap | elem slack |
|---|---|---:|---:|---:|---:|---:|---:|
| mixed | `initial` | 751 | 71249 | 72000 | 36048 | 47232 | **11184** |
| mixed | `update_only` | 751 | 71249 | 72000 | 36048 | 47232 | **11184** |
| mixed | `insert_only` | 33486 | 2961248 | 2994734 | 1607328 | 2079696 | **472368** |
| mixed | `replace25` | 12877 | 1243445 | 1256322 | 618096 | 796176 | **178080** |
| skew | `initial` | 4901 | 435210 | 440111 | 235248 | 317376 | **82128** |
| skew | `update_only` | 4901 | 435210 | 440111 | 235248 | 317376 | **82128** |
| skew | `insert_only` | 37578 | 3176556 | 3214134 | 1803744 | 2573856 | **770112** |
| skew | `replace25` | 36178 | 2849768 | 2885946 | 1736544 | 2451984 | **715440** |
| tranco | `initial` | 11759 | 147258 | 196109 | 564432 | 734976 | **170544** |
| tranco | `update_only` | 11759 | 147258 | 196109 | 564432 | 734976 | **170544** |
| tranco | `insert_only` | 76963 | 1514545 | 1630606 | 3694224 | 5004336 | **1310112** |
| tranco | `replace25` | 39817 | 795510 | 865181 | 1911216 | 2682576 | **771360** |

### 2.4 Separately listed, and RSS

`memory_breakdown()` is the index's own requested bytes. RSS is what the process
actually holds. These are DIFFERENT accounting bases and are reported apart.

| dataset | state | model | directory | tables | fences+self | peak RSS (MB) |
|---|---|---:|---:|---:|---:|---:|
| mixed | `initial` | 1.01 | 0.65 | 0.28 | 0.03 | 170.8 |
| mixed | `update_only` | 1.01 | 0.65 | 0.28 | 0.03 | 170.8 |
| mixed | `insert_only` | 1.01 | 0.65 | 0.28 | 0.03 | 180.9 |
| mixed | `replace25` | 1.01 | 0.65 | 0.28 | 0.03 | 180.9 |
| skew | `initial` | 1.01 | 0.87 | 0.27 | 0.03 | 207.8 |
| skew | `update_only` | 1.01 | 0.87 | 0.27 | 0.03 | 208.2 |
| skew | `insert_only` | 1.01 | 0.87 | 0.27 | 0.03 | 221.3 |
| skew | `replace25` | 1.01 | 0.86 | 0.27 | 0.03 | 221.3 |
| tranco | `initial` | 2.02 | 0.72 | 0.29 | 0.05 | 139.1 |
| tranco | `update_only` | 2.02 | 0.72 | 0.29 | 0.05 | 139.1 |
| tranco | `insert_only` | 2.02 | 0.72 | 0.29 | 0.05 | 153.0 |
| tranco | `replace25` | 2.02 | 0.73 | 0.29 | 0.05 | 153.0 |

## 3. Reclaim opportunity, and where the free space actually sits

A record may move back only into the ONE sub-block its key order selects (M1),
and only if THAT sub-block has a free slot AND room for its length. `fits any`
is what the existing coarse predicate (`group_has_room`) would claim instead.

| dataset | state | overflow records | groups | **fits (ordered)** | blocked: slots | blocked: bytes | blocked: order | fits any sub-block |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| mixed | `initial` | 751 | 155 | **0** | 0 | 751 | 0 | 0 |
| mixed | `update_only` | 751 | 155 | **0** | 0 | 751 | 0 | 0 |
| mixed | `insert_only` | 33486 | 6752 | **0** | 27073 | 6413 | 0 | 0 |
| mixed | `replace25` | 12877 | 3318 | **0** | 819 | 12033 | 25 | 1089 |
| skew | `initial` | 4901 | 607 | **0** | 0 | 4901 | 0 | 0 |
| skew | `update_only` | 4901 | 607 | **0** | 0 | 4901 | 0 | 0 |
| skew | `insert_only` | 37578 | 953 | **0** | 13941 | 23637 | 0 | 0 |
| skew | `replace25` | 36178 | 826 | **0** | 4564 | 31481 | 133 | 4910 |
| tranco | `initial` | 11759 | 1104 | **0** | 903 | 10856 | 0 | 537 |
| tranco | `update_only` | 11759 | 1104 | **0** | 903 | 10856 | 0 | 537 |
| tranco | `insert_only` | 76963 | 8836 | **0** | 52688 | 24275 | 0 | 593 |
| tranco | `replace25` | 39817 | 1732 | **256** | 2468 | 36952 | 141 | 4640 |

**Is the free space in the record's own group?** A group that holds overflow AND
has both a free slot and free suffix bytes could in principle host something.
Free space in groups that hold NO overflow cannot serve overflow records at all.

| dataset | state | groups with overflow **and** room | free slots in groups without overflow | free bytes in groups without overflow |
|---|---|---:|---:|---:|
| mixed | `initial` | 152 | 14332 | 7358205 |
| mixed | `update_only` | 152 | 14332 | 7358205 |
| mixed | `insert_only` | 173 | 14278 | 5018842 |
| mixed | `replace25` | 3219 | 14292 | 5505435 |
| skew | `initial` | 594 | 19326 | 6567418 |
| skew | `update_only` | 594 | 19326 | 6567418 |
| skew | `insert_only` | 651 | 19178 | 6518013 |
| skew | `replace25` | 792 | 19193 | 6538810 |
| tranco | `initial` | 982 | 12543 | 13120769 |
| tranco | `update_only` | 982 | 12543 | 13120769 |
| tranco | `insert_only` | 1153 | 12017 | 8625439 |
| tranco | `replace25` | 1603 | 12147 | 12483263 |


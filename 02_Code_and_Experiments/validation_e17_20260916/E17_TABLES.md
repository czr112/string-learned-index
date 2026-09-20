# E17 tables: the three construction paths in one process

Source: `results/main/e17_build.jsonl`, 15 measured records (5 processes x 3 datasets). Warm-ups excluded by tag.

**Statistic used in every cell: the median across processes of that process's own value.**
Ratios are given both ways -- median of the per-process ratios, and ratio of the medians.

| dataset | keys |
|---|---:|
| `mixed` | 131,072 |
| `skew` | 131,072 |
| `tranco` | 262,144 |

(`tranco` is the Tranco list as the data SOURCE; only the first 262,144 rows are used.
Nothing here tests a million keys.)

## 1. Digest gate

Each run builds S three ways and compares the digests BEFORE reporting any timing; a
mismatch aborts the run, so a record existing at all means they agreed. The serialisation
is E14/E15/E16's, including the SHA-256 self-test, so it can also be compared with the
digest the earlier rounds recorded.

| dataset | distinct digests inside E17 | equals E15's recorded digest |
|---|---:|---|
| mixed | **1** | **yes** |
| skew | **1** | **yes** |
| tranco | **1** | **yes** |

**All three paths build the same structure, and it is the one the earlier rounds recorded.**

## 2. Times (ms)

* **B — probe + plan + build-then-rebuild (original)**
* **A — probe + plan + direct build (E15)**
* **C — demand_profile + plan + direct build (E16, final)**

Each repetition runs all three in a ROTATED order, so position within the round cannot
favour one path; ratios are formed within a repetition and then aggregated.

| path | mixed | skew | tranco |
|---|---:|---:|---:|
| B — probe + plan + build-then-rebuild (original) | 137.76 | 152.84 | 243.89 |
| A — probe + plan + direct build (E15) | 78.60 | 93.51 | 154.73 |
| C — demand_profile + plan + direct build (E16, final) | 69.24 | 80.85 | 109.28 |

## 3. Ratios

| | mixed | skew | tranco |
|---|---:|---:|---:|
| **A / B** (isolates E15), median of per-process ratios | **0.576x** | **0.577x** | **0.630x** |
| **A / B** (isolates E15), ratio of the medians | 0.571x | 0.612x | 0.634x |
| **C / A** (isolates E16, on E15's path), median of per-process ratios | **0.863x** | **0.875x** | **0.713x** |
| **C / A** (isolates E16, on E15's path), ratio of the medians | 0.881x | 0.865x | 0.706x |
| **C / B** (the two together), median of per-process ratios | **0.496x** | **0.526x** | **0.453x** |
| **C / B** (the two together), ratio of the medians | 0.503x | 0.529x | 0.448x |

**`C / B` is the number the earlier rounds could not supply.** E15 measured A/B in its own
round and E16 measured C/A in another; neither is the same observation of A, so their
product is not C/B. It is measured directly here.

Spread of the per-process `C / B` (5 processes):

| dataset | min | median | max |
|---|---:|---:|---:|
| mixed | 0.444 | 0.496 | 0.528 |
| skew | 0.498 | 0.526 | 0.536 |
| tranco | 0.422 | 0.453 | 0.456 |

Pooled over every repetition in every process (25 samples per side):

| dataset | B median | A median | C median | A/B | C/A | **C/B** |
|---|---:|---:|---:|---:|---:|---:|
| mixed | 136.87 | 78.71 | 69.24 | 0.575 | 0.880 | **0.506** |
| skew | 155.04 | 93.51 | 79.50 | 0.603 | 0.850 | **0.513** |
| tranco | 249.30 | 156.20 | 109.28 | 0.627 | 0.700 | **0.438** |

Ratios of pooled medians are not the median of per-process ratios. Both are shown because
they are different quantities that happen to agree closely here -- up to about 0.015 apart


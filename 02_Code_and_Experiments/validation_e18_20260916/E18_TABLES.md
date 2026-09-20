# E18 tables: where the final construction path spends its time

Source: `results/main/e18_build.jsonl`. Headline timings from the CLEAN build (15 records = 5 processes x 3 datasets, warm-ups excluded). The construction's internal partition and the activity counters come from the separate INSTRUMENTED build (9 records, tagged DIAG) and are used nowhere else.

**Statistic: the median across processes of that process's own value.**
**Proportions are computed WITHIN a repetition and then aggregated** -- never by dividing
one median by another, and never by adding stage medians together.

## 1. Digest gate

`-DE18_PREPASS` reorders the construction's placement loop -- group id and fingerprint are
computed in one pre-pass instead of inline. Both are pure functions of the key, so the
placement decisions must be identical. The digest is the check, and it is also compared
with E15's recorded value.

| dataset | clean build matches E15 | instrumented build matches E15 |
|---|---|---|
| mixed | **yes** | **yes** |
| skew | **yes** | **yes** |
| tranco | **yes** | **yes** |

**The instrumented build produces the same structure.**

## 2. The final path (C), three stages -- CLEAN BUILD

One outer span for the whole flow, three nested spans for the stages. The stage spans are
measured inside the same repetition as the flow they are divided by.

| | mixed | skew | tranco |
|---|---:|---:|---:|
| flow total (ms) | 75.03 | 82.31 | 113.39 |
| 1. statistics (ms) | 23.49 | 30.11 | 22.15 |
| 2. planning (ms) | 0.99 | 1.01 | 1.84 |
| 3. final index construction (ms) | 44.68 | 51.56 | 89.08 |

Share of the flow, per repetition:

| stage | mixed | skew | tranco |
|---|---:|---:|---:|
| statistics | 32.2% | 33.0% | 19.4% |
| planning | 1.3% | 1.2% | 1.6% |
| **construction** | **66.5%** | **65.8%** | **79.0%** |

**The final index construction is 66%-79% of the final flow.** That is the same-round
answer to "how much is still construction": it remains the dominant term, and the two
upstream stages together are 20%-34%.

## 3. Inside the construction -- INSTRUMENTED BUILD

Constructor phases over the final build. `grp_prep` and `place` TOGETHER are the placement
work, split into "work out the group id and fingerprint" and "everything else", using
two clock calls per region rather than per key. This is why the placement loop could be
partitioned at all without distorting it.

| phase | mixed | skew | tranco |
|---|---:|---:|---:|
| `grp_prep` | 7.774 | 9.300 | 11.701 |
| `place` | 13.127 | 17.276 | 45.893 |
| `demand_pass` | 5.963 | 7.450 | 9.041 |
| `model` | 3.697 | 3.979 | 8.558 |
| `storage` | 0.711 | 0.683 | 1.552 |
| `dir` | 0.227 | 0.340 | 0.394 |
| `layout` | 0.184 | 0.166 | 0.191 |
| `prefix` | 0.010 | 0.012 | 0.010 |
| `suffix_scan` | 0.072 | 0.072 | 0.125 |
| **`ctor_total`** | **32.00** | **39.02** | **77.62** |

| share of `ctor_total` | mixed | skew | tranco |
|---|---:|---:|---:|
| `grp_prep` | 24.1% | 23.0% | 15.1% |
| `place` | 42.0% | 43.6% | 59.1% |
| `demand_pass` | 18.6% | 19.1% | 11.6% |
| `model` | 11.4% | 9.8% | 11.3% |
| `storage` | 2.3% | 1.8% | 2.0% |
| **`grp_prep` + `place`** (all placement) | **66.0%** | **67.3%** | **74.0%** |

| closure: sum of phases / `ctor_total` | 0.9979 | 0.9982 | 0.9978 |

The phases partition the constructor, so they must sum to just under `ctor_total`. They do,
to the same order as E14 (which reported 0.9991-0.9996 for its eight-phase split; this is
now nine phases, the extra one being `grp_prep`). The shortfall is again NOT attributed.

**These are instrumented numbers and are not used for any headline figure.**

## 4. Activity counts -- INSTRUMENTED BUILD, no clock calls

These operations are far too short to time individually; a timer around each would cost
more than the thing it measures and would change the cost structure. So they are counted,
not timed, and the counts are what the next step is chosen from.

Per dataset, over the final build stage, plus the statistics stage where it matters:

| counter | stage | mixed | skew | tranco | per key (mixed) |
|---|---|---:|---:|---:|---:|
| `group_calls` | build | 262,144 | 262,144 | 524,288 | 2.000 |
| `group_calls` | stats | 131,072 | 131,072 | 262,144 | 1.000 |
| `fingerprint_calls` | build | 131,072 | 131,072 | 262,144 | 1.000 |
| `region_ctors` | build | 64 | 64 | 128 | 0.000 |
| `subblock_probes` | build | 520,737 | 930,960 | 534,258 | 3.973 |
| `insert_attempts` | build | 132,113 | 133,686 | 264,442 | 1.008 |
| `insert_ok` | build | 130,321 | 126,171 | 250,385 | 0.994 |
| `entry_shifts` | build | 0 | 0 | 0 | 0.000 |
| `suffix_bytes_copied` | build | 10,589,234 | 10,095,606 | 3,034,435 | 80.789 |
| `overflow_inserts` | build | 751 | 4,901 | 11,759 | 0.006 |
| `overflow_containers` | build | 155 | 607 | 1,104 | 0.001 |

Bytes per key, for the one counter that is a byte count:

| | mixed | skew | tranco |
|---|---:|---:|---:|
| `suffix_bytes_copied` per key | 80.8 | 77.0 | 11.6 |

## 5. What is computed more than once

The counters make this visible without any timing. On the final path the group id is
computed three times per key:

| where | calls per key |
|---|---:|
| `demand_profile` (statistics stage) | 1 |
| `demand_pass` (inside the constructor) | 1 |
| placement (inside the constructor) | 1 |
| **total on the C path** | **3.00** | **3.00** | **3.00** |

Two of the three are inside the same construction. Note also that `entry_shifts` is
**exactly zero** on every dataset: the build feeds keys in sorted order, so every insert
appends at the end of its block and no descriptor is ever moved.


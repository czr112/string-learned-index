# E26: what compaction does, and what reusing the stored tag saves

Sources: `results/main/e26_diag.jsonl` (12 records, counts) and `results/main/e26_time.jsonl` (1800 records, clean timing).

Both arms sit on the **E25 default candidate** (V1 + V2). C0 and C1 differ by
**one `Config` flag**, `compact_reuse_tag`. Membership, placement order,
capacity, the compaction trigger and the boundary maintenance are untouched by
design, so the difference is attributable to the removed recomputation.

## 1. Why compaction runs, and whether it helped

`entered` counts entries into the branch that can compact (a primary placement
failed); `ran` is when it actually compacted (`group_has_room` was true);
`helped` is when the retry then placed the record in the primary.

**A `no help` is recorded as "did not enable THIS placement", not as "the
compaction had no value"** -- compaction also redistributes records across
sub-blocks, and that can matter for later operations.

| dataset | workload | ran | helped | no help | helped / ran |
|---|---|---:|---:|---:|---:|
| mixed | insert_only | 531 | 0 | 32735 | 0.0% |
| mixed | replace | 1131 | 34 | 12151 | 3.0% |
| skew | insert_only | 2168 | 0 | 32677 | 0.0% |
| skew | replace | 5103 | 179 | 31719 | 3.5% |
| tranco | insert_only | 1901 | 0 | 65204 | 0.0% |
| tranco | replace | 4065 | 108 | 27685 | 2.7% |

## 2. What a compaction costs, and what it reclaims

`garbage_before` is the un-referenced byte total **in the groups being
compacted** -- not an index-wide figure. `copied` is the suffix volume the
replay copies, and is exactly the volume C0 re-hashes.

| dataset | workload | records replayed | copied (MB) | garbage before (KB) | garbage / copied |
|---|---|---:|---:|---:|---:|
| mixed | insert_only | 148841 | 6.91 | 0.0 | 0.00% |
| mixed | replace | 256133 | 11.03 | 47.4 | 0.42% |
| skew | insert_only | 800621 | 45.88 | 0.0 | 0.00% |
| skew | replace | 1561776 | 81.95 | 286.2 | 0.34% |
| tranco | insert_only | 208157 | 2.64 | 0.0 | 0.00% |
| tranco | replace | 428402 | 5.63 | 79.1 | 1.37% |

## 3. Hashing C1 removes (from the copy volume, NOT from the diag counter)

The diagnostic build re-hashes every record to check the reuse, so its
`fp_hash_bytes` cannot show C1's saving. The column below is the volume C0
hashes during replay (`compact_bytes_copied`) against C0's total hashing.

| dataset | workload | C0 total hash (MB) | replay hash removed (MB) | share removed |
|---|---|---:|---:|---:|
| mixed | insert_only | 9.67 | 6.91 | **71.5%** |
| mixed | replace | 16.13 | 11.03 | **68.4%** |
| skew | insert_only | 48.50 | 45.88 | **94.6%** |
| skew | replace | 86.49 | 81.95 | **94.8%** |
| tranco | insert_only | 3.95 | 2.64 | **66.8%** |
| tranco | replace | 7.70 | 5.63 | **73.1%** |

## 4. C1 / C0, paired inside each repetition

Ratio below 1 = C1 faster. Ratios are formed within one repetition and reduced
to a per-process median; `dir` counts processes agreeing on the direction.

| dataset | workload | checkpoint | C0 | C1 | C2 | ART | C1/C0 | C2/C1 | **C2/C0** | **C2/ART** |
|---|---|---:|---:|---:|---:|---:|---|---|---|---|
| mixed | insert_only | 5% | 0.53 | 0.54 | 0.44 | 0.40 | **1.064** (1/5) | **0.790** (5/5) | **0.818** (5/5) | **1.116** (1/5) |
| mixed | insert_only | 10% | 1.03 | 1.02 | 0.86 | 0.74 | **0.973** (3/5) | **0.833** (5/5) | **0.884** (5/5) | **1.156** (0/5) |
| mixed | insert_only | 25% | 2.76 | 2.67 | 2.28 | 2.01 | **0.960** (3/5) | **0.826** (5/5) | **0.812** (5/5) | **1.136** (0/5) |
| mixed | replace | 5% | 0.87 | 0.91 | 0.91 | 0.60 | **1.035** (0/5) | **1.035** (1/5) | **1.052** (1/5) | **1.505** (0/5) |
| mixed | replace | 10% | 1.75 | 1.77 | 1.75 | 1.08 | **1.025** (1/5) | **1.000** (3/5) | **1.026** (2/5) | **1.601** (0/5) |
| mixed | replace | 25% | 4.07 | 4.26 | 3.84 | 2.14 | **1.035** (0/5) | **0.915** (5/5) | **0.974** (4/5) | **1.856** (0/5) |
| skew | insert_only | 5% | 24.60 | 24.02 | 0.82 | 1.86 | **0.976** (3/5) | **0.035** (5/5) | **0.033** (5/5) | **0.443** (5/5) |
| skew | insert_only | 10% | 50.70 | 48.97 | 1.68 | 3.60 | **0.941** (5/5) | **0.035** (5/5) | **0.034** (5/5) | **0.468** (5/5) |
| skew | insert_only | 25% | 83.50 | 80.39 | 4.12 | 8.68 | **0.948** (4/5) | **0.052** (5/5) | **0.051** (5/5) | **0.486** (5/5) |
| skew | replace | 5% | 25.38 | 25.39 | 13.47 | 2.09 | **0.979** (4/5) | **0.531** (5/5) | **0.526** (5/5) | **6.422** (0/5) |
| skew | replace | 10% | 51.61 | 50.42 | 26.59 | 4.08 | **0.978** (5/5) | **0.528** (5/5) | **0.511** (5/5) | **6.363** (0/5) |
| skew | replace | 25% | 110.06 | 104.64 | 57.00 | 9.63 | **0.947** (5/5) | **0.541** (5/5) | **0.502** (5/5) | **5.952** (0/5) |
| tranco | insert_only | 5% | 4.11 | 3.90 | 1.44 | 1.07 | **0.981** (4/5) | **0.369** (5/5) | **0.359** (5/5) | **1.343** (0/5) |
| tranco | insert_only | 10% | 8.73 | 8.35 | 3.04 | 1.89 | **0.981** (4/5) | **0.350** (5/5) | **0.344** (5/5) | **1.638** (0/5) |
| tranco | insert_only | 25% | 22.73 | 21.85 | 7.25 | 4.23 | **0.968** (4/5) | **0.329** (5/5) | **0.326** (5/5) | **1.688** (0/5) |
| tranco | replace | 5% | 4.73 | 4.62 | 3.69 | 1.27 | **0.989** (4/5) | **0.822** (5/5) | **0.797** (5/5) | **2.957** (0/5) |
| tranco | replace | 10% | 8.18 | 8.01 | 6.78 | 2.58 | **0.979** (4/5) | **0.838** (5/5) | **0.861** (5/5) | **2.676** (0/5) |
| tranco | replace | 25% | 25.40 | 24.99 | 20.16 | 5.51 | **0.985** (3/5) | **0.826** (5/5) | **0.797** (5/5) | **3.736** (0/5) |

`(dir)` is how many of the independent processes agree the numerator is faster.
C1/C0 isolates the removed recomputation. C2/C1 isolates the trigger gate. C2/C0
is the two together. **None of these is formed by multiplying the other two** --
each is measured directly inside the same repetition.

**ART runs in the SAME repetition.** A historical ART time is not evidence about
a new arm, so the C2/ART column is a same-round comparison or it is nothing.


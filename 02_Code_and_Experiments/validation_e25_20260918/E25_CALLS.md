# E25 call matrix: what each write API invokes, per category

Source: `results/main/e25_calls.jsonl`, 93 records (3 datasets x 3 variants x the categories below).

**These are counts, not timings.** They say what work each API performs; they
are never converted into a speed-up here. E25's timing comparison runs on a
separate clean build, and a reduced call count is not by itself a performance
result.

**V0 is the CURRENT source with the default switches** (both E25 flags off),
measured now -- not a record taken before the change was written.

## 0. Column meanings, and what is deliberately kept apart

| col | counter | means |
|---|---|---|
| L | `locate_calls` | outer routing (`Index::locate`) |
| P | `predict_calls` | model predictions |
| S | `existence_searches` | entries into `Region::find_impl` -- full duplicate checks |
| F | `fp_of_calls` | `Fingerprint::of`, the WHOLE-KEY form. **Auxiliary only** |
| H | `fp_hash_bytes` | **bytes actually fed to the fingerprint hash** |
| C | `cmp_calls` | full-key string comparisons |
| B | `cmp_bytes` | bytes those comparisons walked (LOGICAL, not DRAM reads) |

**F/H are split on purpose.** The model prediction hashes the bytes it consumes
and `find_impl` finishes the key with `tail()`, so the fused path produces a
full fingerprint **without ever calling `Fingerprint::of`**. Counting that
function alone would make fingerprint work look like it vanished when it only
moved. `F` is reported so the two forms can be told apart, never as the total.

**P, S are separate counters.** Predicting the group again after a duplicate
check is NOT the same as running two full searches, and the table must not let
one be read as the other.

**Placement failures are the FIRST rejection reason** inside `insert_into` (slot
count is checked before suffix bytes), so they are mutually exclusive and their
sum is bounded by `place_attempts`. They are not overlapping conditions to be
added up.

**The diagnostic window** is explicit: counters are reset immediately before a
category's operations and read immediately after. Key selection, reference
upkeep and answer checking run outside it and are not counted.

## mixed

Per-operation averages. `L/P/S/H/C`.

| api | category | ops | V0 | V1 | V2 |
|---|---|---:|---|---|---|
| update | `primary_hit` | 2024 | L1.0 P1.0 S0.0 H0 C70.7 | L1.0 P1.0 S1.0 H81 C1.3 | L1.0 P1.0 S1.0 H81 C1.3 |
| update | `overflow_hit` | 9 | L1.0 P1.0 S0.0 H0 C579.2 | L1.0 P1.0 S1.0 H127 C3.2 | L1.0 P1.0 S1.0 H127 C3.2 |
| update | `miss` | 2048 | L1.0 P1.0 S0.0 H0 C149.2 | L1.0 P1.0 S1.0 H91 C0.0 | L1.0 P1.0 S1.0 H91 C0.0 |
| erase | `primary_hit` | 2024 | L1.0 P1.0 S0.0 H0 C69.7 | L1.0 P1.0 S1.0 H81 C1.3 | L1.0 P1.0 S1.0 H81 C1.3 |
| erase | `overflow_hit` | 9 | L1.0 P1.0 S0.0 H0 C570.1 | L1.0 P1.0 S1.0 H127 C3.2 | L1.0 P1.0 S1.0 H127 C3.2 |
| erase | `miss` | 2048 | L1.0 P1.0 S0.0 H0 C146.9 | L1.0 P1.0 S1.0 H91 C0.0 | L1.0 P1.0 S1.0 H91 C0.0 |
| insert | `new_key` | 2048 | L1.0 P2.0 S1.0 H606 C173.1 | L1.0 P2.0 S1.0 H606 C173.1 | L1.0 P1.0 S1.0 H516 C173.1 |
| insert | `duplicate_key` | 2032 | L1.0 P1.0 S1.0 H81 C1.3 | L1.0 P1.0 S1.0 H81 C1.3 | L1.0 P1.0 S1.0 H81 C1.3 |
| upsert | `new_key` | 2048 | L2.0 P3.0 S1.0 H680 C348.9 | L2.0 P3.0 S2.0 H769 C197.5 | L1.0 P1.0 S1.0 H591 C197.5 |
| upsert | `existing_key` | 2029 | L1.0 P1.0 S0.0 H0 C70.2 | L1.0 P1.0 S1.0 H80 C1.2 | L1.0 P1.0 S1.0 H80 C1.2 |
| structural | `region_split_build` | 200 | -- | -- | L1.0 P2.7 S1.0 H11 C19.1 |

### mixed: comparison bytes and placement, per operation

| api | category | V0 | V1 | V2 |
|---|---|---|---|---|
| update | `primary_hit` | B1617 att 0.0 (slot 0.0 / bytes 0.0) | B87 att 0.0 (slot 0.0 / bytes 0.0) | B87 att 0.0 (slot 0.0 / bytes 0.0) |
| update | `overflow_hit` | B31503 att 0.0 (slot 0.0 / bytes 0.0) | B270 att 0.0 (slot 0.0 / bytes 0.0) | B270 att 0.0 (slot 0.0 / bytes 0.0) |
| update | `miss` | B3334 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `primary_hit` | B1595 att 0.0 (slot 0.0 / bytes 0.0) | B87 att 0.0 (slot 0.0 / bytes 0.0) | B87 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `overflow_hit` | B30972 att 0.0 (slot 0.0 / bytes 0.0) | B270 att 0.0 (slot 0.0 / bytes 0.0) | B270 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `miss` | B3281 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) |
| insert | `new_key` | B2718 att 10.8 (slot 1.7 / bytes 0.5) | B2718 att 10.8 (slot 1.7 / bytes 0.5) | B2718 att 10.8 (slot 1.7 / bytes 0.5) |
| insert | `duplicate_key` | B87 att 0.0 (slot 0.0 / bytes 0.0) | B87 att 0.0 (slot 0.0 / bytes 0.0) | B87 att 0.0 (slot 0.0 / bytes 0.0) |
| upsert | `new_key` | B6436 att 12.0 (slot 1.7 / bytes 0.5) | B3091 att 12.0 (slot 1.7 / bytes 0.5) | B3091 att 12.0 (slot 1.7 / bytes 0.5) |
| upsert | `existing_key` | B1412 att 0.0 (slot 0.0 / bytes 0.0) | B85 att 0.0 (slot 0.0 / bytes 0.0) | B85 att 0.0 (slot 0.0 / bytes 0.0) |
| structural | `region_split_build` | -- | -- | B30 att 3.0 (slot 0.5 / bytes 0.0) |

### mixed: erase work and string lifetime

`erase_released_primary` counts suffix bytes whose record went away but whose
bytes STAY in the block as garbage; `erase_released_overflow` counts bytes
whose string is actually destroyed. They are reported apart because only the
second is a real free.

| api | category | V0 | V1 | V2 |
|---|---|---|---|---|
| erase | `primary_hit` | shift 12.4 relP 81 relO 0 alloc 0.0 free 0.0 | shift 12.4 relP 81 relO 0 alloc 0.0 free 0.0 | shift 12.4 relP 81 relO 0 alloc 0.0 free 0.0 |
| erase | `overflow_hit` | shift 4.1 relP 0 relO 127 alloc 0.0 free 1.0 | shift 4.1 relP 0 relO 127 alloc 0.0 free 1.0 | shift 4.1 relP 0 relO 127 alloc 0.0 free 1.0 |
| erase | `miss` | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 |

## skew

Per-operation averages. `L/P/S/H/C`.

| api | category | ops | V0 | V1 | V2 |
|---|---|---:|---|---|---|
| update | `primary_hit` | 2018 | L1.0 P1.0 S0.0 H0 C137.1 | L1.0 P1.0 S1.0 H80 C1.5 | L1.0 P1.0 S1.0 H80 C1.5 |
| update | `overflow_hit` | 15 | L1.0 P1.0 S0.0 H0 C300.4 | L1.0 P1.0 S1.0 H82 C2.0 | L1.0 P1.0 S1.0 H82 C2.0 |
| update | `miss` | 2048 | L1.0 P1.0 S0.0 H0 C278.0 | L1.0 P1.0 S1.0 H91 C0.0 | L1.0 P1.0 S1.0 H91 C0.0 |
| erase | `primary_hit` | 2018 | L1.0 P1.0 S0.0 H0 C135.0 | L1.0 P1.0 S1.0 H80 C1.5 | L1.0 P1.0 S1.0 H80 C1.5 |
| erase | `overflow_hit` | 15 | L1.0 P1.0 S0.0 H0 C295.1 | L1.0 P1.0 S1.0 H82 C2.0 | L1.0 P1.0 S1.0 H82 C2.0 |
| erase | `miss` | 2048 | L1.0 P1.0 S0.0 H0 C273.6 | L1.0 P1.0 S1.0 H91 C0.0 | L1.0 P1.0 S1.0 H91 C0.0 |
| insert | `new_key` | 2048 | L1.0 P2.0 S1.0 H2646 C865.3 | L1.0 P2.0 S1.0 H2646 C865.3 | L1.0 P1.0 S1.0 H2558 C865.3 |
| insert | `duplicate_key` | 2032 | L1.0 P1.0 S1.0 H80 C1.5 | L1.0 P1.0 S1.0 H80 C1.5 | L1.0 P1.0 S1.0 H80 C1.5 |
| upsert | `new_key` | 2048 | L2.0 P3.0 S1.0 H2902 C1170.5 | L2.0 P3.0 S2.0 H2991 C886.7 | L1.0 P1.0 S1.0 H2813 C886.7 |
| upsert | `existing_key` | 2029 | L1.0 P1.0 S0.0 H0 C143.5 | L1.0 P1.0 S1.0 H80 C1.5 | L1.0 P1.0 S1.0 H80 C1.5 |
| structural | `region_split_build` | 200 | -- | -- | L1.0 P2.7 S1.0 H11 C19.1 |

### skew: comparison bytes and placement, per operation

| api | category | V0 | V1 | V2 |
|---|---|---|---|---|
| update | `primary_hit` | B2125 att 0.0 (slot 0.0 / bytes 0.0) | B88 att 0.0 (slot 0.0 / bytes 0.0) | B88 att 0.0 (slot 0.0 / bytes 0.0) |
| update | `overflow_hit` | B4607 att 0.0 (slot 0.0 / bytes 0.0) | B98 att 0.0 (slot 0.0 / bytes 0.0) | B98 att 0.0 (slot 0.0 / bytes 0.0) |
| update | `miss` | B4240 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `primary_hit` | B2093 att 0.0 (slot 0.0 / bytes 0.0) | B88 att 0.0 (slot 0.0 / bytes 0.0) | B88 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `overflow_hit` | B4527 att 0.0 (slot 0.0 / bytes 0.0) | B98 att 0.0 (slot 0.0 / bytes 0.0) | B98 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `miss` | B4173 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) |
| insert | `new_key` | B13376 att 45.5 (slot 1.8 / bytes 1.1) | B13376 att 45.5 (slot 1.8 / bytes 1.1) | B13376 att 45.5 (slot 1.8 / bytes 1.1) |
| insert | `duplicate_key` | B88 att 0.0 (slot 0.0 / bytes 0.0) | B88 att 0.0 (slot 0.0 / bytes 0.0) | B88 att 0.0 (slot 0.0 / bytes 0.0) |
| upsert | `new_key` | B18196 att 46.2 (slot 1.9 / bytes 1.1) | B13865 att 46.2 (slot 1.9 / bytes 1.1) | B13865 att 46.2 (slot 1.9 / bytes 1.1) |
| upsert | `existing_key` | B2215 att 0.0 (slot 0.0 / bytes 0.0) | B88 att 0.0 (slot 0.0 / bytes 0.0) | B88 att 0.0 (slot 0.0 / bytes 0.0) |
| structural | `region_split_build` | -- | -- | B30 att 3.0 (slot 0.5 / bytes 0.0) |

### skew: erase work and string lifetime

`erase_released_primary` counts suffix bytes whose record went away but whose
bytes STAY in the block as garbage; `erase_released_overflow` counts bytes
whose string is actually destroyed. They are reported apart because only the
second is a real free.

| api | category | V0 | V1 | V2 |
|---|---|---|---|---|
| erase | `primary_hit` | shift 18.7 relP 80 relO 0 alloc 0.0 free 0.0 | shift 18.7 relP 80 relO 0 alloc 0.0 free 0.0 | shift 18.7 relP 80 relO 0 alloc 0.0 free 0.0 |
| erase | `overflow_hit` | shift 2.7 relP 0 relO 82 alloc 0.0 free 1.0 | shift 2.7 relP 0 relO 82 alloc 0.0 free 1.0 | shift 2.7 relP 0 relO 82 alloc 0.0 free 1.0 |
| erase | `miss` | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 |

## tranco

Per-operation averages. `L/P/S/H/C`.

| api | category | ops | V0 | V1 | V2 |
|---|---|---:|---|---|---|
| update | `primary_hit` | 2026 | L1.0 P1.0 S0.0 H0 C35.1 | L1.0 P1.0 S1.0 H12 C1.0 | L1.0 P1.0 S1.0 H12 C1.0 |
| update | `overflow_hit` | 14 | L1.0 P1.0 S0.0 H0 C112.4 | L1.0 P1.0 S1.0 H13 C1.0 | L1.0 P1.0 S1.0 H13 C1.0 |
| update | `miss` | 2048 | L1.0 P1.0 S0.0 H0 C67.7 | L1.0 P1.0 S1.0 H23 C0.0 | L1.0 P1.0 S1.0 H23 C0.0 |
| erase | `primary_hit` | 2026 | L1.0 P1.0 S0.0 H0 C34.8 | L1.0 P1.0 S1.0 H12 C1.0 | L1.0 P1.0 S1.0 H12 C1.0 |
| erase | `overflow_hit` | 14 | L1.0 P1.0 S0.0 H0 C111.8 | L1.0 P1.0 S1.0 H13 C1.0 | L1.0 P1.0 S1.0 H13 C1.0 |
| erase | `miss` | 2048 | L1.0 P1.0 S0.0 H0 C67.1 | L1.0 P1.0 S1.0 H23 C0.0 | L1.0 P1.0 S1.0 H23 C0.0 |
| insert | `new_key` | 2048 | L1.0 P2.0 S1.0 H113 C105.1 | L1.0 P2.0 S1.0 H113 C105.1 | L1.0 P1.0 S1.0 H94 C105.1 |
| insert | `duplicate_key` | 2039 | L1.0 P1.0 S1.0 H12 C1.0 | L1.0 P1.0 S1.0 H12 C1.0 | L1.0 P1.0 S1.0 H12 C1.0 |
| upsert | `new_key` | 2048 | L2.0 P3.0 S1.0 H117 C173.7 | L2.0 P3.0 S2.0 H136 C106.9 | L1.0 P1.0 S1.0 H98 C106.9 |
| upsert | `existing_key` | 2039 | L1.0 P1.0 S0.0 H0 C34.5 | L1.0 P1.0 S1.0 H12 C1.0 | L1.0 P1.0 S1.0 H12 C1.0 |
| structural | `region_split_build` | 200 | -- | -- | L1.0 P2.7 S1.0 H11 C19.1 |

### tranco: comparison bytes and placement, per operation

| api | category | V0 | V1 | V2 |
|---|---|---|---|---|
| update | `primary_hit` | B158 att 0.0 (slot 0.0 / bytes 0.0) | B12 att 0.0 (slot 0.0 / bytes 0.0) | B12 att 0.0 (slot 0.0 / bytes 0.0) |
| update | `overflow_hit` | B380 att 0.0 (slot 0.0 / bytes 0.0) | B13 att 0.0 (slot 0.0 / bytes 0.0) | B13 att 0.0 (slot 0.0 / bytes 0.0) |
| update | `miss` | B292 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `primary_hit` | B156 att 0.0 (slot 0.0 / bytes 0.0) | B12 att 0.0 (slot 0.0 / bytes 0.0) | B12 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `overflow_hit` | B378 att 0.0 (slot 0.0 / bytes 0.0) | B13 att 0.0 (slot 0.0 / bytes 0.0) | B13 att 0.0 (slot 0.0 / bytes 0.0) |
| erase | `miss` | B290 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) | B0 att 0.0 (slot 0.0 / bytes 0.0) |
| insert | `new_key` | B472 att 7.7 (slot 1.8 / bytes 0.3) | B472 att 7.7 (slot 1.8 / bytes 0.3) | B472 att 7.7 (slot 1.8 / bytes 0.3) |
| insert | `duplicate_key` | B12 att 0.0 (slot 0.0 / bytes 0.0) | B12 att 0.0 (slot 0.0 / bytes 0.0) | B12 att 0.0 (slot 0.0 / bytes 0.0) |
| upsert | `new_key` | B781 att 7.9 (slot 1.8 / bytes 0.3) | B498 att 7.9 (slot 1.8 / bytes 0.3) | B498 att 7.9 (slot 1.8 / bytes 0.3) |
| upsert | `existing_key` | B152 att 0.0 (slot 0.0 / bytes 0.0) | B12 att 0.0 (slot 0.0 / bytes 0.0) | B12 att 0.0 (slot 0.0 / bytes 0.0) |
| structural | `region_split_build` | -- | -- | B30 att 3.0 (slot 0.5 / bytes 0.0) |

### tranco: erase work and string lifetime

`erase_released_primary` counts suffix bytes whose record went away but whose
bytes STAY in the block as garbage; `erase_released_overflow` counts bytes
whose string is actually destroyed. They are reported apart because only the
second is a real free.

| api | category | V0 | V1 | V2 |
|---|---|---|---|---|
| erase | `primary_hit` | shift 14.7 relP 12 relO 0 alloc 0.0 free 0.0 | shift 14.7 relP 12 relO 0 alloc 0.0 free 0.0 | shift 14.7 relP 12 relO 0 alloc 0.0 free 0.0 |
| erase | `overflow_hit` | shift 1.4 relP 0 relO 13 alloc 0.0 free 1.0 | shift 1.4 relP 0 relO 13 alloc 0.0 free 1.0 | shift 1.4 relP 0 relO 13 alloc 0.0 free 1.0 |
| erase | `miss` | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 | shift 0.0 relP 0 relO 0 alloc 0.0 free 0.0 |

## Structural rows, reported separately

A region rebuild or split is not a fixed cost per operation, so it is never
folded into the per-operation averages above. The row below is a purpose-built
phase (`region_keys = 16`, 200 inserts) whose only job is to make those events
happen and be counted.

| dataset | api | category | variant | ops | rebuilds | compactions | predictions |
|---|---|---|---|---:|---:|---:|---:|
| mixed | structural | `region_split_build` | V2 | 200 | 11 | 0 | 541 |
| skew | structural | `region_split_build` | V2 | 200 | 11 | 0 | 541 |
| tranco | structural | `region_split_build` | V2 | 200 | 11 | 0 | 541 |

